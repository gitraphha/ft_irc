#pragma once

#include "Channel.hpp"

#include <map>

class ChannelManager {
    std::map<std::string, Channel> channels;

	std::map<std::string, Channel>::iterator findChannel(const std::string &name);
	std::map<std::string, Channel>::const_iterator findChannel(const std::string &name) const;
    Channel                       &at(const std::string &name);

  public:
    ChannelManager();

    bool hasChannel(const std::string &name) const;
    bool isMember(Client *client, const std::string &name) const;

    void join(Client *client, const std::string &name,
              const std::string &key = "");
    void part(Client *client, const std::string &name);
    void removeChannel(const std::string &name);

    void broadcast(const std::string &name, const std::string &msg,
                   const Client *skip = 0) ;

    void addInvite(const std::string &name, Client *client);
    void removeInvite(const std::string &name, Client *client);
};
