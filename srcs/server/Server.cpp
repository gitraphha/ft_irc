#include "Server.hpp"

#include "Errors.hpp"
#include "ServerHelper.hpp"
#include "SignalHandler.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <sys/poll.h>

/*
    Server Constructor
    * set port and password from command line args
    * init the logger prefix (server name)
*/
Server::Server(int const port, std::string const &password)
    : port_(port), password_(password), serverName_(SERVER_NAME),
      numServerSockets_(0), logger_(serverName_ + ": ") {}

Server::~Server() {
    while (!pollFds_.empty())
        closeSocket_(pollFds_.back().fd);
}

/*
    sendMsg
    * send string msg to a network socket
    * uses i/o buffers and error counters in SocketIOData
	* closes the socket (== disconnects the client) in case of too many errors
*/
bool Server::sendMsg(int const fd, std::string const &msg) {
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
        log_(oss.str(), Logger::lmError);
    } else {
        if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
            oss << " partly (" << bytesSent << "/" << sIOD.writeBuffer.length()
                << ") " << sIOD.writeBuffer.substr(0, bytesSent);
        else
            oss << " successfully";
        oss << " sent to FD: " << fd;
        log_(oss.str(), Logger::lmInfo);
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

void Server::setupServerSockets_() {
    int              domArr[] = {AF_INET, AF_INET6};
    std::vector<int> domains(domArr,  // NOLINT
                             domArr + // NOLINT
                                 (sizeof(domArr) / sizeof(domArr[0])));
    for (size_t idx = 0; idx < domains.size(); ++idx) {
        try {
            createServerSocket_(domains[idx]);
        } catch (std::exception const &e) {
            log_(e.what(), Logger::lmExcept);
        }
    }
    if (numServerSockets_ == 0)
        throw std::runtime_error("No server socket could be setup correctly.");
}

/*
    createServerSocket_
    * Setup a listening network socket for the address family/domain specified
	* only supports IPv4 and IPv6
    * for IPv6, uses and IPv6only socket, so the IPv4 is kept separate
	  (not all systems support dual stack handling of IPv4 through IPv6)
*/
int Server::createServerSocket_(int const domain) {
    if (domain != AF_INET && domain != AF_INET6)
        throw std::invalid_argument(
            "Internal Error while opening server socket.");

    int socketFd = socket(domain, SOCK_STREAM, 0);
    if (socketFd < 0)
        throw std::runtime_error("Error while opening server socket for " +
                                 srvHelp::getAdressFamilyStr(domain) + " !");

    int optval = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &optval,
                   sizeof(optval)) != 0)
        throw std::runtime_error("Error while setting socket options for " +
                                 srvHelp::getAdressFamilyStr(domain) + " !");

    if (domain == AF_INET6) {
        int v6only = 1;
        setsockopt(socketFd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only,
                   sizeof(v6only));
    }

    srvHelp::setSocketNonBlock(socketFd);

    srvHelp::bindServerSocket(socketFd, domain, port_);

    if (listen(socketFd, MAX_CONNECTIONS) < 0)
        throw std::runtime_error("Error while listening on socket for " +
                                 srvHelp::getAdressFamilyStr(domain) + " !");

    storePollFd_(socketFd);
    ++numServerSockets_;

    return socketFd;
}

void Server::processServerSocketData_() {
    for (size_t idx = 0;
         !SignalHandler::stopLoopSignalReceived() && idx < numServerSockets_;
         ++idx) {
        struct pollfd const &serverPollFds = pollFds_[idx];
        if ((serverPollFds.revents & POLLERR))
            throw std::runtime_error("Server socket error.");
        if (serverPollFds.revents & POLLIN)
            acceptClient_(serverPollFds.fd);
    }
}

bool Server::checkSocket_(struct pollfd &pfd, SocketIOData &sIOD) {
    if ((pfd.revents & POLLERR)) {
        if (sIOD.recvErrorCount >= SOCKETERRORLIMIT)
            closeSocket_(pfd.fd);
        else
            ++sIOD.recvErrorCount;
        return false;
    }
    return true;
}

void Server::setClientInfo_(int fd, Client const **clntPtr, std::string &nick,
                            bool &isReg) {
    try {
        *clntPtr = clients_.at(fd);
        if (*clntPtr && (*clntPtr)->isRegistered()) {
            isReg = true;
            nick = (*clntPtr)->getNick();
        }
    } catch (std::exception const &e) {
        log_(e.what(), Logger::lmExcept);
    }
}

bool Server::clientDataSanityCheck_(SocketIOData &sIOD, int fd, /* 
                                    Client const *const client, */
                                    std::string const &nickName,
                                    bool const         isRegistered) {
    std::string &rdBuffer = sIOD.readBuffer;
    size_t       lfcrPos = rdBuffer.find("\r\n");
    if ((rdBuffer.length() >= MAXMSGLEN) &&
        (lfcrPos == std::string::npos || lfcrPos > MAXMSGLEN - 2)) {
        sIOD.discardMode = true;
        ++sIOD.recvErrorCount;
        sendMsg(fd, Reply::build(serverName(), nickName,
                                 Reply::reply(Reply::ERR_INPUTTOOLONG, "")));
    }
    if (sIOD.discardMode) {
        if (!isRegistered || sIOD.recvErrorCount >= SOCKETERRORLIMIT) {
            closeSocket_(fd);
            return false;
        }
        if (lfcrPos != std::string::npos) {
            rdBuffer.erase(0, lfcrPos + 2);
            sIOD.discardMode = false;
            lfcrPos = rdBuffer.find("\r\n");
        } else if ('\r' == *rdBuffer.rbegin())
            rdBuffer = "\r";
        else
            rdBuffer.clear();
    }
    if (lfcrPos == std::string::npos || sIOD.discardMode) {
        return false;
    }
    return true;
}

