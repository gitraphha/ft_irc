#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>

class Client;
#include "MemberSet.hpp"
#include "ModeSet.hpp"

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
    std::string name;
    std::string nameCaseFolded;
    ModeSet     modes;
    MemberSet   members;
    std::string key;
    ssize_t     userLimit;
    std::time_t createdTime;
    std::string topic;
    std::string topicSetBy;
    std::time_t topicSetTime;

  public:
    Channel(const std::string &name, const std::string &nameCaseFolded,
            const std::string &key = "");

    void               join(Client *client, const std::string &key = "");
    void               part(Client *client);

    size_t             size() const;
    const std::string &getNameCaseFolded() const;
    ssize_t            getUserLimit() const;
    bool               hasMember(Client *client) const;
    bool hasAtLeast(Client *client, const ModeSet &required) const;

    void broadcast(const std::string &msg, const Client *skip = 0) const;
};
