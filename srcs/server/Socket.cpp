#include "Socket.hpp"

#include "ServerHelper.hpp" // for getAdressFamilyStr, sockAddrFamily

#include <poll.h>       // for pollfd, POLLERR
#include <sstream>      // for basic_ostringstream, basic_ostream, oper...
#include <sys/socket.h> // for recv, send, MSG_NOSIGNAL
#include <unistd.h>     // for close

void Socket::addSocket(int const fd, bool srvSocket) {
    sockIODataStore_.insert(std::make_pair(fd, SocketIOData(srvSocket)));
}

bool Socket::checkSocket4Recv(struct pollfd &pfd) {
    SocketIOData *sIODptr = getSockIOData(pfd.fd);
    if (!sIODptr)
        return false;

    if ((pfd.revents & POLLERR)) {
        if (sIODptr->recvErrorCount >= SOCKETERRORLIMIT)
            closeSocket(pfd.fd);
        else
            ++sIODptr->recvErrorCount;
        return false;
    }
    return true;
}

bool Socket::updateBuffer_(int const fd, SocketIOData &sIOD) {
    char recvBuffer[RECVBUFFLEN];
    sIOD.bytesRead = recv(fd, &recvBuffer[0], RECVBUFFLEN, 0);
    if (sIOD.bytesRead <= 0) {
        ++sIOD.recvErrorCount;
        if (sIOD.bytesRead == 0 || sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            std::ostringstream oss;
            oss << "Client sockFd:" << fd;
            if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
                oss << " exceeded error limit (" << SOCKETERRORLIMIT << ")";
            else if (sIOD.bytesRead == 0)
                oss << " closed connection on their side";
            logger_.logMsg(oss.str(), Logger::lmInfo);
            closeSocket(fd);
        }
        return false;
    }
    sIOD.readBuffer += std::string(&recvBuffer[0], sIOD.bytesRead);
    return true;
}

std::string Socket::getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds,
                                          bool &msgTooLong) {
    SocketIOData *sIODptr = getSockIOData(fd);
    if (!sIODptr)
        return "";

    SocketIOData &sIOD = *sIODptr;
    if (!buffHasMoreCmds) {
        if (!updateBuffer_(fd, sIOD))
            return "";
    }
    size_t lfcrPos = bufferLengthAndSanityCheck_(fd, msgTooLong);
    if (msgTooLong || lfcrPos == std::string::npos)
        return "";

    std::string cmdLine;
    cmdLine = sIOD.readBuffer.substr(0, lfcrPos);
    sIOD.recvErrorCount = 0;
    sIOD.readBuffer = sIOD.readBuffer.substr(lfcrPos + 2);
    buffHasMoreCmds = (sIOD.readBuffer.find("\r\n") != std::string::npos);
    return cmdLine;
}

size_t Socket::bufferLengthAndSanityCheck_(int const fd, bool &msgTooLong) {
    Socket::SocketIOData &sIOD = sockIODataStore_.at(fd);
    std::string          &rdBuffer = sIOD.readBuffer;
    size_t                lfcrPos = rdBuffer.find("\r\n");

    if (!sIOD.discardMode) {
        if ((rdBuffer.length() < MAXMSGLEN) ||
            (lfcrPos != std::string::npos && lfcrPos <= MAXMSGLEN - 2))
            return lfcrPos;
        sIOD.discardMode = true;
        msgTooLong = true;
    }
    // discard mode
    if (++sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
        closeSocket(fd);
        return std::string::npos;
    }
    if (lfcrPos != std::string::npos) {
        rdBuffer.erase(0, lfcrPos + 2);
        sIOD.discardMode = false;
        sIOD.recvErrorCount = 0;
        return rdBuffer.find("\r\n");
    }
    // lfcrPos == std::string::npos
    if (!rdBuffer.empty() && '\r' == *rdBuffer.rbegin())
        rdBuffer = "\r";
    else
        rdBuffer.clear();
    return lfcrPos;
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
    ssize_t       bytesSent = -1;
    bool          success = true;

    if (msg.length() > sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length())
        return false;
    sIOD.writeBuffer += msg;
    ssize_t bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                             sIOD.writeBuffer.length(), MSG_NOSIGNAL);
    if (bytesSent == -1) {
        ++sIOD.sendErrorCount;
        if (sIOD.sendErrorCount >= SOCKETERRORLIMIT)
            return false;
        return true;
    }

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length()) {
#if NETCATDBG
        std::ostringstream oss;
        oss << "SendMsg event:\n"
            << " WriteBuffer: '" << sIOD.writeBuffer << "':\n";
        oss << " partly (" << bytesSent << "/" << sIOD.writeBuffer.length()
            << ") " << sIOD.writeBuffer.substr(0, bytesSent)
            << " sent to FD: " << fd;
        logger_.logMsg(oss.str(), Logger::lmInfo);
#endif
        sIOD.writeBuffer.erase(0, bytesSent);
    } else
        sIOD.writeBuffer.clear();
    return true;
}

/*
Performance note: 2 searches of sockIODataStore.
Wrap in try catch to remove this redundancy
or make socketClosed return a pointer instead
*/
void Socket::closeSocket(int const fd) {
    if (socketClosed(fd))
        return;
    SocketIOData &sIOD = sockIODataStore_.at(fd); // throws

    std::ostringstream oss;
    if (sIOD.isSrvSocket) {
        oss << "Closing server socket ("
            << srvHelp::getAdressFamilyStr(srvHelp::sockAddrFamily(fd)) << ")";
    } else {
        oss << "Closing client sockFd:" << fd;
    }
    logger_.logMsg(oss.str(), Logger::lmInfo);

    close(fd);
    sockIODataStore_.erase(fd);
}