bool Server::processClientSocketData_(struct pollfd &pfd, SocketIOData &sIOD) {
    Client const *client = 0;
    std::string   nickName = "*";
    bool          clntIsReg = false;
    setClientInfo_(pfd.fd, &client, nickName, clntIsReg);

    if (!clientDataSanityCheck_(sIOD, pfd.fd, nickName, clntIsReg))
        return false;

    sIOD.recvErrorCount = 0;
    size_t      lfcrPos = sIOD.readBuffer.find("\r\n");
    std::string cmdLine = sIOD.readBuffer.substr(0, lfcrPos);
    sIOD.readBuffer = sIOD.readBuffer.substr(lfcrPos + 2);

#if NETCATDBG
    {
        // size_t const       clientNumber = idx + 1 - numServerSockets_;
        std::ostringstream oss;
        oss << "You are client '" << nickName << "' (fd " << pfd.fd << "): \n"
            << "Srv received: " << std::string(cmdLine) << std::endl;
        sendMsg(pfd.fd, oss.str());
    }
#endif

    try {
        cmds_.run(*this, *clients_.at(pfd.fd), cmdLine);
    } catch (Error const &e) {
        if (e.hasReply()) {
            sendMsg(pfd.fd, Reply::build(serverName(), nickName, e.reply()));
        } else {
            std::string tmp("Error while trying to run cmd:\n");
            tmp.append(cmdLine + "\n" + e.what());
            log_(tmp, Logger::lmExcept);
        }
    }
    return true;
}

void Server::processClientSockets_() {
    for (size_t idx = numServerSockets_;
         !SignalHandler::stopLoopSignalReceived() && idx < pollFds_.size();
         ++idx) {
        pollfd       &pfd = pollFds_.at(idx);
        SocketIOData &sIOD = sockIODataStore_.at(pfd.fd);

        if (!checkSocket_(pfd, sIOD))
            continue;
        if (pfd.revents & POLLIN) {
            char recvBuffer[RECVBUFFLEN];
            sIOD.bytesRead = recv(pfd.fd, &recvBuffer[0], RECVBUFFLEN, 0);

            if (sIOD.bytesRead > 0) {
                sIOD.readBuffer += std::string(&recvBuffer[0], sIOD.bytesRead);
                processClientSocketData_(pfd, sIOD);
            } else if (sIOD.bytesRead <= 0) {
                closeSocket_(pfd.fd);
            }
        }
    }
}

/*
	tbd
*/
void Server::acceptClient_(int const serverFd) {
    socketAddrX sockAddrIn46 = {};
    socklen_t   sockLen = 0;

    srvHelp::setSocketAddr(serverFd, 0, sockAddrIn46, sockLen);

    int const clientFd =
        accept(serverFd,
               reinterpret_cast<struct sockaddr *>(&sockAddrIn46), // NOLINT
               &sockLen);
    if (clientFd == -1) {
        log_("Could not accept client:\n" +
                 srvHelp::printAddrfamilyAddrPort(sockAddrIn46),
             Logger::lmWarn);
        return;
    }

    srvHelp::setSocketNonBlock(clientFd);

    storePollFd_(clientFd);

    std::cout << "Client connected:\n"
              << " fd: " << clientFd << "\n"
              << srvHelp::printAddrfamilyAddrPort(sockAddrIn46) << std::endl;
}

void Server::storePollFd_(int const socketFd) {
    pollfd pfd = {};
    pfd.fd = socketFd;
    pfd.events = POLLIN; // Monitor for incoming connections

    pollFds_.push_back(pfd);
    sockIODataStore_.insert(
        std::make_pair<int, SocketIOData>(socketFd, SocketIOData()));
}

/*
	tbd
*/
void Server::closeSocket_(int const fd) {
    size_t idx = 0;
    for (idx = 0; idx < pollFds_.size(); ++idx)
        if (fd == pollFds_.at(idx).fd)
            break;
    if (idx == pollFds_.size())
        return;

    std::ostringstream oss;
    if (idx < numServerSockets_) {
        if (pollFds_.size() != idx + 1) {
            log_("Internal error: Close client sockets before "
                 "closing server!",
                 Logger::lmError);
            return;
        }
        oss << "Closing server socket ("
            << srvHelp::getAdressFamilyStr(srvHelp::sockAddrFamily(fd)) << ")";
    } else {
        oss << "Closing client #" << idx + 1 - numServerSockets_ << ", sockFd "
            << fd << "; (" << pollFds_.size() - 1 - numServerSockets_
            << " clients remaining)";
    }
    log_(oss.str(), Logger::lmInfo);

    close(fd);
    std::vector<std::string>::difference_type dist =
        static_cast<std::vector<std::string>::difference_type>(idx);
    pollFds_.erase(pollFds_.begin() + dist);
    sockIODataStore_.erase(fd);
}

void Server::log_(std::string const &msg, Logger::logMode const lm) {
    logger_.logMsg(msg, lm);
}

/*
	Main server loop

	* sets up its listening socket(s)
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
	* on server stop signal, disconnects all clients and closes
		the server socket(s)
*/
void Server::run() {
    setupServerSockets_();

    while (!SignalHandler::stopLoopSignalReceived()) {
        int pollResult = poll(pollFds_.data(), pollFds_.size(), -1);
        if (pollResult == -1) {
            log_("Fatal error: poll failed.", Logger::lmError);
            break;
        }

        processServerSocketData_();
        processClientSockets_();
    }
}
