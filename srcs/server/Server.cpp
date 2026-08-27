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
*/
Server::Server(int const port, std::string const &password)
    : port_(port), password_(password), serverName_(SERVER_NAME),
      numServerSockets_(0), logger_(serverName_ + ": ") {}

bool Server::sendMsg(int const fd, std::string const &msg) {
    SocketIOData &sIOD = sockIODataStore_.at(fd);
    ssize_t       bytesSent = -1;
    bool          success = true;

    if (msg.length() > sIOD.writeBuffer.max_size() - sIOD.writeBuffer.length())
        sIOD.errorCount = SOCKETERRORLIMIT;
    else {
        sIOD.writeBuffer += msg;
        bytesSent = send(fd, sIOD.writeBuffer.c_str(),
                         sIOD.writeBuffer.length(), MSG_NOSIGNAL);
        if (bytesSent <= 0)
            ++sIOD.errorCount;
    }
    if (sIOD.errorCount >= SOCKETERRORLIMIT) {
        closeSocket_(fd);
        success = false;
    }

#if VERBOSE_LOG
    std::ostringstream oss;
    oss << "writeBuffer: '" << sIOD.writeBuffer << "'";
    if (!success) {
        oss << ": send to FD " << fd << " failed.";
        log_(oss.str(), (logMode)lmError);
    } else {
        if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
            oss << " partly (" << bytesSent << "/" << sIOD.writeBuffer.length()
                << ": " << sIOD.writeBuffer.substr(0, bytesSent);
        else
            oss << " successfully";
        oss << " sent to FD: " << fd;
        log_(oss.str(), (logMode)lmInfo);
    }
#endif // VERBOSE_LOG

    if (!success)
        return false;

    if (sIOD.errorCount > 0)
        sIOD.errorCount = 0;
    if (static_cast<size_t>(bytesSent) < sIOD.writeBuffer.length())
        sIOD.writeBuffer.erase(0, bytesSent);
    else
        sIOD.writeBuffer.clear();
    return true;
}

/*
	tbd
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
    for (size_t idx = 0; idx < numServerSockets_; ++idx) {
        struct pollfd const &serverPollFds = pollFds_[idx];
        if ((serverPollFds.revents & POLLERR) && !srvHelp::checkSocketFailOK())
            throw std::runtime_error("Server socket error");
        if (serverPollFds.revents & POLLIN)
            acceptClient_(serverPollFds.fd);
    }
}

void Server::processClientSocketData_() {
    for (size_t idx = numServerSockets_; idx < pollFds_.size(); ++idx) {
        pollfd &pfd = pollFds_.at(idx);
        if ((pfd.revents & POLLERR) && !srvHelp::checkSocketFailOK()) {
            std::cerr << "Client socket error on fd: " << pfd.fd
                      << "Disconnect client." << std::endl;
            closeSocket_(idx);
        } else if (pfd.revents & POLLIN) {
            char    recvBuffer[RECVBUFFLEN];
            ssize_t bytesRead = recv(pfd.fd, &recvBuffer[0], RECVBUFFLEN, 0);

            if (bytesRead > 0) {
                SocketIOData &sockIOData = sockIODataStore_.at(pfd.fd);
                Client const *client = 0;
                try {
                    client = clients_.at(pfd.fd);
                } catch (std::exception const &e) {
                    std::cerr << "caught exception: " << e.what() << std::endl;
                }

                std::string nickName = "*";
                if (client && client->isRegistered())
                    nickName = client->getNick();

                std::string &userInput = sockIOData.readBuffer;
                userInput += std::string(&recvBuffer[0], bytesRead);

                size_t lfcrPos = userInput.find("\r\n");
                if ((userInput.length() >= MAXMSGLEN) &&
                    (lfcrPos == std::string::npos || lfcrPos > MAXMSGLEN - 2)) {
                    sockIOData.discardMode = true;
                    sendMsg(pfd.fd,
                            Reply::build(
                                serverName(), nickName,
                                Reply::reply(Reply::ERR_INPUTTOOLONG, "")));
                }
                if (sockIOData.discardMode) {
                    if (lfcrPos != std::string::npos) {
                        userInput.erase(0, lfcrPos + 2);
                        sockIOData.discardMode = false;
                        lfcrPos = userInput.find("\r\n");
                    } else if ('\r' == *userInput.rbegin())
                        userInput = "\r";
                    else
                        userInput.clear();
                }
                if (lfcrPos == std::string::npos || sockIOData.discardMode) {
                    continue;
                }
#if VERBOSE_LOG
                std::ostringstream oss;
                oss << "You are client #" << idx + 1 - numServerSockets_
                    << ": \n"
                    << "Srv received: "
                    << std::string(userInput) /* << std::endl */;
                std::cout << "UserInput read from client #" << oss.str();
                // Pong to client
                sendMsg(pfd.fd, oss.str());
