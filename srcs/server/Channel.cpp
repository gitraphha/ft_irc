#include "Channel.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <sstream>

// userLimit 0 means no limit is set
Channel::Channel(const std::string &name, const std::string &nameCaseFolded)
    : name_(name), nameCaseFolded_(nameCaseFolded), userLimit_(0),
      createdTime_(std::time(NULL)), topicSetTime_() {}

// ---- Membership (JOIN, PART, REMOVE, KICK) ----------------------------------

// Join causes `client` to join the channel named `name`, creating it if needed.
void Channel::join(Client &client, const std::string &key) {
    if (hasMember(client))
        return;
    bool inviteOnly = modes_.hasMode(Mode::InviteOnly);
    if (inviteOnly && !client.isInvitedTo(nameCaseFolded_))
        throw Error::reply(Reply::ERR_INVITEONLYCHAN, name_);
    if (!this->key_.empty() && this->key_ != key)
        throw Error::reply(Reply::ERR_BADCHANNELKEY, name_);
    if (userLimit_ > 0 && size() >= (size_t)userLimit_)
        throw Error::reply(Reply::ERR_CHANNELISFULL, name_);
    ModeSet newModes;
    if (!size())
        newModes += Mode::ChannelOperator;
    members_.add(client, newModes);
    if (inviteOnly)
        client.removeInvite(nameCaseFolded_);
}

// Part parts `client` from the channel and broadcasts part message to all
// clients.
void Channel::part(Client &client, const std::string &msg) {
    requireMember_(client);
    broadcast(Reply::fromClient(client.getPrefix(), "PART", name_, msg));
    members_.remove(client);
}

// Remove parts `client` from the channel.
void Channel::remove(Client &client) {
    requireMember_(client);
    members_.remove(client);
}

// Kick parts `target` from the channel, if `client` has permissions to do so.
void Channel::kick(Client &client, Client &target, const std::string &msg) {
    requireMember_(client);
    if (!hasMember(target))
        throw Error::reply(Reply::ERR_USERNOTINCHANNEL,
                           target.getNick() + " " + name_);
    if (!clientHasPrivsOver(client, target))
        throw Error::reply(Reply::ERR_CHANOPRIVSNEEDED, name_);
    broadcast(Reply::fromClient(client.getPrefix(), "KICK",
                                name_ + " " + target.getNick(), msg));
    members_.remove(target);
}

bool Channel::hasMember(Client &client) const {
    return members_.has(client);
}

void Channel::requireMember_(Client &client) const {
    if (!hasMember(client))
        throw Error::reply(Reply::ERR_NOTONCHANNEL, name_);
}

// ---- Channel state ----------------------------------------------------------

size_t Channel::size() const {
    return members_.size();
}

const std::string &Channel::getName() const {
    return name_;
}

const std::string &Channel::getNameCaseFolded() const {
    return nameCaseFolded_;
}

int Channel::getUserLimit() const {
    return userLimit_;
}

const std::string &Channel::getTopic() const {
    return topic_;
}

bool Channel::inviteOnly() const {
    return (modes_.hasMode(Mode::InviteOnly));
}

bool Channel::hasTopic() const {
    return !topic_.empty();
}

// ---- Modes (MODE command) ---------------------------------------------------

/*
Returns current modes for RPL_CHANNELMODEIS, ex: "+itk key 5".
*/
std::string Channel::modeString(Client &client) const {
    std::string modes = "+";
    std::string params;
    bool        member = hasMember(client);
    for (size_t i = 0; i < Mode::totalChannelModes; i++) {
        int m = Mode::supportedChannelModes[i];
        if (!modes_.hasMode(m))
            continue;
        modes += static_cast<char>(m);
        if (m == Mode::Key && !key_.empty() && member)
            params += " " + key_;
        else if (m == Mode::UserLimit && userLimit_ > 0 && member) {
            std::ostringstream oss;
            oss << userLimit_;
            params += " " + oss.str();
        }
    }
    return modes + params;
}

