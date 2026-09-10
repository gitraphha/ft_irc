#include "Socket.hpp"

#include "Errors.hpp"       // for Error
#include "Reply.hpp"        // for reply, Code
#include "ServerHelper.hpp" // for getAdressFamilyStr, sockAddrFamily

#include <poll.h>       // for pollfd, POLLERR
#include <sstream>      // for basic_ostringstream, basic_ostream, oper...
#include <sys/socket.h> // for recv, send, MSG_NOSIGNAL
#include <unistd.h>     // for close

void Socket::addSocket(int const fd, bool srvSocket) {
    sockIODataStore_.insert(std::make_pair(fd, SocketIOData(srvSocket)));
}

bool Socket::checkSocket4Recv(struct pollfd &pfd) {
    if (socketClosed(pfd.fd))
        return false;

bool Socket::checkSocket4Recv(struct pollfd &pfd) {
    if (socketClosed(pfd.fd))
        return false;

    SocketIOData &sIOD = sockIODataStore_.at(pfd.fd);
    if ((pfd.revents & POLLERR)) {
        if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
            closeSocket(pfd.fd);
        else
            ++sIOD.recvErrorCount;
        return false;
    }
    return true;
}

bool Socket::updateBuffer_(int const fd) {
    Socket::SocketIOData &sIOD = sockIODataStore_.at(fd);
    char                  recvBuffer[RECVBUFFLEN];
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

std::string Socket::getCRLFLineFromBuffer(int const fd, bool &buffHasMoreCmds) {
    if (!buffHasMoreCmds) {
        if (!updateBuffer_(fd))
            return "";
    }

    bool   msgTooLong = false;
    size_t lfcrPos = bufferLengthAndSanityCheck_(fd, msgTooLong);
    if (msgTooLong)
        return std::string(MAXMSGLEN + 1, '\0');
    if (lfcrPos == std::string::npos)
        return "";

    Socket::SocketIOData &sIOD = sockIODataStore_.at(fd);
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

    if ((!sIOD.discardMode) && (rdBuffer.length() >= MAXMSGLEN) &&
        (lfcrPos == std::string::npos || lfcrPos > MAXMSGLEN - 2)) {
        sIOD.discardMode = true;
        msgTooLong = true;
    }
    if (sIOD.discardMode) {
        if (++sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            closeSocket(fd);
            return std::string::npos;
        }
        if (lfcrPos != std::string::npos) {
            rdBuffer.erase(0, lfcrPos + 2);
            sIOD.discardMode = false;
            sIOD.recvErrorCount = 0;
            lfcrPos = rdBuffer.find("\r\n");
        } else /* lfcrPos == std::string::npos */ {
            if ('\r' == *rdBuffer.rbegin())
                rdBuffer = "\r";
            else
                rdBuffer.clear();
        }
    }
    return lfcrPos;
}

/*
    sendMsg
    * send string msg to a network socket
    * uses i/o buffers and error counters in SocketIOData
	* closes the socket (== disconnects the client) in case of too many errors
*/
bool Socket::sendMsg(int const fd, std::string const &msg) {
    if (socketClosed(fd))
        return false;

    SocketIOData &sIOD = sockIODataStore_.at(fd);
    ssize_t       bytesSent = -1;
    bool          success = true;

    if (msg.length() > sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length())
        sIOD.sendErrorCount = SOCKETERRORLIMIT;
    else {
        sIOD.writeBuffer += msg;
        bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                         sIOD.writeBuffer.length(), MSG_NOSIGNAL);
        if (bytesSent <= 0)
            ++sIOD.sendErrorCount;
    }
    if (sIOD.sendErrorCount >= SOCKETERRORLIMIT) {
        closeSocket(fd);
        success = false;
    }

    {
        std::ostringstream oss;
        oss << "SendMsg event:\n"
            << " WriteBuffer: '" << sIOD.writeBuffer << "':\n";
        if (!success) {
            oss << " send to FD " << fd << " failed.";
            logger_.logMsg(oss.str(), Logger::lmError);
        } else if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length()) {
            oss << " partly (" << bytesSent << "/" << sIOD.writeBuffer.length()
                << ") " << sIOD.writeBuffer.substr(0, bytesSent)
                << " sent to FD: " << fd;
            logger_.logMsg(oss.str(), Logger::lmInfo);
        }
    }

    if (!success)
        return false;

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
    return true;
}

void Socket::closeSocket(int const fd) {
    SocketIOData &sIOD = sockIODataStore_.at(fd);

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
