#ifndef IRCSERVERHELPER_HPP
#define IRCSERVERHELPER_HPP

#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>

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

inline void storePollFd(int const socketFd, std::vector<struct pollfd> &pFds,
                        std::vector<std::string> &usrInBuff) {
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

inline void printAddrfamilyAddrPort(socketAddrX const &sockAddrIn46) {
    char      addrBuffer[64];
    socklen_t addrBuffLen = sizeof(addrBuffer) / sizeof(char);

    if (sockAddrIn46.sai4.sin_family == AF_INET) {
        std::cout << "\n AF: "
                  << getAdressFamilyStr(sockAddrIn46.sai4.sin_family)
                  << "\n addr:port: "
                  << inet_ntop(AF_INET, &sockAddrIn46.sai4.sin_addr, addrBuffer,
                               addrBuffLen)
                  << ":" << std::dec << ntohs(sockAddrIn46.sai4.sin_port)
                  << std::endl;
    } else if (sockAddrIn46.sai6.sin6_family == AF_INET6) {
        std::cout << "\n AF: "
                  << getAdressFamilyStr(sockAddrIn46.sai6.sin6_family)
                  << "\n addr:port: ["
                  << inet_ntop(AF_INET6, &sockAddrIn46.sai6.sin6_addr,
                               addrBuffer, addrBuffLen)
                  << "]:" << std::dec << ntohs(sockAddrIn46.sai4.sin_port)
                  << std::endl;
    }
}

} // namespace srvHelp

#endif // IRCSERVERHELPER_HPP