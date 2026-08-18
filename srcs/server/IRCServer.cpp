#include "server/IRCServer.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <exception>
#include <iostream>
#include <sys/poll.h>

/*
	Signal handling
	will be refactored in a separate class
*/
volatile std::sig_atomic_t IRCServer::g_stopServerLoop = 0;

/*
	Signal callback function
	Will set a static class member to the triggering signal,
	i.e. != 0, so the state can be used as a bool value directly.
	Will be refactored in a separate class, and with a more explicit
	stop state request function
*/
void                       IRCServer::g_handleStopSignals(int signal) {
    g_stopServerLoop = signal;
}

/*
	Setup all signal that should trigger the
	IRCServer to stop.
*/
void IRCServer::_initSignalHandling() {
    g_stopServerLoop = 0;
    _stopSignals.push_back(SIGINT);
    _stopSignals.push_back(SIGQUIT);
    _stopSignals.push_back(SIGTERM);

    if (!_setStopSignals(g_handleStopSignals))
        throw std::runtime_error("std::signal failed");
}

/*
	Reset all signal that should trigger the
	IRCServer to stop to the default on server exit.
*/
void IRCServer::_resetSignalHandling() {
    _setStopSignals(SIG_DFL);
}

/*
	Helper function to actually set the signal behaviour
	on system level using sigaction, as this is the most
	portable POSIX compliant way to do it.
*/
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

/*
	IRCServer Constructor
		* setup signal handling
		* ...
*/
IRCServer::IRCServer() {
    _initSignalHandling();
}

/*
	IRCServer Destructor
		* reset signal handling to default
		* ...
*/
IRCServer::~IRCServer() {
    _resetSignalHandling();
}

/*
	Dummy function to get a usable server socket
	for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
*/
int IRCServer::_createServerSocket() {
    // Create a TCP socket
    int serverSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (serverSocket == -1) {
        return -1;
    }
    // Bind
    struct sockaddr_in serverAddr = {};
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

/*
	Dummy function to get a usable client socket
	on connect for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
*/
void IRCServer::_acceptClient() {
    int                clientFd = 0;
    struct pollfd      clientPollfd = {};
    struct sockaddr_in clientAI = {};
    socklen_t          aiSize = sizeof(clientAI);

    // accept4, so we can directly set SOCK_NONBLOCK
    // the subject allows fcntl only on macOS
    clientFd = accept4(_pollfds.front().fd,
                       reinterpret_cast<struct sockaddr *>(&clientAI), &aiSize,
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

/*
	Dummy function to disconnet aclient socket
	for mockup testing.
	Implementation explicitely not exhaustive,
	will be replaced by an "official" version anyways.
	There is more to be done than just closing and removing
	the client socket.
*/
void IRCServer::_disconnectClient(size_t const idx) {
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

/*
	Main server loop

	* sets up its listening socket
	* runs an infinit loop to listen for connections
	* handles client connect and disconnect scenarios
	* on server stop, disconnects all clients and closes
		the server socket
*/
void IRCServer::run() {
    _createServerSocket();

    while (!g_stopServerLoop) {
        sleep(1);

        int pollResult = poll(_pollfds.data(), _pollfds.size(), 0);
        if (pollResult == -1) {
            // nice error message and exit the loop
            // or throw, but then have ressource cleanup after catch
            break;
        }

        struct pollfd &serverPollFds = _pollfds[0];
        if (serverPollFds.revents & POLLERR) {
            std::cerr << "Server socket error\n";
            break;
        }

        if (serverPollFds.revents & POLLIN) {
            _acceptClient();
        }
        for (size_t idx = 1; idx < _pollfds.size(); ++idx) {
            pollfd &pfd = _pollfds.at(idx);
            if (pfd.revents & POLLERR) {
                std::cerr << "Client socket error on fd: " << pfd.fd
                          << std::endl;
                // Do we need to disconnect directly in this case? Maybe not.
                _disconnectClient(idx);
            } else if (pfd.revents & POLLIN) {
                char buffer[1024] = "";
                int  bytesRead = read(pfd.fd, buffer, 1024);
                // for mockup only,
                // this functionality will go to its own function of course
                if (bytesRead > 0) {
                    std::cout << "Buffer read from client " << idx << ": \n"
                              << std::string(buffer) << std::endl;
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
                    _disconnectClient(idx);
                }
            }
        }
    }
    while (!_pollfds.empty()) {
        _disconnectClient(_pollfds.size() - 1);
    }
    std::cout << "caught signal: " << g_stopServerLoop << "." << std::endl;
}
