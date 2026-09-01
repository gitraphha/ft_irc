#pragma once

#include "ChannelManager.hpp"
#include "ClientManager.hpp"
#include "CommandProcessor.hpp"
#include "Logger.hpp"
#include "Socket.hpp"

#include <poll.h>
#include <string>
#include <vector>

#define SERVER_NAME "irc.local"
#define MAX_CONNECTIONS 512

class Server {
  private:
    std::string serverName_;
    int         port_;
    std::string password_;

    Logger logger_;

    Socket           sockets_;
    ChannelManager   channels_;
    ClientManager    clients_;
    CommandProcessor cmds_;

    size_t                     numServerSockets_;
    std::vector<struct pollfd> pollFds_;

    void setupServerSockets_();
    int  createServerSocket_(int const domain);
    void processServerSocketData_();
    void acceptClient_(int const serverFd);
    void processClientSockets_();
    void storePollFd_(int const socketFd, bool srvSocket);
    void setClientInfo_(int fd, Client **clntPtr, std::string &nick,
                        bool &isReg);
    void log_(std::string const &msg, Logger::logMode const lm);

    Server(Server const &other);
    Server &operator=(Server const &other);

  public:
    Server(int const port, std::string const &password);

    ~Server();

    void run();

    bool tryRegister(Client &client) {
        (void)client;
        return true;
    };
};
