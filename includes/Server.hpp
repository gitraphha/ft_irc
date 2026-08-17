#pragma once

// draft: to be completed by rhaas

#include "ChannelManager.hpp"
#include "ClientManager.hpp"
#include "CommandProcessor.hpp"

#define SERVER_NAME "irc.local"

class Server {
    ChannelManager    channels;
    ClientManager     clients;
    CommandProcessor  cmds;
    int               port;
    std::string       password;

  public:
	Server(int port, const std::string &password);

    bool tryRegister(Client &client);
};
