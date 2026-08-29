#pragma once

#include "ChannelManager.hpp"
#include "ClientManager.hpp"
#include "CommandProcessor.hpp"
#include "Logger.hpp"

#include <poll.h>
#include <string>
#include <vector>

#define SERVER_NAME "irc.local"
#define MAX_CONNECTIONS 512

#define RECVBUFFLEN 1024
#define MAXMSGLEN 512
#define SOCKETERRORLIMIT 8

#define NETCATDBG 1 // For netcat debug

class Server {
  private:
    struct SocketIOData {
        std::string readBuffer;
        std::string writeBuffer;
        ssize_t     bytesRead;
        size_t      recvErrorCount;
        size_t      sendErrorCount;
        bool        discardMode;

        SocketIOData()
            : bytesRead(0), recvErrorCount(0), sendErrorCount(0),
              discardMode(false) {}
    };

    ChannelManager              channels_;
    ClientManager               clients_;
    CommandProcessor            cmds_;

    int                         port_;
    std::string                 password_;
    std::string                 serverName_;

    size_t                      numServerSockets_;
    std::vector<struct pollfd>  pollFds_;
    std::map<int, SocketIOData> sockIODataStore_;

    Logger                      logger_;

    void                        setupServerSockets_();
    int                         createServerSocket_(int const domain);
    void                        processServerSocketData_();
    void                        acceptClient_(int const serverFd);
    void                        processClientSockets_();
    void                        storePollFd_(int const socketFd);
    bool checkSocket_(struct pollfd &pfd, SocketIOData &sIOD);
    bool processClientSocketData_(struct pollfd &pfd, SocketIOData &sIOD);
    void setClientInfo_(int fd, Client const **clntPtr, std::string &nick,
                        bool &isReg);
    bool clientDataSanityCheck_(SocketIOData &sIOD, int fd,
                                /* Client const *const client, */
                                std::string const &nickName,
                                bool const         isRegistered);
    void closeSocket_(int const fd);
    void log_(std::string const &msg, Logger::logMode const lm);

    Server(Server const &other);
    Server &operator=(Server const &other);

  public:
    Server(int const port, std::string const &password);

    ~Server();

    std::string const &serverName() const {
        return serverName_;
    };

    void run();
    bool sendMsg(int const fd, std::string const &msg);

    bool tryRegister(Client &client) {
        (void)client;
        return true;
    };
};
