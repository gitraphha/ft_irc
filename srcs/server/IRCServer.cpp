#include "server/IRCServer.hpp"

#include "server/SignalHandler.hpp"
// #include "server/Socket.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/poll.h>

/*
	IRCServer Constructor
	* set port and password from command line args
*/
IRCServer::IRCServer(int const port, std::string const &password)
    : port_(port), password_(password), numServerSockets_(0) {}

// /*
// 	IRCServer Destructor
// 		* ...
// */
// IRCServer::~IRCServer() {}

/*
	Dummy function to get a usable server socket
	for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
*/
static std::string getDomainStr(int const domain) {
    if (domain == AF_INET)
        return "IPv4";
    if (domain == AF_INET6)
        return "IPv6";
    return "";
}

static int sockAddrFamily(int const socketFd) {
    struct sockaddr sockAddr;
    socklen_t       sockLen = sizeof(sockAddr);
    if (getsockname(socketFd, &sockAddr, &sockLen) != 0)
        throw;
    return sockAddr.sa_family;
}

static void setSocketNonBlock(int const socketFd) {
    int const domain = sockAddrFamily(socketFd);
    if (fcntl(socketFd, F_SETFL, O_NONBLOCK) != 0) // NOLINT
        throw std::runtime_error(
            "Error while setting socket to NON-BLOCKING for " +
            getDomainStr(domain) + " !");
}

static void setSocketAddr(int const socketFd, int const port,
                          socketAddrX &sockAddrIn46, socklen_t &sockLen) {
    int domain = sockAddrFamily(socketFd);

    if (domain == AF_INET) {
        struct sockaddr_in &clientSockAddrIn4 = sockAddrIn46.sai4;
        clientSockAddrIn4.sin_family = AF_INET;
        clientSockAddrIn4.sin_addr.s_addr = INADDR_ANY;
        clientSockAddrIn4.sin_port = htons(port);
        sockLen = sizeof(clientSockAddrIn4);
    } else if (domain == AF_INET6) {
        struct sockaddr_in6 &clientSockAddrIn6 = sockAddrIn46.sai6;
        clientSockAddrIn6.sin6_family = AF_INET6;
        clientSockAddrIn6.sin6_addr = in6addr_any;
        clientSockAddrIn6.sin6_port = htons(port);
        sockLen = sizeof(clientSockAddrIn6);
    }
}

static void bindServerSocket(int const socketFd, int const domain,
                             int const port) {
    // struct sockaddr    *serverSockaddr = 0;
    // socklen_t           socklen = 0;
    // struct sockaddr_in  serverAddr4 = {};
    // struct sockaddr_in6 serverAddr6 = {};
    // if (domain == AF_INET) {
    //     serverAddr4.sin_family = AF_INET;
    //     serverAddr4.sin_addr.s_addr = INADDR_ANY;
    //     serverAddr4.sin_port = htons(port);
    //     serverSockaddr =
    //         reinterpret_cast<struct sockaddr *>(&serverAddr4); // NOLINT
    //     socklen = sizeof(serverAddr4);
    // } else if (domain == AF_INET6) {
    //     serverAddr6.sin6_family = AF_INET;
    //     serverAddr6.sin6_addr = in6addr_any;
    //     serverAddr6.sin6_port = htons(port);
    //     serverSockaddr =
    //         reinterpret_cast<struct sockaddr *>(&serverAddr6); // NOLINT
    //     socklen = sizeof(serverAddr6);
    // }
    socketAddrX sockAddrIn46;
    socklen_t   sockLen;
    setSocketAddr(socketFd, port, sockAddrIn46, sockLen);

    if (bind(socketFd, reinterpret_cast<struct sockaddr *>(&sockAddrIn46),
             sockLen) < 0)
        throw std::runtime_error("Error while binding server socket for " +
                                 getDomainStr(domain) + " !");
}

static void storePollFd(int const socketFd, std::vector<struct pollfd> &pFds,
                        std::vector<std::string> &usrInBuff) {
    pollfd pfd = {};
    pfd.fd = socketFd;
    pfd.events = POLLIN; // Monitor for incoming connections

    pFds.push_back(pfd);
    usrInBuff.push_back(std::string());
}

