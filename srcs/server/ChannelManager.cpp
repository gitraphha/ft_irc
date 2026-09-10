#include "ChannelManager.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

#include <set>

ChannelManager::ChannelManager() {}

std::map<std::string, Channel>::iterator
ChannelManager::findChannel_(const std::string &name) {
    return channels_.find(Utils::foldCase(name));
}

std::map<std::string, Channel>::const_iterator
ChannelManager::findChannel_(const std::string &name) const {
    return channels_.find(Utils::foldCase(name));
}

std::map<std::string, Channel>::iterator
ChannelManager::findChannelOrThrow_(const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel_(name);
    if (it == channels_.end())
        throw Error::reply(Reply::ERR_NOSUCHCHANNEL, name);
    return it;
}

std::map<std::string, Channel>::const_iterator
ChannelManager::findChannelOrThrow_(const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it = findChannel_(name);
    if (it == channels_.end())
        throw Error::reply(Reply::ERR_NOSUCHCHANNEL, name);
    return it;
}

/*
Channels names are strings (beginning with a '&', '#', '+' or '!' character) of
length up to fifty (50) characters. It SHALL NOT contain any spaces (' '), a
control G (^G or ASCII 7), a comma (','). Space is used as parameter separator
and command is used as a list item separator by the protocol). A colon (':') can
also be used as a delimiter for the channel mask. Channel names are case
insensitive.

Note: * and ? added for mask safety
*/
bool ChannelManager::isChannelName(const std::string &name) {
    if (name.empty() || name.size() > 50 || name.find_first_of("&#+!") != 0)
        return false;
    return name.find_first_of(" \a,:*?") == std::string::npos;
}

void ChannelManager::cleanup_(std::map<std::string, Channel>::iterator it) {
    if (!it->second.size())
        channels_.erase(it);
}

// 'at' returns an existing channel with name equivalent to `name`,
// or throw an error
Channel &ChannelManager::at(const std::string &name) {
    return findChannelOrThrow_(name)->second;
}

const Channel &ChannelManager::at(const std::string &name) const {
    return findChannelOrThrow_(name)->second;
}

bool ChannelManager::hasChannel(const std::string &name) const {
    return findChannel_(name) != channels_.end();
}

bool ChannelManager::isMember(Client &client, const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it = findChannel_(name);
    return (it != channels_.end() && it->second.hasMember(client));
}

// Join causes `client` to join the channel named `name`, creating it if needed.
void ChannelManager::join(Client &client, const std::string &name,
                          const std::string &key) {
    if (!isChannelName(name))
        throw Error::reply(Reply::ERR_NOSUCHCHANNEL, name);
    std::string                              folded = Utils::foldCase(name);
    std::map<std::string, Channel>::iterator it = channels_.find(folded);
    if (it == channels_.end()) {
        Channel newChannel(name, folded);
        newChannel.join(client, key);
        channels_.insert(std::make_pair(folded, newChannel));
        return;
    }
    it->second.join(client, key);
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
// Broadcasts part message to all clients.
void ChannelManager::part(Client &client, const std::string &name,
                          const std::string &msg) {
    std::map<std::string, Channel>::iterator it = findChannelOrThrow_(name);
    it->second.part(client, msg);
    cleanup_(it);
}

void ChannelManager::kick(Client &client, const std::string &name,
                          Client &target, const std::string &msg) {
    std::map<std::string, Channel>::iterator it = findChannelOrThrow_(name);
    it->second.kick(client, target, msg);
    cleanup_(it);
}

// Parts client from all channels, deleting it if it's empty.
// Broadcasts part message if `announce` is true.
void ChannelManager::leaveAll(Client &client, const std::string &msg,
                              bool announce) {
    std::map<std::string, Channel>::iterator it = channels_.begin();
    while (it != channels_.end()) {
        std::map<std::string, Channel>::iterator cur = it++;
        if (!cur->second.hasMember(client))
            continue;
        if (announce)
            cur->second.part(client, msg);
        else
            cur->second.remove(client);
        cleanup_(cur);
    }
}

// delete a channel
void ChannelManager::removeChannel(const std::string &name) {
    channels_.erase(findChannelOrThrow_(name));
}

void ChannelManager::broadcast(const std::string &name, const std::string &msg,
                               const Client *skip) const {
    const Channel &channel = at(name);
    channel.broadcast(msg, skip);
}

void ChannelManager::broadcastToPeers(Client            &client,
                                      const std::string &msg) const {
    std::set<Client *> peers;
    for (std::map<std::string, Channel>::const_iterator it = channels_.begin();
         it != channels_.end(); ++it)
        if (it->second.hasMember(client))
            it->second.collectMembers(peers);
    peers.erase(&client);
    for (std::set<Client *>::iterator it = peers.begin(); it != peers.end();
         ++it)
        (*it)->send(msg);
}
