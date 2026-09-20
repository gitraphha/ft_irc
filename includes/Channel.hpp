#pragma once

#include <ctime>
#include <set>
#include <string>

class Server;
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
    int         userLimit_;
    std::time_t createdTime_;
    std::string topic_;
    std::string topicSetBy_;
    std::time_t topicSetTime_;

    // Membership
    void requireMember_(Client &client) const;

    // Modes (MODE command)
    bool applyModeToMember_(Server &server, Client &client,
                            const Mode::ModeChange &change);
    void applyKeyMode_(Client &client, const Mode::ModeChange &change,
                       Mode::ModeChanges &applied);
    void applyUserLimitMode_(Client &client, const Mode::ModeChange &change,
                             Mode::ModeChanges &applied);
    static bool validateChannelKey_(std::string key);

  public:
    Channel(const std::string &name, const std::string &nameCaseFolded);

    // Membership (JOIN, PART, REMOVE, KICK)
    void join(Client &client, const std::string &key = "");
    void part(Client &client, const std::string &msg = "");
    void remove(Client &client);
    void kick(Client &client, Client &target, const std::string &msg = "");
    bool hasMember(Client &client) const;

    // Channel state
    size_t             size() const;
    const std::string &getName() const;
    const std::string &getNameCaseFolded() const;
    int                getUserLimit() const;
    std::time_t        getCreatedTime() const;
    bool               inviteOnly() const;
    bool               hasTopic() const;
    const std::string &getTopic() const;

    // Modes (MODE command)
    std::string       modeString(Client &client) const;
    Mode::ModeChanges applyChannelModeChanges(Server &server, Client &client,
                                              Mode::ModeChanges changes);
    void              broadcastCmodeChanges(Client                  &client,
                                            const Mode::ModeChanges &changes) const;
    void              setKey(const std::string &key);
    void              setUserLimit(int limit);

    // Topic (TOPIC command)
    void setTopic(Client &client, const std::string &topic);
    void sendTopic(Client &client) const;

    // Names / Invite (NAMES, INVITE)
    void sendNames(Client &client) const;
    void checkInvite(Client &client, Client &target) const;

    // Privilege checks
    bool clientIsAtLeast(Client &client, int mode) const;
    bool clientHasPrivsOver(Client &client, Client &target) const;

    // Broadcasting
    void broadcast(const std::string &msg, const Client *skip = 0) const;
    void collectMembers(std::set<Client *> &out) const;
};
