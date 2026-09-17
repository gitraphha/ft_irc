#include "Server.hpp"

#include "Client.hpp"        // for Client
#include "Errors.hpp"        // for Error
#include "Reply.hpp"         // for build
#include "ServerHelper.hpp"  // for getAdressFamilyStr, composeAddrfamAddrP...
#include "SignalHandler.hpp" // for SignalHandler

#include <algorithm>    // for find_if
#include <exception>    // for exception
#include <netinet/in.h> // for IPPROTO_IPV6, IPV6_V6ONLY
#include <sstream>      // for basic_ostringstream, basic_ostream, ope...
#include <stdexcept>    // for runtime_error, invalid_argument
#include <stdint.h>     // for uint16_t
#include <string>       // for allocator, basic_string, char_traits
#include <sys/socket.h> // for setsockopt, AF_INET6, AF_INET, accept

/*
    Server Constructor
    * set port and password from command line args
    * init the logger prefix (server name)
*/
Server::Server(int const port, std::string const &password)
    : serverName_(SERVER_NAME), serverVersion_(SERVER_VERSION),
      createdAt_(Logger::nowStr()), port_(port), password_(password),
      logger_(serverName_ + ": "), sockets_(logger_), numServerSockets_(0) {}

/*
	Cleanup
	* going backwards through pollFds to ensure server sockets are closed last
 */
Server::~Server() {
    while (!pollFds_.empty()) {
        int const fd = pollFds_.back().fd;
        removeClientSocketPfd_(fd, "Server shutdown - goodbye");
    }
}

bool Server::setupServerSockets_() {
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
    if (numServerSockets_ == 0) {
        log_("No server socket could be setup correctly.", Logger::lmError);
        return false;
    }
    return true;
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

    if (listen(socketFd, MAX_CONNECTIONQUEUE) < 0)
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

void Server::processClientSockets_() {
    for (size_t idx = numServerSockets_;
         !SignalHandler::stopLoopSignalReceived() && idx < pollFds_.size();
         ++idx) {
        pollfd &pfd = pollFds_.at(idx);

        if (!(pfd.revents & (POLLIN | POLLERR | POLLHUP)))
            continue;

        struct {
            bool        closeSocket;
            std::string closeMsg;
        } closeData = {false, ""};

        if (!(sockets_.checkSocketData(pfd.fd) &&
              sockets_.updateBuffer_(pfd))) {
            closeData.closeSocket = true;
            closeData.closeMsg = "Socket error while updating buffer.";
        }

        bool buffHasMoreCmds = true;
        while (buffHasMoreCmds && !closeData.closeSocket) {
            bool        msgTooLong = false;
            std::string cmdLine = sockets_.getCRLFLineFromBuffer(
                pfd.fd, buffHasMoreCmds, msgTooLong);
            if (sockets_.socketCloseMarker(pfd.fd)) {
                closeData.closeSocket = true;
                closeData.closeMsg =
                    "Error while composing command line from buffer.";
            }

            Client *client = clients_.find(pfd.fd);
            if (!client) {
                std::ostringstream oss;
                oss << "Internal error: No client data for fd:" << pfd.fd;
                log_(oss.str(), Logger::lmError);
                closeData.closeSocket = true;
                closeData.closeMsg = oss.str();
                break;
            }

            if (msgTooLong)
                cmds_.inputTooLong(*this, *client);
            if (cmdLine.empty())
                continue;

            {
                std::ostringstream oss;
                oss << "RecvMsg event:\nFrom FD " << pfd.fd << ": " << cmdLine;
                logger_.logMsg(oss.str(), Logger::lmIn);
            }

            // returns true if connection is to be closed
            closeData.closeSocket = cmds_.run(*this, *client, cmdLine);
            closeData.closeMsg = "";
        };

        if (closeData.closeSocket) {
            removeClientSocketPfd_(pfd.fd, closeData.closeMsg);
            --idx;
        }
    }
}

void Server::removeClientSocketPfd_(int const fd, std::string const &msg) {
    if (!msg.empty() && !sockets_.isServerSocket(fd)) {
        Client *client = clients_.find(fd);
        if (client)
            client->sendClosingMsg(msg);
        clients_.remove(fd, channels_);
    }
    sockets_.closeSocket(fd);
    removePollFd_(fd);
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

    std::string clientIP;
    uint16_t    port = 0;
    srvHelp::getIPandPort(sockAddrIn46, srvHelp::getSaFamily(sockAddrIn46),
                          port, clientIP);
    clients_.add(clientFd, clientIP, *this);

    std::ostringstream oss;
    oss << "Client connected:"
        << " fd:" << clientFd
        << srvHelp::composeAddrfamAddrPortStr(sockAddrIn46);
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
        std::find_if(pollFds_.begin(), pollFds_.end(), srvHelp::PfdHasFd(fd));
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
    if (!setupServerSockets_())
        return;

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

/*
   NOTE: all send should go though server::sendMsg
*/
void Server::sendMsg(int const fd, std::string const &msg) {
    if (msg.length() > 2) {
        std::ostringstream oss;
        oss << "SendMsg event:\nSendto FD " << fd << ": "
            << msg.substr(0, msg.length() - 2);
        logger_.logMsg(oss.str(), Logger::lmOut);
    }
    if (!sockets_.sendMsg(fd, msg))
        removeClientSocketPfd_(fd, "Error while sending Msg.");
}