/*
Applies a parsed MODE change list to the channel and returns subset of applied
changes.

  - skips List ops (Lists are queries. 324 reply is sent by the caller/handler).
  - rejects any change the client lacks privilege
  - applies mode change
    +k set/validate the channel key (ERR_KEYSET / ERR_INVALIDMODEPARAM)
	+l set the user limit (must be a positive base-10 integer)
	o/h/v/q/a  channel-user modes via `applyModeToMember_`
	i/t  plain flags via `ModeSet::setMode`

Note: ERR_INVALIDMODEPARAM is IRCv3 extension
*/
Mode::ModeChanges Channel::applyChannelModeChanges(Server           &server,
                                                   Client           &client,
                                                   Mode::ModeChanges changes) {
    const std::string                &serverName = server.getServerName();
    Mode::ModeChanges                 applied;
    Mode::ModeChanges::const_iterator it = changes.begin();
    for (; it != changes.end(); it++) {
        if (it->op == Mode::List)
            continue;
        if (!Mode::hasPrivs(client.getNickCaseFolded(),
                            members_.highestUserMode(client), *it)) {
            client.sendReply(serverName,
                             Reply::reply(Reply::ERR_CHANOPRIVSNEEDED, name_));
            continue;
        }
        switch (it->mode) {
            case Mode::Key:
                applyKeyMode_(serverName, client, *it, applied);
                break;
            case Mode::UserLimit:
                applyUserLimitMode_(serverName, client, *it, applied);
                break;
            case Mode::ChannelFounder:
            case Mode::ChannelAdmin:
            case Mode::ChannelOperator:
            case Mode::Halfop:
            case Mode::Voice:
                if (it->arg.empty()) {
                    client.sendReply(
                        serverName,
                        Reply::reply(Reply::ERR_NEEDMOREPARAMS, "MODE"));
                    continue;
                }
                if (applyModeToMember_(server, client, *it))
                    applied.push_back(*it);
                break;
            default:
                modes_.setMode(it->mode, it->op == Mode::Add);
                applied.push_back(*it);
        }
    }
    return applied;
}

void Channel::applyKeyMode_(const std::string &serverName, Client &client,
                            const Mode::ModeChange &change,
                            Mode::ModeChanges      &applied) {
    if (change.op == Mode::Remove) {
        setKey("");
        applied.push_back(change);
        return;
    }
    if (!key_.empty()) {
        client.sendReply(serverName, Reply::reply(Reply::ERR_KEYSET, name_));
        return;
    }
    if (!validateChannelKey_(change.arg)) {
        client.sendReply(
            serverName,
            Reply::reply(
                Reply::ERR_INVALIDMODEPARAM,
                Utils::join3(name_, Mode::toString(change.mode), change.arg),
                "Invalid mode parameter"));
        return;
    }
    setKey(change.arg);
    applied.push_back(change);
}

void Channel::applyUserLimitMode_(const std::string &serverName, Client &client,
                                  const Mode::ModeChange &change,
                                  Mode::ModeChanges      &applied) {
    static const std::string &userLimitError =
        "+l user limit value must be an integer between 1 and 2147483647, "
        "expressed in base 10";
    if (change.op == Mode::Remove) {
        setUserLimit(0);
        applied.push_back(change);
        return;
    }
    int val = Utils::toPosInt(change.arg);
    if (val <= 0) {
        client.sendReply(
            serverName,
            Reply::reply(
                Reply::ERR_INVALIDMODEPARAM,
                Utils::join3(name_, Mode::toString(change.mode), change.arg),
                userLimitError));
        return;
    }
    setUserLimit(val);
    applied.push_back(change);
}

// Broadcast all channel mode changes.
void Channel::broadcastCmodeChanges(Client                  &client,
                                    const Mode::ModeChanges &changes) const {
    if (changes.empty())
        return;
    std::string msg = name_ + " " + Mode::serialiseModeChanges(changes);
    Mode::ModeChanges::const_iterator it = changes.begin();
    for (; it != changes.end(); it++) {
        if (!it->arg.empty())
            msg += " " + it->arg;
    }
    broadcast(Reply::fromClient(client.getPrefix(), "MODE", msg));
}

