#pragma once

#include "ChannelManager.hpp"
#include "ClientManager.hpp"
#include "CommandProcessor.hpp"
#include "Logger.hpp"

#include <exception>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <vector>

#define SERVER_NAME "irc.local"
#define MAX_CONNECTIONS 512

#define RECVBUFFLEN 516
#define MAXMSGLEN 512
#define SOCKETERRORLIMIT 16

#ifndef VERBOSE_LOG
#define VERBOSE_LOG 0
#endif

class Server {
  private:
    struct SocketIOData {
        std::string readBuffer;
        std::string writeBuffer;
        size_t      errorCount;
        bool        discardMode;

        SocketIOData() : errorCount(0), discardMode(false) {}
    };

    ChannelManager              channels_;
    ClientManager               clients_;
    CommandProcessor            cmds_;

    int                         port_;
    std::string                 password_;
    std::string                 serverName_;

    size_t                      numServerSockets_;
    std::vector<struct pollfd>  pollFds_;
    // std::vector<std::string>   clientInputBuffers_;
    std::map<int, SocketIOData> sockIODataStore_;

    Logger                      logger_;

    // Server(Server const &other);
    // Server &operator=(Server const &other);

    int                         createServerSocket_(int const domain);
    void                        processServerSocketData_();
    void                        acceptClient_(int const serverFd);
    void                        storePollFd_(int const socketFd);
    void                        processClientSocketData_();
    void                        closeSocket_(int const fd);
    void                        log_(std::string const &msg, Logger::logMode const lm);

  public:
    Server(int const port, std::string const &password);

    // ~Server();

    std::string const &serverName() {
        return serverName_;
    };

    void run();
    bool sendMsg(int const fd, std::string const &msg);

    bool tryRegister(Client &client) {
        (void)client;
        return false;
    };
};