#endif // VERBOSE_LOG
                try {
                    cmds_.run(*this, *clients_.at(pfd.fd), userInput);
                } catch (Error const &e) {
                    if (e.hasReply()) {
                        Client const &c = *clients_.at(pfd.fd);
                        sendMsg(pfd.fd, Reply::build(serverName(), c.getNick(),
                                                     e.reply()));
                    } else {
                        std::string tmp("Error while trying to send msg:\n");
                        tmp.append(e.what());
                        log_(tmp.c_str(), (logMode)lmError);
                    }
                }
                userInput.clear();

            } else if (bytesRead <= 0) {
                // Comment will be deleted later, kept for reference for now
                // this means, the client closed the connection
                // POLLHUP isn't useful, because:
                // When you kill netcat with Ctrl+C:
                //     Netcat's OS kernel sent a FIN packet to your server
                //     Your server's poll() detected this with POLLIN
                //     Your read() returned 0 (zero bytes) == "I got the FIN, connection closed"
                // FIN is the TCP mechanism for orderly shutdown.
                // That's why POLLIN + read() return 0—the FIN,
                // and the zero read is how the kernel tells you "the peer closed."
                closeSocket_(idx);
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
        // throw -> no everything else can still run w/o problems
        // silent return -> cannot sent anything to client without working socket anyways...
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
        if (pollFds_.size() != idx + 1)
            return log_("Internal error: Close client sockets before "
                        "closing server!",
                        (logMode)lmError);
        oss << "Closing server socket ("
            << srvHelp::getAdressFamilyStr(srvHelp::sockAddrFamily(fd)) << ")";
    } else {
        oss << "Closing client #" << idx + 1 - numServerSockets_ << " socket ("
            << pollFds_.size() - 1 - numServerSockets_ << " clients remaining)";
    }
    log_(oss.str(), (logMode)lmInfo);

    close(fd);
    std::vector<std::string>::difference_type dist =
        static_cast<std::vector<std::string>::difference_type>(idx);
    pollFds_.erase(pollFds_.begin() + dist);
    sockIODataStore_.erase(fd);
}

void Server::log_(std::string const &msg, logMode const lm) {
    logger_.logMsg(msg, lm);
}

/*
	Main server loop

	* sets up its listening socket(s)
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
	* on server stop signal, disconnects all clients and closes
		the server socket(s)
	* if poll returns an error -> exception is thrown, except for two scenarios:
        EINTR — The call was interrupted by a signal. This is the most common
		        retriable error. Retrying is standard practice.
        EAGAIN — Temporary resource unavailability (rare with poll(),
		         but possible in some edge cases).
*/
void Server::run() {
    createServerSocket_(AF_INET);
    createServerSocket_(AF_INET6);

    while (!SignalHandler::stopLoopSignalReceived()) {
        int pollResult = poll(pollFds_.data(), pollFds_.size(), -1);
        if (pollResult == -1)
            throw std::runtime_error("Fatal error: poll failed.");

        processServerSocketData_();
        processClientSocketData_();
    }
    while (!pollFds_.empty()) {
        closeSocket_(pollFds_.size() - 1);
    }
}