int IRCServer::createServerSocket_(int const domain) {
    if (domain != AF_INET && domain != AF_INET6)
        throw std::invalid_argument(
            "Internal Error while opening server socket.");

    int socketFd = socket(domain, SOCK_STREAM, 0);
    if (socketFd < 0)
        throw std::runtime_error("Error while opening server socket for " +
                                 getDomainStr(domain) + " !");

    int optval = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval,
                   sizeof(optval)) != 0)
        throw std::runtime_error("Error while setting socket options for " +
                                 getDomainStr(domain) + " !");

    if (domain == AF_INET6) {
        int v6only = 1;
        setsockopt(socketFd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only,
                   sizeof(v6only));
    }

    setSocketNonBlock(socketFd);

    bindServerSocket(socketFd, domain, port_);

    if (listen(socketFd, MAX_CONNECTIONS) < 0)
        throw std::runtime_error("Error while listening on socket for " +
                                 getDomainStr(domain) + " !");

    storePollFd(socketFd, pollFds_, clientInputBuffers_);
    ++numServerSockets_;

    return socketFd;
}

/*
	Dummy function to get a usable client socket
	on connect for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
*/
void IRCServer::acceptClient_(int const serverFd) {
    socketAddrX sockAddrIn46 = {};
    socklen_t   sockLen = 0;
    setSocketAddr(serverFd, 0, sockAddrIn46, sockLen);

    int const clientFd =
        accept(serverFd, reinterpret_cast<struct sockaddr *>(&sockAddrIn46),
               &sockLen); // NOLINT
    if (clientFd == -1)
        //throw ??
        ;

    setSocketNonBlock(clientFd);

    storePollFd(clientFd, pollFds_, clientInputBuffers_);

    std::cout << "Client connected:\n"
              << " fd: " << clientFd;

    if (sockAddrIn46.sai4.sin_family == AF_INET) {
        std::cout << "\n AF: " << getDomainStr(sockAddrIn46.sai4.sin_family)
                  << "\n addr:port: ";
        uint8_t *addrData =
            reinterpret_cast<uint8_t *>(&sockAddrIn46.sai4.sin_addr.s_addr);
        for (size_t idx = 0;
             idx < sizeof(sockAddrIn46.sai4.sin_addr) / sizeof(uint8_t);
             ++idx) {
            if (idx > 0)
                std::cout << ".";
            std::cout << std::dec << static_cast<int>(addrData[idx]);
        }
        std::cout << ":" << std::dec << ntohs(sockAddrIn46.sai4.sin_port)
                  << std::endl;
    } else if (sockAddrIn46.sai6.sin6_family == AF_INET6) {
        std::cout << "\n AF: " << getDomainStr(sockAddrIn46.sai6.sin6_family)
                  << "\n addr:port: [";
        uint8_t *addrData =
            reinterpret_cast<uint8_t *>(&sockAddrIn46.sai6.sin6_addr.s6_addr);
        for (size_t idx = 0;
             idx < sizeof(sockAddrIn46.sai6.sin6_addr) / sizeof(uint8_t);
             ++idx) {
            if (idx > 0 && idx % 4 == 0)
                std::cout << ":";
            std::cout << std::hex << static_cast<int>(addrData[idx]);
        }

        std::cout << "]:" << std::dec << ntohs(sockAddrIn46.sai4.sin_port)
                  << std::endl;
    }
}

/*
	Dummy function to disconnet aclient socket
	for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
	There is more to be done than just closing and removing
	the client socket.
*/
void IRCServer::disconnectClient_(size_t const idx) {
    pollfd &pfd = pollFds_.at(idx);

    if (idx == 0) {
        if (pollFds_.size() != 1)
            std::cout << "Close clients before closing server!" << std::endl;
        std::cout << "Closing server" << std::endl;
    } else
        std::cout << "Closing client #" << idx << " (" << pollFds_.size() - 2
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

	* sets up its listening socket
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
	* on server stop, disconnects all clients and closes
		the server socket
*/
void IRCServer::run() {
    createServerSocket_(AF_INET);
    createServerSocket_(AF_INET6);
    //createServerSocket_(AF_INET);

    while (!SignalHandler::stopLoopSignalReceived()) {
        usleep(100);

        int pollResult = poll(pollFds_.data(), pollFds_.size(), 0);
        if (pollResult == -1) {
            // nice error message and exit the loop
            // or throw, but then have ressource cleanup after catch
            break;
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
