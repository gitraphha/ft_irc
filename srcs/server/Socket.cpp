#include "Socket.hpp"

#include "ServerHelper.hpp" // for getAdressFamilyStr, sockAddrFamily

#include <poll.h>       // for pollfd, POLLERR
#include <sstream>      // for basic_ostringstream, basic_ostream, oper...
#include <sys/socket.h> // for recv, send, MSG_NOSIGNAL
#include <unistd.h>     // for close

void Socket::addSocket(int const fd, bool srvSocket) {
    sockIODataStore_.insert(std::make_pair(fd, SocketIOData(srvSocket)));
}

bool Socket::checkSocketData(int const fd) {
    return (getSockIOData(fd) != NULL);
}

bool Socket::checkSocket4Recv(struct pollfd &pfd, SocketIOData &sIOD) {
    if ((pfd.revents & POLLERR)) {
        ++sIOD.recvErrorCount;
        if (sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            sIOD.closeFd = true;
            return false;
        }
    }
    return true;
}

bool Socket::updateBuffer_(struct pollfd &pfd) {
    SocketIOData *sIODptr = getSockIOData(pfd.fd);
    if (!sIODptr || sIODptr->closeFd)
        return false;
    SocketIOData &sIOD = *sIODptr;

    if (!checkSocket4Recv(pfd, sIOD))
        return false;

    char recvBuffer[RECVBUFFLEN];
    sIOD.bytesRead = recv(pfd.fd, &recvBuffer[0], RECVBUFFLEN, 0);
    if (sIOD.bytesRead <= 0) {
        ++sIOD.recvErrorCount;
        if (sIOD.bytesRead == 0 || sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            {
                std::ostringstream oss;
                oss << "Client sockFd:" << pfd.fd;
                if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
                    oss << " exceeded error limit (" << SOCKETERRORLIMIT << ")";
                else if (sIOD.bytesRead == 0)
                    oss << " closed connection on their side";
                logger_.logMsg(oss.str(), Logger::lmInfo);
            }
            sIOD.closeFd = true;
            return false;
        }
        return true;
    }
    sIOD.readBuffer += std::string(&recvBuffer[0], sIOD.bytesRead);
    return true;
}

std::string Socket::getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds,
                                          bool &msgTooLong) {
    buffHasMoreCmds = false;

    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return "";
    SocketIOData &sIOD = *sIODptr;

    size_t lfcrPos = bufferLengthAndSanityCheck_(sIOD, msgTooLong);
    if (lfcrPos == std::string::npos)
        return "";

    std::string cmdLine = sIOD.readBuffer.substr(0, lfcrPos);
    sIOD.recvErrorCount = 0;
    sIOD.readBuffer = sIOD.readBuffer.substr(lfcrPos + 2);
    buffHasMoreCmds = ((sIOD.readBuffer.size() > MAXMSGLEN) ||
                       (sIOD.readBuffer.find("\r\n") != std::string::npos));
    return cmdLine;
}

size_t Socket::bufferLengthAndSanityCheck_(SocketIOData &sIOD,
                                           bool         &msgTooLong) {
    std::string &rdBuffer = sIOD.readBuffer;
    size_t       lfcrPos = rdBuffer.find("\r\n");

    msgTooLong = false;
    if (!sIOD.discardMode) {
        if ((rdBuffer.length() < MAXMSGLEN) ||
            (lfcrPos != std::string::npos && lfcrPos <= MAXMSGLEN - 2)) {
            return lfcrPos;
        } else {
            sIOD.discardMode = true;
            msgTooLong = true;
        }
    }
    // discard mode
    if (++sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
        sIOD.closeFd = true;
        return std::string::npos;
    }
    if (lfcrPos != std::string::npos) {
        rdBuffer.erase(0, lfcrPos + 2);
        sIOD.discardMode = false;
        return std::string::npos;
    }
    if (!rdBuffer.empty() && '\r' == *rdBuffer.rbegin())
        rdBuffer = "\r";
    else
        rdBuffer.clear();
    return std::string::npos;
}

/*
    sendMsg
    * send string msg to a network socket
    * uses i/o buffers and error counters in SocketIOData
	* returns false in case of too many errors - caller must close socket

	NOTE: caller MUST close socket when sendMsg returns false
*/
bool Socket::sendMsg(int const fd, std::string const &msg) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return false;

    SocketIOData &sIOD = *sIODptr;
    if (msg.length() >
        sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length()) {
        sIOD.closeFd = true;
        return false;
    }
    sIOD.writeBuffer += msg;
    ssize_t bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                             sIOD.writeBuffer.length(), MSG_NOSIGNAL);
    if (bytesSent <= 0) {
        ++sIOD.sendErrorCount;
        if (sIOD.sendErrorCount >= SOCKETERRORLIMIT) {
            sIOD.closeFd = true;
            return false;
        }
        return true;
    }

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
    return true;
}

bool Socket::socketCloseMarker(int const fd) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return true;
    return sIODptr->closeFd;
}

void Socket::closeSocket(int const fd) {
    SocketIOData *sIODptr = getSockIOData(fd);
    int const     addrFam = srvHelp::sockAddrFamily(fd);
    close(fd);

    if (!sIODptr)
        return;
    SocketIOData      &sIOD = *sIODptr;
    std::ostringstream oss;
    if (sIOD.isSrvSocket) {
        oss << "Closing server socket (" << srvHelp::getAdressFamilyStr(addrFam)
            << ")";
    } else {
        oss << "Closing client sockFd:" << fd;
    }
    logger_.logMsg(oss.str(), Logger::lmInfo);

    sockIODataStore_.erase(fd);
}

Socket::SocketIOData *Socket::getSockIOData(int const fd) {
    std::map<int, SocketIOData>::iterator it = sockIODataStore_.find(fd);
    if (it == sockIODataStore_.end())
        return NULL;
    return &(it->second);
}

bool Socket::isServerSocket(int const fd) {
    SocketIOData const *const sIODptr = getSockIOData(fd);
    if (sIODptr)
        return sIODptr->isSrvSocket;
    return false;
}
