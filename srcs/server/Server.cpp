#include "Server.hpp"

#include "Errors.hpp"
#include "ServerHelper.hpp"
#include "SignalHandler.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/poll.h>

/*
    Server Constructor
    * set port and password from command line args
    * init the logger prefix (server name)
*/
Server::Server(int const port, std::string const &password)
    : serverName_(SERVER_NAME), port_(port), password_(password),
      logger_(serverName_ + ": "), sockets_(logger_), numServerSockets_(0) {}

/*
	Cleanup
	* going backwards through pollFds to ensure server sockets are closed last
 */
Server::~Server() {
    while (!pollFds_.empty()) {
        int const   fd = pollFds_.back().fd;
        std::string nickName = clients_.at(fd)->getNick();
        sockets_.sendMsg(
            fd, Reply::build(
                    serverName_, nickName,
                    Reply::Message(-1, "ERROR", "Server shutdown - goodbye.")));
        sockets_.closeSocket(fd);
        pollFds_.erase(--pollFds_.end());
        clients_.remove(fd);
    }
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

    storePollFd_(socketFd, true);
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

void Server::setClientInfo_(int fd, Client **clntPtr, std::string &nick,
                            bool &isReg) {
    try {
        *clntPtr = &clients_.at(fd);
        if (*clntPtr && (*clntPtr)->isRegistered()) {
            isReg = true;
            nick = (*clntPtr)->getNick();
        }
    } catch (std::exception const &e) {
        log_(e.what(), Logger::lmWarn);
    }
}

void Server::processClientSockets_() {
    for (size_t idx = numServerSockets_;
         !SignalHandler::stopLoopSignalReceived() && idx < pollFds_.size();
         ++idx) {
        pollfd &pfd = pollFds_.at(idx);

        if (sockets_.checkSocket(pfd) && (pfd.revents & POLLIN)) {

            // to be replaced by a request to some sort of manager
            Client     *client = 0;
            std::string nickName = "*";
            bool        clntIsReg = false;
            setClientInfo_(pfd.fd, &client, nickName, clntIsReg);
            // end: to be replaced by a request to some sort of manager

            std::string cmdLine;
            bool        buffHasMoreCmds = false;
            do {
                try {
                    cmdLine = sockets_.getCmdLine(pfd.fd, buffHasMoreCmds);
                    if (!cmdLine.empty())
                        cmds_.run(*this, *client, cmdLine);
                } catch (Error const &e) {
                    if (e.hasReply()) {
                        sockets_.sendMsg(
                            pfd.fd,
                            Reply::build(serverName_, nickName, e.reply()));
                    } else {
                        std::string tmp(
                            "Error while trying to get/execute cmd:\n");
                        tmp.append("'" + cmdLine + "'\n" + e.what());
                        log_(tmp, Logger::lmExcept);
                    }
                }
            } while (buffHasMoreCmds);
        }

        if (sockets_.socketClosed(pfd.fd)) {
            removePollFd_(pfd.fd);
            clients_.remove(pfd.fd);
        }
    }
}

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
                 srvHelp::composeAddrfamAddrPortStr(sockAddrIn46),
             Logger::lmWarn);
        return;
    }

    srvHelp::setSocketNonBlock(clientFd);

    storePollFd_(clientFd, false);
    clients_.add(new Client(), clientFd);

    std::cout << "Client connected:\n"
              << " fd: " << clientFd << "\n"
              << srvHelp::composeAddrfamAddrPortStr(sockAddrIn46) << std::endl;
}

void Server::storePollFd_(int const socketFd, bool srvSocket) {
    pollfd pfd = {};
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    pollFds_.push_back(pfd);
    sockets_.addSocket(socketFd, srvSocket);
}

void Server::removePollFd_(int const idx) {
    std::vector<struct pollfd>::difference_type dist =
        static_cast<std::vector<struct pollfd>::difference_type>(idx);
    pollFds_.erase(pollFds_.begin() + dist);
}

void Server::log_(std::string const &msg, Logger::logMode const lm) {
    logger_.logMsg(msg, lm);
}

const std::string &Server::getServerName() const {
    return serverName_;
}

ChannelManager &Server::getChannelManager() {
    return channels_;
}

ClientManager &Server::getClientManager() {
    return clients_;
}

/*
	Main server loop

	* sets up its listening socket(s)
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
*/
void Server::run() {
    setupServerSockets_();

    while (!SignalHandler::stopLoopSignalReceived()) {
        int pollResult = poll(pollFds_.data(), pollFds_.size(), -1);
        if (pollResult == -1) {
            if (SignalHandler::stopLoopSignalReceived())
                break;
            log_("Fatal error: poll failed.", Logger::lmError);
            break;
        }

        processServerSocketData_();
        processClientSockets_();
    }
}
