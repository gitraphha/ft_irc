#include "Channel.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <sstream>

// userLimit -1 means no limit is set
Channel::Channel(const std::string &name, const std::string &nameCaseFolded)
    : name_(name), nameCaseFolded_(nameCaseFolded), key_(), userLimit_(-1),
      createdTime_(std::time(NULL)), topicSetTime_() {}

// Join causes `client` to join the channel named `name`, creating it if needed.
void Channel::join(Client &client, const std::string &key) {
    if (hasMember(client))
        throw Error(Reply::reply(Reply::ERR_USERONCHANNEL,
                                 client.getNick() + " " + name_));
    bool inviteOnly = modes_.has(Mode::InviteOnly);
    if (inviteOnly && !client.isInvitedTo(nameCaseFolded_))
        throw Error(Reply::reply(Reply::ERR_INVITEONLYCHAN, name_));
    if (!this->key_.empty() && this->key_ != key)
        throw Error(Reply::reply(Reply::ERR_BADCHANNELKEY, name_));
    if (getUserLimit() >= 0 && (ssize_t)size() >= getUserLimit())
        throw Error(Reply::reply(Reply::ERR_CHANNELISFULL, name_));
    ModeSet newModes;
    if (size() == 0) {
        newModes += Mode::ChannelOperator;
        newModes += Mode::ChannelFounder;
    }
    members_.add(client, newModes);
    if (inviteOnly)
        client.removeInvite(nameCaseFolded_);
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
// Broadcasts part message to all clients.
void Channel::part(Client &client, const std::string &msg) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
    broadcast(Reply::fromClient(client.getPrefix(), "PART", name_, msg));
    members_.remove(client);
}

// Quit parts `client` from the channel named `name`, deleting it if it's empty.
void Channel::quit(Client &client) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
    members_.remove(client);
}

void Channel::kick(Client &client, Client &target, const std::string &msg) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
    if (!hasAtLeast(client, Mode::Halfop))
        throw Error(Reply::reply(Reply::ERR_CHANOPRIVSNEEDED, name_));
    if (!hasMember(target))
        throw Error(Reply::reply(Reply::ERR_USERNOTINCHANNEL,
                                 target.getNick() + " " + name_));
    broadcast(Reply::fromClient(client.getPrefix(), "KICK",
                                name_ + " " + target.getNick(), msg));
    members_.remove(target);
}

size_t Channel::size() const {
    return members_.size();
}

const std::string &Channel::getName() const {
    return name_;
}

const std::string &Channel::getNameCaseFolded() const {
    return nameCaseFolded_;
}

ssize_t Channel::getUserLimit() const {
    return userLimit_;
}

const std::string &Channel::getTopic() const {
    return topic_;
}

// sets the topic of this channel, if the client is allowed to do so.
void Channel::setTopic(Client &client, const std::string &topic) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
    if (modes_.has(Mode::OpOnlyTopic) && !hasAtLeast(client, Mode::Halfop))
        throw Error(Reply::reply(Reply::ERR_CHANOPRIVSNEEDED, name_));
    topic_ = topic;
    topicSetBy_ = client.getNick();
    topicSetTime_ = std::time(NULL);
    broadcast(Reply::fromClient(client.getPrefix(), "TOPIC", name_, topic_));
}

// sends the channel topic, topicSetBy and topicSetTime to the given client if
// topic is set.
void Channel::sendTopic(const std::string &serverName, Client &client) const{
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
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

// send list of users in the channel to the given client
// s toggle the secret channel flag -> if added, for non-members treat as
// non-existent channel
void Channel::sendNames(const std::string &serverName, Client &client) const {
    client.sendReply(serverName, Reply::reply(Reply::RPL_NAMREPLY, "= " + name_,
                                              members_.namesList()));
    client.sendReply(serverName, Reply::reply(Reply::RPL_ENDOFNAMES, name_));
}

// throws error when client cannot invite target to the channel
void Channel::checkInvite(Client &client, Client &target) const {
	if (!hasMember(client))
		throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
	if (hasMember(target))
		throw Error(Reply::reply(Reply::ERR_USERONCHANNEL,
							target.getNick() + " " + name_));
	if (inviteOnly() && !hasAtLeast(client, Mode::ChannelOperator))
		throw Error(Reply::reply(Reply::ERR_CHANOPRIVSNEEDED, name_));
}

bool Channel::inviteOnly() const {
    return (modes_.has(Mode::InviteOnly));
}

// hasAtLeast returns whether the client has at least the given channel privilege.
bool Channel::hasAtLeast(Client &client, int mode) const {
    return members_.hasAtLeast(client, mode);
}

bool Channel::hasAtLeast(Client &client, const ModeSet &required) const {
    return members_.hasAtLeast(client, required);
}

bool Channel::hasMember(Client &client) const {
    return members_.has(client);
}

bool Channel::hasTopic() const {
    return !topic_.empty();
}

void Channel::broadcast(const std::string &msg, const Client *skip) const {
    members_.send(msg, skip);
}
