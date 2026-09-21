#include "Server.hpp"

#include "Client.hpp"        // for Client
#include "Reply.hpp"         // for fromClient
#include "ServerHelper.hpp"  // for getAdressFamilyStr, composeAddrfamAddrP...
#include "SignalHandler.hpp" // for SignalHandler

#ifdef BONUS
#include "Bot.hpp" // for Bot
#endif

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
        removeClientSocketPfd_(fd);
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
            // defensible but server exits if this happens
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
#ifdef BONUS
		// Transfer listener/data fds are in pollFds_ too (syncTransferPollFds_)
        // but are not clients: processTransferSocket_ handles their revents.
        if (tm_.ownsFd(pfd.fd))
            continue;
#endif

        if (sockets_.socketCloseMarker(pfd.fd)) {
            removeClientSocketPfd_(pfd.fd, "Socket marked for closing.");
            --idx;
            continue;
        }

        if (pfd.revents & (POLLOUT))
            sockets_.flushWriteBuffer(pfd.fd);
        setPolloutInterest_(pfd.fd);

        // Client already removed (QUIT, wrong password): its 464/ERROR was
        // still queued. Close once it is sent, or if the peer is gone.
        if (!clients_.find(pfd.fd)) {
            if (!sockets_.hasPendingWrite(pfd.fd) ||
                (pfd.revents & (POLLERR | POLLHUP))) {
                removeClientSocketPfd_(pfd.fd);
                --idx;
            }
            continue;
        }

        if (!(pfd.revents & (POLLIN | POLLERR | POLLHUP)))
            continue;

        struct {
            bool        closeSocket;
            std::string closeMsg;
        } closeData = {false, ""};

        /* POLLHUP is deliberately not treated as an error, a client that sends
         * and closes at once (e.g. printf ... | nc) still has data in the receive
         * buffer, so it is read normally; recv() returning 0 closes it afterwards. */
        if (pfd.revents & POLLERR) {
            closeData.closeSocket = true;
            closeData.closeMsg = "Socket POLLERR.";
        } else if (!(sockets_.checkSocketData(pfd.fd) &&
                     sockets_.updateBuffer_(pfd))) {
            closeData.closeSocket = true;
            closeData.closeMsg = "Connection closed";
        }

        bool buffHasMoreCmds = true;
        while (buffHasMoreCmds && !closeData.closeSocket) {
            bool        msgTooLong = false;
            std::string cmdLine = sockets_.getCRLFLineFromBuffer(
                pfd.fd, buffHasMoreCmds, msgTooLong);
            if (sockets_.socketCloseMarker(pfd.fd)) {
                closeData.closeSocket = true;
                closeData.closeMsg = "Connection closed";
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
                cmds_.inputTooLong(*client);
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
            if (closeData.closeMsg.empty() &&
                sockets_.hasPendingWrite(pfd.fd)) {
                // handler asked to close: keep the fd for POLLOUT only until
                // its pending output is sent (see the top of this loop)
                pfd.events = static_cast<short>(POLLOUT);
                clients_.remove(pfd.fd, channels_);
            } else {
                removeClientSocketPfd_(pfd.fd, closeData.closeMsg);
                --idx;
            }
        }
    }
}

