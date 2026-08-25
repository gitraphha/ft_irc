#pragma once

// draft: to be completed by rhaas

#include "ChannelManager.hpp"
#include "ClientManager.hpp"
#include "CommandProcessor.hpp"

#define SERVER_NAME "irc.local"

class Server {
    ChannelManager   channels_;
    ClientManager    clients_;
    CommandProcessor cmds_;
    int              port_;
    std::string      password_;

  public:
    Server(int port, const std::string &password);

    bool tryRegister(Client &client);
};
#ifndef SERVER_HPP
#define SERVER_HPP

#include <exception>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <vector>

#define BUFFLEN 516
#define MAXMSGLEN 512
#define MAX_CONNECTIONS 512

class Server {
  private:
    int                        port_;
    std::string                password_;

    size_t                     numServerSockets_;
    std::vector<struct pollfd> pollFds_;
    std::vector<std::string>   clientInputBuffers_;

    // Server(Server const &other);
    // Server &operator=(Server const &other);

    int                        createServerSocket_(int const domain);
    void                       processServerSocketData_();
    void                       acceptClient_(int const serverFd);
    void                       processClientSocketData_();
    void                       disconnectClient_(size_t const idx);

  public:
    Server(int const port, std::string const &password);
    // ~Server();

    void run();
    bool sendMsg(int fd, std::string const &msg);
};

#endif // SERVER_HPP