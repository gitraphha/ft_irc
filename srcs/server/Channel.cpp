#include "Channel.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

#include <algorithm>

// userLimit -1 means no limit is set
Channel::Channel(const std::string &name, const std::string &nameCaseFolded, const std::string &key)
    : name(name), nameCaseFolded(nameCaseFolded), key(key), userLimit(-1),
      createdTime(std::time(NULL)), topicSetTime() {}

// Join causes `client` to join the channel named `name`, creating it if needed.
void Channel::join(Client *client, const std::string &key) {
    if (hasMember(client))
        throw Error(Reply::reply(Reply::ERR_USERONCHANNEL,
                                 client->getNick() + " " + name));
    bool inviteOnly = modes.has(Mode::InviteOnly);
    if (inviteOnly && !isInvited(client))
        throw Error(Reply::reply(Reply::ERR_INVITEONLYCHAN, name));
    if (!this->key.empty() && this->key != key)
        throw Error(Reply::reply(Reply::ERR_BADCHANNELKEY, name));
    if (getUserLimit() >= 0 && (ssize_t)size() >= getUserLimit())
        throw Error(Reply::reply(Reply::ERR_CHANNELISFULL, name));
    ModeSet newModes;
    if (size() == 0) {
        newModes += Mode::ChannelOperator;
        newModes += Mode::ChannelFounder;
    }
    members.add(client, newModes);
    if (inviteOnly)
        removeInvite(client);
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
void Channel::part(Client *client) {
    if (!hasMember(client))
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, name));
    members.remove(client);
}

size_t Channel::size() const {
    return members.size();
}

const std::string &Channel::getNameCaseFolded() const {
    return nameCaseFolded;
}

ssize_t Channel::getUserLimit() const {
    return userLimit;
}

bool Channel::hasMember(Client *client) const {
    return members.has(client);
}

// hasAtLeast returns whether the client has at least the given channel privilege.
bool Channel::hasAtLeast(Client *client, const ModeSet &required) const {
    return members.hasAtLeast(client, required);
}

void Channel::broadcast(const std::string &msg, const Client *skip) const {
    members.send(msg, skip);
}

bool Channel::isInvited(const Client *client) const {
    std::vector<Client *>::const_iterator it =
        std::find(invites.begin(), invites.end(), client);
    return it != invites.end();
}

void Channel::addInvite(Client *client) {
    if (!isInvited(client))
        invites.push_back(client);
}

void Channel::removeInvite(const Client *client) {
    std::vector<Client *>::iterator it =
        std::find(invites.begin(), invites.end(), client);
    if (it != invites.end())
        invites.erase(it);
}