void Server::removeClientSocketPfd_(int const fd, std::string const &msg) {
    if (!sockets_.isServerSocket(fd)) {
        if (!msg.empty()) {
            Client *client = clients_.find(fd);
            if (client) {
                // Client left without QUIT (killed, error limit) so tell its channels
                if (client->isRegistered())
                    channels_.broadcastToPeers(
                        *client, Reply::fromClient(client->getPrefix(), "QUIT",
                                                   "", msg));
                client->sendClosingMsg(msg);
            }
        }
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
        // Closing socket in Server::sendMsg messes up processClientSockets_'s loop.
        // It erases pollFds while processClientSockets_ is iterating
        // So, Socket::sendMsg only marks the failed fds to be closed.
        // Reaping client happens in processClientSockets_ when poll() returns.
        // With poll(-1), an idle loop would sleep forver and never reap these fds,
        // so wake it up every 1000.
        int timeout = 1000;
#ifdef BONUS
        syncTransferPollFds_();
        if (tm_.isActive())
            timeout = 100;
#endif
        int pollResult = poll(pollFds_.data(), pollFds_.size(), timeout);
        if (pollResult == -1) {
            if (SignalHandler::stopLoopSignalReceived())
                break;
            log_("Fatal error: poll failed.", Logger::lmError);
            break;
        }

        processServerSocketData_();
        processClientSockets_();
#ifdef BONUS
        processTransferSocket_();
#endif
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
    sockets_.sendMsg(fd, msg);
    setPolloutInterest_(fd);
}

void Server::setPolloutInterest_(int const fd) {
    std::vector<pollfd>::iterator it =
        std::find_if(pollFds_.begin(), pollFds_.end(), srvHelp::PfdHasFd(fd));
    if (it == pollFds_.end())
        return;
    short const pollout = static_cast<short>(POLLOUT);
    short const interest = sockets_.hasPendingWrite(fd) ? pollout : 0;
    it->events = static_cast<short>((it->events & ~pollout) | interest);
}

#ifdef BONUS
TransferManager &Server::getTransferManager() {
    return tm_;
}

Bot &Server::getBot() {
    return bot_;
}

// Keeps the transfer listener/data fds inside the single poll() set:
// drops the ones the TransferManager already closed, adds the live ones.
void Server::syncTransferPollFds_() {
    for (size_t i = numServerSockets_; i < pollFds_.size();) {
        int const fd = pollFds_.at(i).fd;
        if (!sockets_.checkSocketData(fd) && !tm_.ownsFd(fd))
            pollFds_.erase(pollFds_.begin() + i);
        else
            ++i;
    }
    if (!tm_.isActive())
        return;
    int const   fds[2] = {tm_.getListenerFd(), tm_.current()->dataFd};
    short const events[2] = {POLLIN, POLLOUT};
    for (int i = 0; i < 2; ++i)
        if (fds[i] != -1 &&
            std::find_if(pollFds_.begin(), pollFds_.end(),
                         srvHelp::PfdHasFd(fds[i])) == pollFds_.end())
            pollFds_.push_back(PollFd(fds[i], events[i]).pfd);
}

void Server::processTransferSocket_() {
    if (!tm_.isActive())
        return;
    if (tm_.expired()) {
        tm_.closeOffer();
        return;
    }
    int listener = tm_.getListenerFd();
    if (listener == -1)
        return;
    int           dataFd = tm_.current()->dataFd;
    struct pollfd pfds[2];
    int           n = 1;
    pfds[0] = PollFd(listener, POLLIN).pfd;
    if (dataFd != -1) {
        pfds[1] = PollFd(dataFd, POLLOUT).pfd;
        n = 2;
    }
    for (int i = 0; i < n; ++i) { // revents from the main poll()
        std::vector<pollfd>::iterator it = std::find_if(
            pollFds_.begin(), pollFds_.end(), srvHelp::PfdHasFd(pfds[i].fd));
        if (it == pollFds_.end())
            pfds[i].revents = 0;
        else
            pfds[i].revents = it->revents;
    }
    if (pfds[0].revents & (POLLERR | POLLNVAL)) {
        tm_.closeOffer();
        return;
    }
    if (pfds[0].revents & POLLIN) {
        dataFd = tm_.getDataFd();
        if (dataFd < 0)
            return;
        // pfds[1] = PollFd(dataFd, POLLOUT).pfd;
        // n = 2;
        return; // the data fd joins the poll set on the next iteration
    }
    if (n != 2)
        return;
    if (pfds[1].revents & (POLLERR | POLLNVAL)) {
        tm_.closeOffer();
        return;
    }
    if (!(pfds[1].revents & (POLLOUT | POLLHUP)))
        return;
    ssize_t sent = 0;
    do {
        sent = tm_.sendChunk();
    } while (sent > 0);
    std::string msg;
    if (sent < 0)
        msg = "transfer failed";
    else if (tm_.transferDone())
        msg = "transfer complete";
    else
        return;
    Client *client = clients_.find(tm_.current()->clientFd);
    if (client)
        client->send(Reply::fromClient(bot_.getPrefix(serverName_), "PRIVMSG",
                                       client->getNick(), msg));
    tm_.closeOffer();
}

#endif
