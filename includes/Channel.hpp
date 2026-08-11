#pragma once

#include "Client.hpp"
#include "MemberSet.hpp"
#include "ModeSet.hpp"
#include "Server.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>

/*
 * Hold one channel’s state and membership:
 *
 * name (#general)
 * members (clients) + who is operator
 * topic
 * modes: i, t, k, o (via member ops), l + key/limit values
 * invite list (for +i)
 * helpers: add/remove member, isOp, canJoin (key/limit/invite), broadcast to members
 * Commands like JOIN/KICK/MODE use Channel; they shouldn’t each reinvent that state.
*/

class Channel {
    std::string              name;
    std::string              nameCaseFolded;
    ModeSet                  flags;
    std::vector<std::string> invites;
    MemberSet                members;
    std::string              key;
    ssize_t                  userLimit;
    // const Server *server;
    std::time_t              createdTime;
    std::string              topic;
    std::string              topicSetBy;
    std::time_t              topicSetTime;

    bool                     invited(const std::string &nick) const;

  public:
    Channel(const std::string &name, const std::string &nameCaseFolded);

    void    join(Client *client, const std::string &key = "");
    void    part(Client *client);

    size_t  size() const;
    ssize_t getUserLimit() const;
    bool    has(Client *client) const;
    bool    clientIsAtLeast(Client *client, const ModeSet &modes) const;

    void    broadcast(const std::string &msg, const Client *skip = 0) const;

    void    addInvite(const std::string &nick);
    void    removeInvite(const std::string &nick);
};
