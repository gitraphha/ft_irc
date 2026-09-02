#include "Socket.hpp"

#include "Errors.hpp"
#include "Reply.hpp"
#include "ServerHelper.hpp"

#include <exception>
#include <sstream>
#include <sys/poll.h>
#include <unistd.h>

void Socket::addSocket(int const fd, bool srvSocket) {
    sockIODataStore_.insert(std::make_pair(fd, SocketIOData(srvSocket)));
}

bool Socket::checkSocket(struct pollfd &pfd) {
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
        if (sIOD.bytesRead == 0 || sIOD.recvErrorCount >= SOCKETERRORLIMIT)
            closeSocket(fd);
        return false;
    }
    sIOD.readBuffer += std::string(&recvBuffer[0], sIOD.bytesRead);
    return true;
}

std::string Socket::getCmdLine(int const fd, bool &buffHasMoreCmds) {
    Socket::SocketIOData &sIOD = sockIODataStore_.at(fd);
    std::string           cmdLine;

    if (!buffHasMoreCmds)
        updateBuffer_(fd);

    size_t lfcrPos = bufferLengthAndSanityCheck_(fd);
    if (lfcrPos != std::string::npos) {
#if NETCATDBG
        {
            std::ostringstream oss;
            oss << "You are client fd " << fd << ": \n"
                << "Srv received: " << std::string(sIOD.readBuffer)
                << std::endl;
            sendMsg(fd, oss.str());
        }
#endif
        sIOD.recvErrorCount = 0;
        cmdLine = sIOD.readBuffer.substr(0, lfcrPos);
        sIOD.readBuffer = sIOD.readBuffer.substr(lfcrPos + 2);
        buffHasMoreCmds = (sIOD.readBuffer.find("\r\n") != std::string::npos);
    }
    return cmdLine;
}

size_t Socket::bufferLengthAndSanityCheck_(
    int const fd /* , bool const isRegistered */) {
    Socket::SocketIOData &sIOD = sockIODataStore_.at(fd);
    std::string          &rdBuffer = sIOD.readBuffer;
    size_t                lfcrPos = rdBuffer.find("\r\n");
    bool                  msgTooLong = false;

    if ((rdBuffer.length() >= MAXMSGLEN) &&
        (lfcrPos == std::string::npos || lfcrPos > MAXMSGLEN - 2)) {
        sIOD.discardMode = true;
        ++sIOD.recvErrorCount;
        msgTooLong = true;
    }
    if (sIOD.discardMode) {
        if (/* !isRegistered || */ sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            closeSocket(fd);
            return std::string::npos;
        }
        if (lfcrPos != std::string::npos) {
            rdBuffer.erase(0, lfcrPos + 2);
            sIOD.discardMode = false;
            lfcrPos = rdBuffer.find("\r\n");
        } else /* lfcrPos == std::string::npos */ {
            if ('\r' == *rdBuffer.rbegin())
                rdBuffer = "\r";
            else
                rdBuffer.clear();
        }
    }
    if (msgTooLong)
        throw Error(Reply::reply(Reply::ERR_INPUTTOOLONG, ""));

    return lfcrPos;
}

/*
    sendMsg
    * send string msg to a network socket
    * uses i/o buffers and error counters in SocketIOData
	* closes the socket (== disconnects the client) in case of too many errors
*/
bool Socket::sendMsg(int const fd, std::string const &msg) {
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

#if NETCATDBG
    std::ostringstream oss;
    oss << "SendMsg event:\n"
        << " WriteBuffer: '" << sIOD.writeBuffer << "':\n";
    if (!success) {
        oss << " send to FD " << fd << " failed.";
        logger_.logMsg(oss.str(), Logger::lmError);
    } else {
        if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
            oss << " partly (" << bytesSent << "/" << sIOD.writeBuffer.length()
                << ") " << sIOD.writeBuffer.substr(0, bytesSent);
        else
            oss << " successfully";
        oss << " sent to FD: " << fd;
        logger_.logMsg(oss.str(), Logger::lmInfo);
    }
#endif

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
        oss << "Closing client with sockFd " << fd;
    }
    logger_.logMsg(oss.str(), Logger::lmInfo);

    close(fd);
    sockIODataStore_.erase(fd);
}
