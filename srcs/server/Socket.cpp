#include "Socket.hpp"

#include "Reply.hpp"
#include "ServerHelper.hpp"

#include <exception>
#include <sstream>
#include <sys/poll.h>
#include <unistd.h>

void Socket::addSocket(int const fd) {
    sockIODataStore_.insert(
        std::make_pair<int, Socket::SocketIOData>(fd, Socket::SocketIOData()));
}

bool Socket::checkSocket_(struct pollfd &pfd) {
    SocketIOData &sIOD = sockIODataStore_.at(pfd.fd);
    if ((pfd.revents & POLLERR)) {
        if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
            closeSocket_(pfd.fd);
        else
            ++sIOD.recvErrorCount;
        return false;
    }
    return true;
}

size_t Socket::clientDataSanityCheck_(Socket::SocketIOData &sIOD, int fd,
                                      std::string const &serverName,
                                      std::string const &nickName,
                                      bool const         isRegistered) {
    std::string &rdBuffer = sIOD.readBuffer;
    size_t       lfcrPos = rdBuffer.find("\r\n");
    if ((rdBuffer.length() >= MAXMSGLEN) &&
        (lfcrPos == std::string::npos || lfcrPos > MAXMSGLEN - 2)) {
        sIOD.discardMode = true;
        ++sIOD.recvErrorCount;
        sendMsg(fd, Reply::build(serverName, nickName,
                                 Reply::reply(Reply::ERR_INPUTTOOLONG, "")));
    }
    if (sIOD.discardMode) {
        if (!isRegistered || sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            closeSocket_(fd);
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
        closeSocket_(fd);
        success = false;
    }

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

    if (!success)
        return false;

    sIOD.sendErrorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
    return true;
}

Socket::SocketIOData &Socket::getSocketIOData_(int const fd) {
    std::map<int, SocketIOData>::iterator sIODit = sockIODataStore_.find(fd);
    if (sIODit == sockIODataStore_.end()) {
        std::ostringstream oss;
        oss << "No SocketIOData for fd " << fd;
        throw std::out_of_range(oss.str());
    }
    return sIODit->second;
}

void Socket::closeSocket_(int const fd) {
    SocketIOData &sIOD = getSocketIOData_(fd);

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
