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
