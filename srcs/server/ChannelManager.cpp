#include "ChannelManager.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

ChannelManager::ChannelManager() {}

std::map<std::string, Channel>::iterator
ChannelManager::findChannel(const std::string &name) {
    return channels.find(Utils::foldCase(name));
}

std::map<std::string, Channel>::const_iterator
ChannelManager::findChannel(const std::string &name) const {
    return channels.find(Utils::foldCase(name));
}

// 'at' returns an existing channel with name equivalent to `name`,
// or throw an error
Channel &ChannelManager::at(const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel(name);
    if (it == channels.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    return it->second;
}

bool ChannelManager::hasChannel(const std::string &name) const {
    return findChannel(name) != channels.end();
}

bool ChannelManager::isMember(Client *client, const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it = findChannel(name);
    return (it != channels.end() && it->second.hasMember(client));
}

// Join causes `client` to join the channel named `name`, creating it if needed.
void ChannelManager::join(Client *client, const std::string &name,
                          const std::string &key) {
    std::string                              folded = Utils::foldCase(name);
    std::map<std::string, Channel>::iterator it = channels.find(folded);
    if (it == channels.end()) {
        Channel newChannel(name, folded, key);
        newChannel.join(client, key);
        channels.insert(std::make_pair(folded, newChannel));
        return;
    }
    it->second.join(client, key);
}

// Part parts `client` from the channel named `name`, deleting it if it's empty.
void ChannelManager::part(Client *client, const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel(name);
    if (it == channels.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    it->second.part(client);
    if (!it->second.size())
        channels.erase(it);
}

// delete a channel
void ChannelManager::removeChannel(const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel(name);
    if (it == channels.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    channels.erase(it);
}

void ChannelManager::broadcast(const std::string &name, const std::string &msg,
                               const Client *skip) {
    Channel &channel = at(name);
    channel.broadcast(msg, skip);
}
