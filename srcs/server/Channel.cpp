#include "Channel.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

#include <algorithm>

// userLimit -1 means no limit is set
Channel::Channel(const std::string &name, const std::string &nameCaseFolded,
                 const std::string &key)
    : name_(name), nameCaseFolded_(nameCaseFolded), key_(key), userLimit_(-1),
      createdTime_(std::time(NULL)), topicSetTime_() {}

// Join causes `client` to join the channel named `name`, creating it if needed.
void Channel::join(Client *client, const std::string &key) {
    if (hasMember(client))
        throw Error(Reply::reply(Reply::ERR_USERONCHANNEL,
                                 client->getNick() + " " + name_));
    bool inviteOnly = modes_.has(Mode::InviteOnly);
    if (inviteOnly && !client->isInvitedTo(nameCaseFolded_))
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
        client->removeInvite(nameCaseFolded_);
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
void Channel::part(Client *client) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name_));
    members_.remove(client);
}

size_t Channel::size() const {
    return members_.size();
}

const std::string &Channel::getNameCaseFolded() const {
    return nameCaseFolded_;
}

ssize_t Channel::getUserLimit() const {
    return userLimit_;
}

bool Channel::hasMember(Client *client) const {
    return members_.has(client);
}

// hasAtLeast returns whether the client has at least the given channel privilege.
bool Channel::hasAtLeast(Client *client, const ModeSet &required) const {
    return members_.hasAtLeast(client, required);
}

void Channel::broadcast(const std::string &msg, const Client *skip) const {
    members_.send(msg, skip);
}
