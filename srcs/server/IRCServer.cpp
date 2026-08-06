#include "IRCServer.hpp"

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

    // Bind and listen
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8082);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr *)&serverAddr,
             sizeof(serverAddr)) == -1) {
        return -1;
    }

    listen(serverSocket, 100);

    _pollfds.resize(1);
    _pollfds[0].fd = serverSocket;
    _pollfds[0].events = POLLIN; // Monitor for incoming connections

    return serverSocket;
}

void IRCServer::_acceptClient() {
    int                clientFd;
    struct sockaddr_in clientAI;
    struct pollfd      clientPoll;
    socklen_t          AISize = sizeof(clientAI);

    // clientFd = accept(_pollfds.front().fd, reinterpret_cast<struct sockaddr *>(&clientAI), &AISize);
    clientFd = accept4(_pollfds.front().fd,
                       reinterpret_cast<struct sockaddr *>(&clientAI), &AISize,
                       SOCK_NONBLOCK);

    if (clientFd == -1) {
        std::cerr << "[ERROR] Accepting failed." << std::endl;
        return;
    }
    clientPoll.fd = clientFd;
    clientPoll.events = POLLIN;
    clientPoll.revents = 0;
    _pollfds.push_back(clientPoll);
    std::cout << "[SERVER] Client connected [FD: " << clientFd << "]"
              << std::endl;
}

void IRCServer::run() {
    _createDummyServerSocket();

    // Event loop
    while (!g_stopServerLoop) {
        std::cout << "running..." << std::endl;
        sleep(5);

        // poll() timeout in milliseconds (-1 = infinite)
        int pollResult = poll(_pollfds.data(), _pollfds.size(), 0);

        if (pollResult == -1) {
            // print nice error message and exit the loop
            // or throw, but then have ressource cleanup after catch
            break;
        } else if (pollResult == 0) {
            // no data ready to read
            continue;
        }

        // Check if server socket has activity
        struct pollfd &serverPollFds = _pollfds[0];
        if (serverPollFds.revents & POLLERR) {
            std::cout << "Socket error\n";
            break;
        } else if (serverPollFds.revents & POLLIN) {
            _acceptClient();
        }

		// Clients
        for (size_t idx = 1; idx < _pollfds.size(); ++idx) {
            pollfd &pfd = _pollfds.at(idx);
            char    buffer[1024] = "";
            int     bytesRead = read(pfd.fd, buffer, 1024);
            if (bytesRead < 0) {
                if (!(errno == EAGAIN || errno == EWOULDBLOCK))
                    std::cerr << "[ERROR] Recv failed on FD " << pfd.fd
                              << std::endl;
            } else if (bytesRead == 0) {
                // removeClient(fd, pollIndex, "Remote host closed the connection");
                close(pfd.fd);
                _pollfds.erase(_pollfds.begin() + idx);
				std::cout << "Closed client #" << idx << " ("<<_pollfds.size() -1 << " remaining)"<< std::endl;
            } else {
                std::cout << "Buffer read from client " << idx << ": \n"
                          << std::string(buffer) << std::endl;
            }
        }
    }
    close(_pollfds[0].fd);
    for (size_t idx = 1; idx < _pollfds.size(); ++idx) {
        pollfd &pfd = _pollfds.at(idx);
        close(pfd.fd);
        _pollfds.erase(_pollfds.begin() + idx);
        std::cout << "Closed client #" << idx << " ("<<_pollfds.size() - 1 << " remaining)"<< std::endl;		
    }

    std::cout << "caught signal: " << g_stopServerLoop << "." << std::endl;
}
