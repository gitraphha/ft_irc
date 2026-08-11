#include "Channel.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

#include <algorithm>

// userLimit -1 means no limit is set
Channel::Channel(const std::string &name, const std::string &nameCaseFolded)
    : name(name), nameCaseFolded(nameCaseFolded), userLimit(-1),
      createdTime(std::time(NULL)), topicSetTime() {}

// Join causes `client` to join the channel named `name`, creating it if necessary.
void Channel::join(Client *client, const std::string &key) {
    if (has(client))
        throw Error(Reply::reply(Reply::ERR_USERONCHANNEL,
                                 client->getNick() + " " + name));
    bool inviteOnly = flags.has(Mode::InviteOnly);
    if (inviteOnly && !invited(client->getNickCaseFolded()))
        throw Error(Reply::reply(Reply::ERR_INVITEONLYCHAN, name));
    if (!this->key.empty() && this->key != key)
        throw Error(Reply::reply(Reply::ERR_BADCHANNELKEY, name));
    if (getUserLimit() >= 0 && (ssize_t)size() >= getUserLimit())
        throw Error(Reply::reply(Reply::ERR_CHANNELISFULL, name));
    ModeSet modes;
    if (size() == 0) {
        modes += Mode::ChannelOperator;
        modes += Mode::ChannelFounder;
    }
    members.add(client, modes);
    if (inviteOnly)
        removeInvite(client->getNick());
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
void Channel::part(Client *client) {
	if (!has(client))
		throw Error(Reply::reply(Reply::ERR_USERNOTINCHANNEL, client->getNick()  + " " + name));
    members.remove(client);
}

size_t Channel::size() const {
    return members.size();
}

ssize_t Channel::getUserLimit() const {
    return userLimit;
}

bool Channel::has(Client *client) const {
    return members.has(client);
}

// ClientIsAtLeast returns whether the client has at least the given channel privilege.
bool Channel::clientIsAtLeast(Client *client, const ModeSet &modes) const {
    return members.hasAtLeast(client, modes);
}

void Channel::broadcast(const std::string &msg, const Client *skip) const {
    members.send(msg, skip);
}

// should receive caseFolded nick
bool Channel::invited(const std::string &nick) const {
    std::vector<std::string>::const_iterator it =
        std::find(invites.begin(), invites.end(), nick);
    return it != invites.end();
}

void Channel::addInvite(const std::string &nick) {
    std::string caseFoldedNick = Utils::caseFolded(nick);
    if (!invited(caseFoldedNick))
        invites.push_back(caseFoldedNick);
}

void Channel::removeInvite(const std::string &nick) {
    std::vector<std::string>::const_iterator it =
        std::find(invites.begin(), invites.end(), Utils::caseFolded(nick));
    if (it != invites.end())
        invites.erase(it);
}
