#include "Socket.hpp"

#include "ServerHelper.hpp" // for getAdressFamilyStr, sockAddrFamily

#include <poll.h>       // for pollfd, POLLERR
#include <sstream>      // for basic_ostringstream, basic_ostream, oper...
#include <sys/socket.h> // for recv, send, MSG_NOSIGNAL
#include <unistd.h>     // for close

void Socket::addSocket(int const fd, bool srvSocket) {
    sockIODataStore_.insert(std::make_pair(fd, SocketIOData(srvSocket)));
}

// TODO: not used anymore as POLLERR closes socket nad updateBuffer is not called
bool Socket::checkSocket4Recv(struct pollfd &pfd, SocketIOData &sIOD) {
    if ((pfd.revents & POLLERR)) {
        ++sIOD.recvErrorCount;
        if (sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            sIOD.closeFd = true;
            sIOD.closeMsg = "SOCKETERRORLIMIT exceeded (POLLERR)";
            return false;
        }
    }
    return true;
}

bool Socket::updateBuffer_(struct pollfd &pfd) {
    SocketIOData &sIOD = getSockIOData(pfd.fd);
    if (sIOD.closeFd)
        return false;

    if (!checkSocket4Recv(pfd, sIOD))
        return false;

    char recvBuffer[RECVBUFFLEN];
    sIOD.bytesRead = recv(pfd.fd, &recvBuffer[0], RECVBUFFLEN, 0);
    if (sIOD.bytesRead <= 0) {
        ++sIOD.recvErrorCount;
        if (sIOD.bytesRead == 0 || sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            std::ostringstream clntSockInfo;
            clntSockInfo << "Client sockFd:" << pfd.fd << ": ";
            std::string reason;
            if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
                reason = "SOCKETERRORLIMIT exceeded (recv)";
            else if (sIOD.bytesRead == 0)
                reason = "closed connection on their side";
            logger_.logMsg(clntSockInfo.str() + reason, Logger::lmInfo);
            sIOD.closeFd = true;
            sIOD.closeMsg = reason;
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

    SocketIOData &sIOD = getSockIOData(fd);
    size_t        lfcrPos = bufferLengthAndSanityCheck_(sIOD, msgTooLong);
    std::string   cmdLine;
    if (lfcrPos != std::string::npos) {
        cmdLine = sIOD.readBuffer.substr(0, lfcrPos);
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

    size_t eolMaxIdxPos = MAXMSGLEN - 2;
    if ((eol > 0) && (eol != std::string::npos) &&
        (rdBuffer.at(eol - 1) == '\r'))
        eolMaxIdxPos = MAXMSGLEN - 1;

    msgTooLong = false;
    if (!sIOD.discardMode) {
        if ((rdBuffer.length() < MAXMSGLEN) ||
            (eol != std::string::npos && (eol <= eolMaxIdxPos))) {
            return eol;
        } else {
            sIOD.discardMode = true;
            msgTooLong = true;
        }
    }
    // discard mode
    if (++sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
        sIOD.closeFd = true;
        sIOD.closeMsg = "SOCKETERRORLIMIT exceeded (discard mode)";
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
    SocketIOData &sIOD = getSockIOData(fd);
    if (sIOD.closeFd)
        return;

    if (msg.length() >
        sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length()) {
        sIOD.closeFd = true;
        sIOD.closeMsg = "SOCKETERRORLIMIT exceeded (write buffer overflow)";
        return;
    }
    sIOD.writeBuffer += msg;
    ssize_t bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                             sIOD.writeBuffer.length(), MSG_NOSIGNAL);
    if (bytesSent <= 0) {
        ++sIOD.sendErrorCount;
        if (sIOD.sendErrorCount >= SOCKETERRORLIMIT) {
            sIOD.closeFd = true;
            sIOD.closeMsg = "SOCKETERRORLIMIT exceeded (send)";
        }
        return;
    }

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
}

bool Socket::socketCloseMarker(int const fd) {
    return getSockIOData(fd).closeFd;
}

void Socket::setSocketCloseState(int const fd, bool doClose,
                                 std::string const &closeMsg) {
    SocketIOData &sIOD = getSockIOData(fd);
    sIOD.closeFd = doClose;
    sIOD.closeMsg = closeMsg;
}

/*
Performance note: 2 searches of sockIODataStore.
Wrap in try catch to remove this redundancy
or make socketClosed return a pointer instead
*/
void Socket::closeSocket(int const fd) {
    try {
        SocketIOData      &sIOD = getSockIOData(fd);
        int const          addrFam = srvHelp::sockAddrFamily(fd);
        std::ostringstream oss;
        if (sIOD.isSrvSocket)
            oss << "Closing server socket ("
                << srvHelp::getAdressFamilyStr(addrFam) << ")";
        else
            oss << "Closing client sockFd:" << fd;
        logger_.logMsg(oss.str(), Logger::lmInfo);
        sockIODataStore_.erase(fd);
    } catch (std::exception const &e) {
        std::ostringstream oss;
        oss << "Error while closing socket Fd:" << fd
            << ": Cannot get socketIOData (fd will be closed anyways)";
        logger_.logMsg(oss.str(), Logger::lmWarn);
    }
    close(fd);
}

Socket::SocketIOData &Socket::getSockIOData(int const fd) {
    std::map<int, SocketIOData>::iterator it = sockIODataStore_.find(fd);
    if (it == sockIODataStore_.end()) {
        std::ostringstream oss;
        oss << "Couldn't retrieve socketIOData for fd:" << fd;
        throw std::runtime_error(oss.str());
    }
    return it->second;
}

bool Socket::removeSockIOData(int const fd) throw() {
	std::map<int, SocketIOData>::iterator it = sockIODataStore_.find(fd);
	if (it == sockIODataStore_.end())
		return true;
    return it->second.closeFd;
}

// bool Socket::isServerSocket(int const fd) {
//     SocketIOData const &sIOD = getSockIOData(fd);
//     return sIOD.isSrvSocket;
// }
