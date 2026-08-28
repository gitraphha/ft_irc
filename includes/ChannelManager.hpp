#pragma once

#include "Channel.hpp"

#include <map>

class ChannelManager {
    std::map<std::string, Channel> channels_;

    std::map<std::string, Channel>::iterator
    findChannel_(const std::string &name);
    std::map<std::string, Channel>::const_iterator
         findChannel_(const std::string &name) const;
    void part_(Client &client, std::map<std::string, Channel>::iterator);

  public:
    ChannelManager();

    Channel &at(const std::string &name);

    bool     hasChannel(const std::string &name) const;
    bool     isMember(Client &client, const std::string &name) const;

    void     join(Client &client, const std::string &name,
                  const std::string &key = "");
    void     part(Client &client, const std::string &name);
    void     partAll(Client &client);
    void     removeChannel(const std::string &name);

    void broadcast(const std::string &name, const std::string &msg,
                   const Client *skip = 0);
};
