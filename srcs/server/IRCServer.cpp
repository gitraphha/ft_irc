#include "server/IRCServer.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <exception>
#include <iostream>
#include <sys/poll.h>

volatile std::sig_atomic_t IRCServer::g_stopServerLoop = 0;

void                       IRCServer::g_handleStopSignals(int signal) {
    g_stopServerLoop = signal;
}

void IRCServer::_initSignalHandling() {
    g_stopServerLoop = 0;
    _stopSignals.push_back(SIGINT);
    _stopSignals.push_back(SIGQUIT);
    _stopSignals.push_back(SIGTERM);

    if (!_setStopSignals(g_handleStopSignals))
        throw std::runtime_error("std::signal failed");
}

void IRCServer::_resetSignalHandling() {
    _setStopSignals(SIG_DFL);
}

bool IRCServer::_setStopSignals(sighandler_t handler) {
    bool             success = true;

    struct sigaction sigact = {};
    sigact.sa_handler = handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    for (size_t i = 0; i < _stopSignals.size(); ++i)
        if (sigaction(_stopSignals.at(i), &sigact, NULL) != 0)
            success = false;

    return success;
}

IRCServer::IRCServer() {
    _initSignalHandling();
}

IRCServer::~IRCServer() {
    _resetSignalHandling();
}

int IRCServer::_createDummyServerSocket() {
    // Create a TCP socket
    int serverSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (serverSocket == -1) {
        return -1;
    }
    // Bind
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8082);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(serverSocket, reinterpret_cast<struct sockaddr *>(&serverAddr),
             sizeof(serverAddr)) == -1) {
        return -1;
    }
    // listen
    listen(serverSocket, 100);
    _pollfds.resize(1);
    _pollfds[0].fd = serverSocket;
    _pollfds[0].events = POLLIN; // Monitor for incoming connections

    return serverSocket;
}

void IRCServer::_acceptClient() {
    int                clientFd;
    struct pollfd      clientPollfd;
    struct sockaddr_in clientAI;
    socklen_t          AISize = sizeof(clientAI);

    // accept4, so we can directly set SOCK_NONBLOCK
    // the subject allows fcntl only on macOS
    clientFd = accept4(_pollfds.front().fd,
                       reinterpret_cast<struct sockaddr *>(&clientAI), &AISize,
                       SOCK_NONBLOCK);

    if (clientFd == -1) {
        std::cerr << "Accept failed." << std::endl;
        return;
    }
    clientPollfd.fd = clientFd;
    clientPollfd.events = POLLIN;
    clientPollfd.revents = 0;
    _pollfds.push_back(clientPollfd);
    std::cout << "Client connected, fd: " << clientFd << std::endl;
}

void IRCServer::_disconnectClient(int const idx) {
    pollfd &pfd = _pollfds.at(idx);

    if (idx == 0) {
        if (_pollfds.size() != 1)
            std::cout << "Close clients before closing server!" << std::endl;
        std::cout << "Closing server" << std::endl;
    } else
        std::cout << "Closing client #" << idx << " (" << _pollfds.size() - 2
                  << " clients remaining)" << std::endl;
    close(pfd.fd);
    _pollfds.erase(_pollfds.begin() + idx);
}

void IRCServer::run() {
    _createDummyServerSocket();

    // Event loop
    while (!g_stopServerLoop) {
        // std::cout << "running..." << std::endl;
        sleep(1);

        int pollResult = poll(_pollfds.data(), _pollfds.size(), 0);
        if (pollResult == -1) {
            // print nice error message and exit the loop
            // or throw, but then have ressource cleanup after catch
            break;
        }

        // Check if server socket has activity
        struct pollfd &serverPollFds = _pollfds[0];
        if (serverPollFds.revents & POLLERR) {
            std::cerr << "Server socket error\n";
            break;
        } else if (serverPollFds.revents & POLLIN) {
            _acceptClient();
        }

        // Clients
        for (size_t idx = 1; idx < _pollfds.size(); ++idx) {
            pollfd &pfd = _pollfds.at(idx);
            // std::cout << "Idx: " << idx << std::endl;
            // std::cout << " revents: " << std::hex << pfd.revents << std::endl;

            if (pfd.revents & POLLERR) {
                std::cerr << "Client socket error on fd: " << pfd.fd
                          << std::endl;
                // Do we need to disconnect directly in this case? Maybe not.
                _disconnectClient(idx);
            } else if (pfd.revents & POLLIN) {
                char buffer[1024] = "";
                int  bytesRead = read(pfd.fd, buffer, 1024);
                if (bytesRead > 0) {
                    std::cout << "Buffer read from client " << idx << ": \n"
                              << std::string(buffer) << std::endl;
                }
                if (bytesRead <= 0) {
                    // this means, the client closed the connection
                    // POLLHUP isn't useful, because

                    // When you kill netcat with Ctrl+C:
                    //     Netcat's OS kernel sent a FIN packet to your server
                    //     Your server's poll() detected this with POLLIN
                    //     Your read() returned 0 (zero bytes) = "I got the FIN, connection closed"
                    // FIN is the TCP mechanism for orderly shutdown.
                    // That's why POLLIN + read() return 0—the FIN,
                    // and the zero read is how the kernel tells you "the peer closed."
                    _disconnectClient(idx);
                }
            }
        }
    }
    while (!_pollfds.empty()) {
        // disconnecting clients first
        _disconnectClient(_pollfds.size() - 1);
    }
    std::cout << "caught signal: " << g_stopServerLoop << "." << std::endl;
}
