#include "Server.hpp"

#include "Client.hpp"        // for Client
#include "Errors.hpp"        // for Error
#include "Reply.hpp"         // for build
#include "ServerHelper.hpp"  // for getAdressFamilyStr, composeAddrfamAddrP...
#include "SignalHandler.hpp" // for SignalHandler

#include <exception>    // for exception
#include <iostream>     // for basic_ostream, operator<<, basic_ostrea...
#include <netinet/in.h> // for IPPROTO_IPV6, IPV6_V6ONLY
#include <set>
#include <sstream>      // for basic_ostringstream
#include <stdexcept>    // for runtime_error, invalid_argument
#include <string>       // for allocator, char_traits, basic_string
#include <sys/socket.h> // for setsockopt, AF_INET6, AF_INET, accept

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
/*
	Cleanup
	* going backwards through pollFds to ensure server sockets are closed last
 */
Server::~Server() {
    while (!pollFds_.empty()) {
        int const fd = pollFds_.back().fd;
        if (!sockets_.isServerSocket(fd)) {
            // Update after Call client member function for shutdown notice message!
            // after PR feat/cmds2: implement QUIT, PING, PONG and NOTICE - #38#
//#define REMOVE_ME
#ifndef REMOVE_ME
            std::string nickName;
            try {
                nickName = clients_.at(fd).getNick();
            } catch (std::exception const &e) {
                logger_.logMsg("\n Cannot get client object:"
                               "\n starting from a second client, the object "
                               "isn't correctly created"
                               "\n in function bool ClientManager::add(Client "
                               "&client, int fd)"
                               "\n Exception: " +
                                   std::string(e.what()),
                               Logger::lmError);
            }
            std::ostringstream oss;
            oss << "ERROR :" << serverName_ << " " << nickName
                << " :Server shutdown - goodbye.";
            sockets_.sendMsg(fd, oss.str());
#else
            Client &client = clients_.at(fd);
            client.sendClosingMsg("Server shutdown - goodbye");
#endif
            clients_.remove(fd, channels_);
        }
        sockets_.closeSocket(fd);
        this->removePollFd_(fd);
        // pollFds_.erase(--pollFds_.end());
    }
}

void Server::setupServerSockets_() {
    static int const              domArr[] = {AF_INET, AF_INET6};
    static std::vector<int> const domains(
        // NOLINTNEXTLINE
        domArr, domArr + (sizeof(domArr) / sizeof(domArr[0])));
    for (size_t idx = 0; idx < domains.size(); ++idx) {
        try {
            createServerSocket_(domains.at(idx));
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
        struct pollfd const &serverPollFds = pollFds_.at(idx);
        if ((serverPollFds.revents & POLLERR))
            throw std::runtime_error("Server socket error.");
        if (serverPollFds.revents & POLLIN)
            acceptClient_(serverPollFds.fd);
    }

    // DEBUG check if cleanup works correctly
    // static size_t crash = 0;
    // if (++crash > 5)
    //     throw std::runtime_error("DBG: Server socket error.");
}

void Server::processClientSockets_() {
    for (size_t idx = numServerSockets_;
         !SignalHandler::stopLoopSignalReceived() && idx < pollFds_.size();
         ++idx) {
        pollfd &pfd = pollFds_.at(idx);

        if (sockets_.checkSocket4Recv(pfd) && (pfd.revents & POLLIN)) {
            std::string cmdLine;
            bool        buffHasMoreCmds = false;
            do {
                // try {
                cmdLine =
                    sockets_.getCRLFLineFromBuffer(pfd.fd, buffHasMoreCmds);
                if (!cmdLine.empty()) {
                    // returns true if connection is to be closed
                    // only happens after a QUIT cmd or maybe after too many ERR_INPUTTOOLONG
                    if (cmds_.run(
                            *this,
                            /* should soon be: pfd.fd */ clients_.at(pfd.fd),
                            cmdLine)) {
                        sockets_.closeSocket(pfd.fd);
                        break;
                    }
                }
                // } catch (Error const &e) {
                //     if (e.hasReply()) {
                //         sockets_.sendMsg(
                //             pfd.fd,
                //             Reply::build(serverName_, nickName, e.reply()));
                //     } else {
                //         std::string tmp(
                //             "Error while trying to get/execute cmd:\n");
                //         tmp.append("'" + cmdLine + "'\n" + e.what());
                //         log_(tmp, Logger::lmExcept);
                //     }
                // }
            } while (buffHasMoreCmds);
        }
    }
    removeClosedSockets_();
}

void Server::removeClosedSockets_() {
    for (size_t idx = numServerSockets_; idx < pollFds_.size(); ++idx) {
        pollfd &pfd = pollFds_.at(idx);
        if (sockets_.socketClosed(pfd.fd)) {
            clients_.remove(pfd.fd, channels_);
            removePollFd_(pfd.fd);
            --idx;
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
    clients_.add(clientFd, serverName_);

    std::ostringstream oss;
    oss << "Client connected:"
        << " fd:" << clientFd
        << srvHelp::composeAddrfamAddrPortStr(sockAddrIn46) << std::endl;
    log_(oss.str(), Logger::lmInfo);
}

void Server::storePollFd_(int const socketFd, bool srvSocket) {
    pollfd pfd = {};
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    pollFds_.push_back(pfd);
    sockets_.addSocket(socketFd, srvSocket);
}

void Server::removePollFd_(int const fd) {
    std::vector<pollfd>::iterator it =
        std::find_if(pollFds_.begin(), pollFds_.end(), srvHelp::pfdHasFd(fd));
    if (it != pollFds_.end())
        pollFds_.erase(it);
}

void Server::log_(std::string const &msg, Logger::logMode const lm) {
    logger_.logMsg(msg, lm);
}

const std::string &Server::getServerName() const {
    return serverName_;
}

const std::string &Server::getServerVersion() const {
    return serverVersion_;
}

const std::string &Server::getCreatedAt() const {
    return createdAt_;
}

const std::string &Server::getPassword() const {
    return password_;
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

void Server::sendMsg(int const fd, std::string const &msg) {
    if (!sockets_.sendMsg(fd, msg)) {
        // will be done at the end of a poll loop

        // clients_.remove(fd, channels_);
        // removePollFd_(fd);
    }
}