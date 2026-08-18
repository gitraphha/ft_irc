#include "server/IRCServer.hpp"
#include "server/SignalHandler.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <exception>
#include <iostream>
#include <sys/poll.h>

/*
	IRCServer Constructor
	* ...
*/
IRCServer::IRCServer() {}

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

    while (!SignalHandler::stopLoopSignalReceived()) {
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
}
