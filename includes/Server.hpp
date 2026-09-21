#pragma once

#include "ChannelManager.hpp"   // for ChannelManager
#include "ClientManager.hpp"    // for ClientManager
#include "CommandProcessor.hpp" // for CommandProcessor
#include "Logger.hpp"           // for Logger
#include "Socket.hpp"           // for Socket

#ifdef BONUS
#include "Bot.hpp" // for Bot
#include "TransferManager.hpp"
#endif

#include <poll.h>   // for pollfd
#include <stddef.h> // for size_t
#include <string>   // for string, basic_string
#include <vector>   // for vector

#define SERVER_NAME "irc.local"
#define SERVER_VERSION "1.0"
#define MAX_CONNECTIONQUEUE 512

class Server {
  private:
    std::string serverName_;
    std::string serverVersion_;
    std::string createdAt_;
    int         port_;
    std::string password_;

    Logger logger_;

    Socket           sockets_;
    ChannelManager   channels_;
    ClientManager    clients_;
    CommandProcessor cmds_;

#ifdef BONUS
    struct PollFd {
        struct pollfd pfd;

        PollFd(int fd, short events) {
            pfd.fd = fd;
            pfd.events = events;
            pfd.revents = 0;
        }
    };

    TransferManager tm_;
    Bot             bot_;
    void            processTransferSocket_();
#endif

    size_t                     numServerSockets_;
    std::vector<struct pollfd> pollFds_;

    bool setupServerSockets_();
    int  createServerSocket_(int const domain);
    void processServerSocketData_();
    void acceptClient_(int const serverFd);
    void processClientSockets_();
    bool removeClientSocketPfd_(int const fd, std::string const &msg = "",
                                bool force = false) throw();
    void storePollFd_(int const socketFd, bool srvSocket);
    void removePollFd_(int const fd);
    void log_(std::string const &msg, Logger::logMode const lm);

    Server(Server const &other);
    Server &operator=(Server const &other);

  public:
    Server(int const port, std::string const &password);
    ~Server();

    void run();

    const std::string &getServerName() const;
    const std::string &getServerVersion() const;
    const std::string &getCreatedAt() const;
    const std::string &getPassword() const;
    ChannelManager    &getChannelManager();
    ClientManager     &getClientManager();

    void sendMsg(int const fd, std::string const &msg);

#ifdef BONUS
    TransferManager &getTransferManager();
    Bot             &getBot();
#endif
};
