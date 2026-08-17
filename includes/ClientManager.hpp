#pragma once

#include "Client.hpp"

#include <map>

// draft: to be completed by blnunez-

class ClientManager {
    std::map<std::string, Client *> clients;
    std::map<int, std::string>      fdNick;

    std::map<std::string, Client *>::iterator
    findClient(const std::string &name);
    std::map<std::string, Client *>::const_iterator
    findClient(const std::string &name) const;

    std::map<int, std::string>::iterator       findClient(int fd);
    std::map<int, std::string>::const_iterator findClient(int fd) const;

    // prevent copy and copy assignment
    ClientManager(const ClientManager &);
    ClientManager &operator=(const ClientManager &);

  public:
    ~ClientManager();
    ClientManager();

    Client *at(const std::string &name);
    Client *at(int fd);

    bool    hasClient(const std::string &name) const;
    bool    add(Client *client, int fd);
    void    remove(int fd);
};
