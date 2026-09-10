#include "Server.hpp"

#include "Client.hpp"        // for Client
#include "Errors.hpp"        // for Error
#include "Reply.hpp"         // for build
#include "ServerHelper.hpp"  // for getAdressFamilyStr, composeAddrfamAddrP...
#include "SignalHandler.hpp" // for SignalHandler

#include <exception>    // for exception
#include <iostream>     // for basic_ostream, operator<<, basic_ostrea...
#include <netinet/in.h> // for IPPROTO_IPV6, IPV6_V6ONLY
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
    : serverName_(SERVER_NAME), serverVersion_(SERVER_VERSION), port_(port),
      password_(password), logger_(serverName_ + ": "), sockets_(logger_),
      numServerSockets_(0) {
    std::time_t now = std::time(0);
    char        buf[64];
    std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y",
                  std::localtime(&now));
    createdAt_ = buf;
    cmds_.registerCommands();
}

/*
	Cleanup
	* going backwards through pollFds to ensure server sockets are closed last
 */
Server::~Server() {
    while (!pollFds_.empty()) {
        int const fd = pollFds_.back().fd;
        if (!sockets_.isServerSocket(fd)) {
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
            clients_.remove(fd, channels_);
        }
        sockets_.closeSocket(fd);
        pollFds_.erase(--pollFds_.end());
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
                    cmdLine =
                        sockets_.getCRLFLineFromBuffer(pfd.fd, buffHasMoreCmds);
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

            if (client && !client->getSendBuffer().empty()) {
                const std::string &out = client->getSendBuffer();
                sockets_.sendMsg(pfd.fd, out);
                client->consumeSendBuffer(out.size());
            }
        }

        if (sockets_.socketClosed(pfd.fd)) {
            clients_.remove(pfd.fd, channels_);
            removePollFd_(idx);
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
    clients_.add(clientFd, new Client(clientFd));

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
