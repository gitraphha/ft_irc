#pragma once

#include "Channel.hpp"

#include <map>

class ChannelManager {
    std::map<std::string, Channel> channels;
    // Get returns an existing channel with name equivalent to `name`, or null
    // will this work with Channel& instead of Channel*?
    Channel                       &get(const std::string &name);
    const Channel                 &get(const std::string &name) const;

  public:
    ChannelManager();

    bool has(const std::string &name) const;
    bool has(Client *client, const std::string &name) const;
    // Join causes `client` to join the channel named `name`, creating it if necessary.
    void join(Client *client, const std::string &name,
              const std::string &key = "");
    // Part parts `client` from the channel named `name`, deleting it if it's empty.
    void part(Client *client, const std::string &name);
    void remove(const std::string &name);
    void broadcast(const std::string &name, const std::string &msg,
                   const Client *skip = 0) const;

    void addInvite(const std::string &name, const std::string &nick);
    void removeInvite(const std::string &name, const std::string &nick);
};
