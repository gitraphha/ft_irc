#ifndef SERVERHELPER_HPP
#define SERVERHELPER_HPP

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/poll.h>
#include <unistd.h>
#include <vector>

typedef union uSocketAddrX {
    struct sockaddr_in  sai4;
    struct sockaddr_in6 sai6;
} socketAddrX;

namespace srvHelp {

inline std::string getAdressFamilyStr(int const domain) {
    if (domain == AF_INET)
        return "IPv4";
    if (domain == AF_INET6)
        return "IPv6";
    return "Unknown Address Family id";
}

inline int sockAddrFamily(int const socketFd) {
    struct sockaddr sockAddr = {};
    socklen_t       sockLen = sizeof(sockAddr);
    if (getsockname(socketFd, &sockAddr, &sockLen) != 0) // NOLINT
        throw;
    return sockAddr.sa_family;
}

inline void setSocketNonBlock(int const socketFd) {
    int const domain = sockAddrFamily(socketFd);
    if (fcntl(socketFd, F_SETFL, O_NONBLOCK) != 0) // NOLINT
        throw std::runtime_error(
            "Error while setting socket to NON-BLOCKING for " +
            getAdressFamilyStr(domain) + " !");
}

inline void setSocketAddr(int const socketFd, int const port, // NOLINT
                          socketAddrX &sockAddrIn46,
                          socklen_t   &sockLen) { // NOLINT
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

inline void bindServerSocket(int const socketFd, int const domain, // NOLINT
                             int const port) {
    socketAddrX sockAddrIn46 = {};
    socklen_t   sockLen = 0;
    setSocketAddr(socketFd, port, sockAddrIn46, sockLen);

    if (bind(socketFd,
             reinterpret_cast<struct sockaddr *>(&sockAddrIn46), // NOLINT
             sockLen) < 0)
        throw std::runtime_error("Error while binding server socket for " +
                                 getAdressFamilyStr(domain) + " !");
}

inline void storePollFd(int const                   socketFd,
                        std::vector<struct pollfd> &pFds,      // NOLINT
                        std::vector<std::string>   &usrInBuff) { // NOLINT
    pollfd pfd = {};
    pfd.fd = socketFd;
    pfd.events = POLLIN; // Monitor for incoming connections

    pFds.push_back(pfd);
    usrInBuff.push_back(std::string());
}

inline bool isSocketListening(int sockfd) {
    int       optVal = 0;
    socklen_t optLen = sizeof(optVal);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ACCEPTCONN, &optVal, &optLen) < 0)
        throw std::runtime_error("Error getting socket options.");

    return (optVal > 0);
}

inline std::string printAddrfamilyAddrPort(socketAddrX const &sockAddrIn46) {
    char              addrBuffer[128];
    socklen_t const   addrBuffLen = sizeof(addrBuffer) / sizeof(char);

    sa_family_t const saFamily =
        reinterpret_cast<struct sockaddr const &>(sockAddrIn46.sai4) // NOLINT
            .sa_family;
    void const *addrPtr = 0;
    uint16_t    portNumber = 0;

    if (saFamily == AF_INET) {
        addrPtr = &sockAddrIn46.sai4.sin_addr;
        portNumber = ntohs(sockAddrIn46.sai4.sin_port);
    } else if (saFamily == AF_INET6) {
        addrPtr = &sockAddrIn46.sai6.sin6_addr;
        portNumber = ntohs(sockAddrIn46.sai6.sin6_port);
    }
    char const *addrStrPtr =
        inet_ntop(saFamily, addrPtr, &addrBuffer[0], addrBuffLen);
    std::string addrStr(addrStrPtr ? addrStrPtr : "<unknown address>");
    if (saFamily == AF_INET6)
        addrStr = "[" + addrStr + "]";

    std::ostringstream oss;
    oss << " AF: " << getAdressFamilyStr(saFamily)
        << "\n addr:port: " << addrStr << ":" << std::dec << portNumber;
    return oss.str();
}

inline bool checkSocketFailOK() {
	//return SignalHandler::stopLoopSignalReceived();
    static size_t loopCounter = 0;
    static size_t retries = 42;
    if (loopCounter % 1000 == 0)
        retries = 42;
    if (errno == EINTR)
        return true;
    if (errno == EAGAIN && retries > 1) {
        retries--;
        usleep(10000);
        return true;
    }
    return false;
}
} // namespace srvHelp

#endif // SERVERHELPER_HPP