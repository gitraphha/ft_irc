#pragma once

#include "Client.hpp"

#include <map>

// draft: to be completed by blnunez-
class ChannelManager;

class ClientManager {
    std::map<std::string, Client *> clients_;
    std::map<int, std::string>      fdNick_;

    std::map<std::string, Client *>::iterator
    findClient_(const std::string &name);
    std::map<std::string, Client *>::const_iterator
    findClient_(const std::string &name) const;

    std::map<int, std::string>::iterator       findClient_(int fd);
    std::map<int, std::string>::const_iterator findClient_(int fd) const;

    // prevent copy and copy assignment
    ClientManager(const ClientManager &);
    ClientManager &operator=(const ClientManager &);

  public:
    ~ClientManager();
    ClientManager();

    Client &at(const std::string &name);
    Client &at(int fd);

    bool hasClient(const std::string &name) const;
    bool add(Client &client, int fd);
    void remove(int fd, ChannelManager &cm);
};
