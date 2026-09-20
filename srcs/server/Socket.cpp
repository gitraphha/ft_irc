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

// POLLERR counts as one receive error, and the socket is closed on the third
// consecutive one. So a client is not dropped on the first POLLERR, there has
// to be three in a row without a valid line in between.
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

// A line ends at '\n' and any '\r' in front of it is dropped. RFC 2812 wants
// "\r\n", but the modern IRC spec (modern.ircdocs.horse) says servers should
// handle a single '\n' as if it were "\r\n", to support clients that send it.
// ergo and InspIRCd do the same.
std::string Socket::getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds,
                                          bool &msgTooLong) {
    buffHasMoreCmds = false;

    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return "";
    SocketIOData &sIOD = *sIODptr;

    size_t      eol = bufferLengthAndSanityCheck_(sIOD, msgTooLong);
    std::string cmdLine;
    if (eol != std::string::npos) {
        cmdLine = sIOD.readBuffer.substr(0, eol);
        if (!cmdLine.empty() && *cmdLine.rbegin() == '\r')
            cmdLine.erase(cmdLine.size() - 1);
        sIOD.recvErrorCount = 0;
        sIOD.readBuffer = sIOD.readBuffer.substr(eol + 1);
    }
    buffHasMoreCmds = ((sIOD.readBuffer.size() > MAXMSGLEN) ||
                       (sIOD.readBuffer.find('\n') != std::string::npos));
    return cmdLine;
}

size_t Socket::bufferLengthAndSanityCheck_(SocketIOData &sIOD,
                                           bool         &msgTooLong) {
    std::string &rdBuffer = sIOD.readBuffer;
    size_t       eol = rdBuffer.find('\n');

    msgTooLong = false;
    if (!sIOD.discardMode) {
        if ((rdBuffer.length() < MAXMSGLEN) ||
            (eol != std::string::npos && eol < MAXMSGLEN)) {
            return eol;
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
    if (eol != std::string::npos) {
        rdBuffer.erase(0, eol + 1);
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
*/
void Socket::sendMsg(int const fd, std::string const &msg) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return;

    SocketIOData &sIOD = *sIODptr;
    if (msg.length() >
        sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length()) {
        sIOD.closeFd = true;
        return;
    }
    sIOD.writeBuffer += msg;
    ssize_t bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                             sIOD.writeBuffer.length(), MSG_NOSIGNAL);
    if (bytesSent <= 0) {
        ++sIOD.sendErrorCount;
        if (sIOD.sendErrorCount >= SOCKETERRORLIMIT)
            sIOD.closeFd = true;
        return;
    }

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
}

bool Socket::socketCloseMarker(int const fd) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return true;
    return sIODptr->closeFd;
}

void Socket::closeSocket(int const fd) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr) {
        close(fd);
        return;
    }
    // call sockAddrFamily only on known fd
    int const          addrFam = srvHelp::sockAddrFamily(fd);
    SocketIOData      &sIOD = *sIODptr;
    std::ostringstream oss;
    if (sIOD.isSrvSocket)
        oss << "Closing server socket (" << srvHelp::getAdressFamilyStr(addrFam)
            << ")";
    else
        oss << "Closing client sockFd:" << fd;
    logger_.logMsg(oss.str(), Logger::lmInfo);

    close(fd);
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
