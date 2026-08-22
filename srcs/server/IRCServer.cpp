#include "server/IRCServer.hpp"

#include "server/IRCServerHelper.hpp"
#include "server/SignalHandler.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <sys/poll.h>

/*
	IRCServer Constructor
	* set port and password from command line args
*/
IRCServer::IRCServer(int const port, std::string const &password)
    : port_(port), password_(password), numServerSockets_(0) {}

/*
	tbd
*/
int IRCServer::createServerSocket_(int const domain) {
    if (domain != AF_INET && domain != AF_INET6)
        throw std::invalid_argument(
            "Internal Error while opening server socket.");

    int socketFd = socket(domain, SOCK_STREAM, 0);
    if (socketFd < 0)
        throw std::runtime_error("Error while opening server socket for " +
                                 srvHelp::getAdressFamilyStr(domain) + " !");

    int optval = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR /* | SO_REUSEPORT */,
                   &optval, sizeof(optval)) != 0)
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

    srvHelp::storePollFd(socketFd, pollFds_, clientInputBuffers_);
    ++numServerSockets_;

    return socketFd;
}

/*
	tbd
*/
void IRCServer::acceptClient_(int const serverFd) {
    socketAddrX sockAddrIn46 = {};
    socklen_t   sockLen = 0;

    srvHelp::setSocketAddr(serverFd, 0, sockAddrIn46, sockLen);

    int const clientFd = accept(
        serverFd, reinterpret_cast<struct sockaddr *>(&sockAddrIn46), // NOLINT
        &sockLen);
    if (clientFd == -1) {
        // throw -> no everything else can still run w/o problems
        // silent return -> cannot sent anything to client without working socket anyways...
        return;
    }

    srvHelp::setSocketNonBlock(clientFd);

    srvHelp::storePollFd(clientFd, pollFds_, clientInputBuffers_);

    std::cout << "Client connected:\n"
              << " fd: " << clientFd;
    srvHelp::printAddrfamilyAddrPort(sockAddrIn46);
}

/*
	tbd
*/
void IRCServer::disconnectClient_(size_t const idx) {
    pollfd &pfd = pollFds_.at(idx);

    if (idx < numServerSockets_) {
        if (pollFds_.size() < numServerSockets_)
            std::cout << "Close clients before closing server!" << std::endl;
        std::cout << "Closing server" << std::endl;
    } else
        std::cout << "Closing client #" << idx << " ("
                  << pollFds_.size() - 1 - numServerSockets_
                  << " clients remaining)" << std::endl;
    close(pfd.fd);

    std::vector<std::string>::difference_type dist =
        static_cast<std::vector<std::string>::difference_type>(idx);
    pollFds_.erase(pollFds_.begin() + dist);
    clientInputBuffers_.erase(
        clientInputBuffers_.begin() +
        static_cast<std::vector<std::string>::difference_type>(dist));
}

/*
	Main server loop

	* sets up its listening socket(s)
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
	* on server stop signal, disconnects all clients and closes
		the server socket(s)
*/
void IRCServer::run() {
    createServerSocket_(AF_INET);
    createServerSocket_(AF_INET6);

    while (!SignalHandler::stopLoopSignalReceived()) {
        usleep(100);

        int pollResult = poll(pollFds_.data(), pollFds_.size(), 0);
        if (pollResult == -1) {
            static size_t loopCounter = 0;
            static size_t retries = 42;
            if (loopCounter % 1000 == 0) {
                retries = 42;
                // EINTR — The call was interrupted by a signal. This is the most common retriable error. Retrying is standard practice.
                // EAGAIN — Temporary resource unavailability (rare with poll(), but possible in some edge cases).
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN && retries > 1) {
                    retries--;
                    usleep(10000);
                    continue;
                }
                throw std::runtime_error("poll failed too many times.");
            }
        }

        for (size_t idx = 0; idx < numServerSockets_; ++idx) {
            struct pollfd const &serverPollFds = pollFds_[idx];
            if (serverPollFds.revents & POLLERR) {
                std::cerr << "Server socket error\n";
                break;
            }
            if (serverPollFds.revents & POLLIN) {
                acceptClient_(serverPollFds.fd);
            }
        }
        for (size_t idx = numServerSockets_; idx < pollFds_.size(); ++idx) {
            pollfd &pfd = pollFds_.at(idx);
            if (pfd.revents & POLLERR) {
                std::cerr << "Client socket error on fd: " << pfd.fd
                          << std::endl;
                disconnectClient_(idx);
            } else if (pfd.revents & POLLIN) {
                char    buffer[BUFFLEN];
                ssize_t bytesRead = recv(pfd.fd, &buffer[0], BUFFLEN, 0);

                if (bytesRead > 0) {
                    std::string &userInput = clientInputBuffers_.at(idx);
                    std::string  bufferStr(&buffer[0], bytesRead);
                    // bool         userInputValid = false;

                    // // DEBUG for cont'd messages
                    // if (!userInput.empty() && *userInput.rbegin() != '\r') {
                    //     *userInput.rbegin() = '&';
                    // }
                    userInput += bufferStr;
                    size_t lfcrPos = userInput.find("\r\n");
                    if (userInput.length() >= MAXMSGLEN) {
                        if (lfcrPos > MAXMSGLEN - 2) {
                            std::cerr << "Client data error: message too "
                                         "long.\nData sent will be discarded."
                                      << std::endl;
                            while (bytesRead > 0)
                                bytesRead =
                                    recv(pfd.fd, &buffer[0], BUFFLEN, 0);
                            userInput.clear();
                            continue;
                        }
                    } else if (lfcrPos == std::string::npos) {
                        continue;
                    } else if (lfcrPos < userInput.length() - 2) {
                        std::cerr << "Client data error: message "
                                     "malformed, data after CR-LF.\nExtraneous "
                                     "data will be discarded."
                                  << std::endl;
                        userInput.resize(lfcrPos + 2);
                    }
                    // if (lfcrPos == userInput.length() - 2)
                    //     userInputValid = true;
                    // else {
                    //     std::cerr << "What's going on here?" << std::endl;
                    //     // userInput += bufferStr.substr(0, lfcrPos + 2);
                    // }
                    // if (userInputValid)
                    {
                        std::cout << "UserInput read from client " << idx
                                  << ": \n"
                                  << std::string(userInput) /* << std::endl */;
                        userInput.clear();
                    }
                    // }
                }
                if (bytesRead <= 0) {
                    // Comment will be deleted later, kept for reference for now
                    // this means, the client closed the connection
                    // POLLHUP isn't useful, because
                    // When you kill netcat with Ctrl+C:
                    //     Netcat's OS kernel sent a FIN packet to your server
                    //     Your server's poll() detected this with POLLIN
                    //     Your read() returned 0 (zero bytes) = "I got the FIN, connection closed"
                    // FIN is the TCP mechanism for orderly shutdown.
                    // That's why POLLIN + read() return 0—the FIN,
                    // and the zero read is how the kernel tells you "the peer closed."
                    disconnectClient_(idx);
                }
            }
        }
    }
    while (!pollFds_.empty()) {
        disconnectClient_(pollFds_.size() - 1);
    }
}
