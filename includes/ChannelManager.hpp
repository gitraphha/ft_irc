#pragma once

#include "Channel.hpp"

#include <map>

typedef std::map<std::string, Channel> ChannelMap;

class ChannelManager {
    ChannelMap channels_;

    ChannelMap::iterator       findChannel_(const std::string &name);
    ChannelMap::const_iterator findChannel_(const std::string &name) const;
    ChannelMap::iterator       findChannelOrThrow_(const std::string &name);
    ChannelMap::const_iterator
                findChannelOrThrow_(const std::string &name) const;
    static bool isChannelName_(const std::string &name);
    void        cleanup_(ChannelMap::iterator it);

  public:
    ChannelManager();

    Channel       &at(const std::string &name);
    const Channel &at(const std::string &name) const;

    bool hasChannel(const std::string &name) const;
    bool isMember(Client &client, const std::string &name) const;

    void join(Client &client, const std::string &name,
              const std::string &key = "");
    void part(Client &client, const std::string &name,
              const std::string &msg = "");
    void kick(Client &client, const std::string &name, Client &target,
              const std::string &msg = "");
    void leaveAll(Client &client, const std::string &msg = "",
                  bool announce = false);
    void removeChannel(const std::string &name);

    void broadcast(const std::string &name, const std::string &msg,
                   const Client *skip = 0) const;
};
