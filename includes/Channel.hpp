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
    std::string name_;
    std::string nameCaseFolded_;
    ModeSet     modes_;
    MemberSet   members_;
    std::string key_;
    ssize_t     userLimit_;
    std::time_t createdTime_;
    std::string topic_;
    std::string topicSetBy_;
    std::time_t topicSetTime_;

  public:
    Channel(const std::string &name, const std::string &nameCaseFolded);

    void   join(Client &client, const std::string &key = "");
    void   part(Client &client, const std::string &msg = "");
    void   quit(Client &client);
    void   kick(Client &client, Client &target, const std::string &msg = "");

    size_t size() const;
    const std::string &getName() const;
    const std::string &getNameCaseFolded() const;
    ssize_t            getUserLimit() const;
    const std::string &getTopic() const;

    void               setTopic(Client &client, const std::string &topic);
    void sendTopic(const std::string &serverName, Client &client) const;
    void sendNames(const std::string &serverName, Client &client) const;
    void checkInvite(Client &client, Client &target) const;

    bool inviteOnly() const;
    bool hasAtLeast(Client &client, int mode) const;
    bool hasAtLeast(Client &client, const ModeSet &required) const;
    bool hasMember(Client &client) const;
    bool hasTopic() const;

    void broadcast(const std::string &msg, const Client *skip = 0) const;
};