/*
Applies a channel-user mode change (+o/+v/+h/...) to the member named by
`change.arg`.

Returns true only if the change was applied, so the caller can decide whether
to broadcast it. Returns false after sending relevant error.
*/
bool Channel::applyModeToMember_(Server &server, Client &client,
                                 const Mode::ModeChange &change) {
    try {
        Client &target = server.getClientManager().at(change.arg);
        if (members_.setMode(target, change.mode, change.op == Mode::Add))
            return true;
        client.sendReply(server.getServerName(),
                         Reply::reply(Reply::ERR_USERNOTINCHANNEL,
                                      change.arg + " " + name_));
    } catch (Error &e) {
        client.sendReply(server.getServerName(), e.reply());
    }
    return false;
}

bool Channel::validateChannelKey_(std::string key) {
    return !key.empty() && key[0] != ':' && key.find(' ') == std::string::npos;
}

void Channel::setKey(const std::string &key) {
    key_ = key;
}

void Channel::setUserLimit(int limit) {
    userLimit_ = limit;
}

// ---- Topic (TOPIC command) --------------------------------------------------

/*
Sets the topic of this channel, if the client is allowed to do so.
NOTE: topic_ can be "", so you cannot use Reply::fromClient here as it ignores
trailing if trailing is empty.
*/
void Channel::setTopic(Client &client, const std::string &topic) {
    if (!hasMember(client))
        throw Error::reply(Reply::ERR_NOTONCHANNEL, name_);
    if (modes_.hasMode(Mode::OpOnlyTopic) &&
        !clientIsAtLeast(client, Mode::Halfop))
        throw Error::reply(Reply::ERR_CHANOPRIVSNEEDED, name_);
    topic_ = topic;
    topicSetBy_ = client.getNick();
    topicSetTime_ = std::time(NULL);
    broadcast(":" + client.getPrefix() + " TOPIC " + name_ + " :" + topic_ +
              "\r\n");
}

/*
Sends the channel topic, topicSetBy and topicSetTime to the given client if
topic is set.

Note: RPL_TOPICWHOTIME is IRCv3 extension
*/
void Channel::sendTopic(const std::string &serverName, Client &client) const {
    if (!hasMember(client))
        throw Error::reply(Reply::ERR_NOTONCHANNEL, name_);
    if (topic_.empty()) {
        client.sendReply(serverName, Reply::reply(Reply::RPL_NOTOPIC, name_));
        return;
    }
    client.sendReply(serverName, Reply::reply(Reply::RPL_TOPIC, name_, topic_));
    std::ostringstream oss;
    oss << static_cast<long>(topicSetTime_); // convert to unix epoch in seconds
    client.sendReply(serverName,
                     Reply::reply(Reply::RPL_TOPICWHOTIME,
                                  name_ + " " + topicSetBy_ + " " + oss.str()));
}

// ---- Names / Invite (NAMES, INVITE) -----------------------------------------

// send list of users in the channel to the given client
// Note: If MODE `s` secret channel flag is implemented: check is required so
// non-members receive non-existent channel instead of a leak in client names
void Channel::sendNames(const std::string &serverName, Client &client) const {
    client.sendReply(serverName, Reply::reply(Reply::RPL_NAMREPLY, "= " + name_,
                                              members_.namesList()));
    client.sendReply(serverName, Reply::reply(Reply::RPL_ENDOFNAMES, name_));
}

// throws error when client cannot invite target to the channel
void Channel::checkInvite(Client &client, Client &target) const {
    if (!hasMember(client))
        throw Error::reply(Reply::ERR_NOTONCHANNEL, name_);
    if (hasMember(target))
        throw Error::reply(Reply::ERR_USERONCHANNEL,
                           target.getNick() + " " + name_);
    if (inviteOnly() && !clientIsAtLeast(client, Mode::ChannelOperator))
        throw Error::reply(Reply::ERR_CHANOPRIVSNEEDED, name_);
}

// ---- Privilege checks -------------------------------------------------------

// returns whether the client has at least the given channel privilege.
bool Channel::clientIsAtLeast(Client &client, int mode) const {
    return members_.hasAtLeast(client, mode);
}

bool Channel::clientHasPrivsOver(Client &client, Client &target) const {
    return Mode::channelUserModeHasPrivsOver(members_.highestUserMode(client),
                                             members_.highestUserMode(target));
}

// ---- Broadcasting -----------------------------------------------------------

void Channel::broadcast(const std::string &msg, const Client *skip) const {
    members_.send(msg, skip);
}
