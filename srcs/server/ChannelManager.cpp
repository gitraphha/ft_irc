#include "ChannelManager.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

ChannelManager::ChannelManager() {}

std::map<std::string, Channel>::iterator
ChannelManager::findChannel_(const std::string &name) {
    return channels_.find(Utils::foldCase(name));
}

std::map<std::string, Channel>::const_iterator
ChannelManager::findChannel_(const std::string &name) const {
    return channels_.find(Utils::foldCase(name));
}

// 'at' returns an existing channel with name equivalent to `name`,
// or throw an error
Channel &ChannelManager::at(const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel_(name);
    if (it == channels_.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    return it->second;
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
    std::string                              folded = Utils::foldCase(name);
    std::map<std::string, Channel>::iterator it = channels_.find(folded);
    if (it == channels_.end()) {
        Channel newChannel(name, folded, key);
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
    std::map<std::string, Channel>::iterator it = findChannel_(name);
    if (it == channels_.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    it->second.part(client, msg);
    if (!it->second.size())
        channels_.erase(it);
}

// Parts client from all channels, deleting it if it's empty.
// Broadcasts part message.
void ChannelManager::partAll(Client &client, const std::string &msg) {
    partAll_(client, msg, true);
}

// Parts client from all channels, deleting it if it's empty.
void ChannelManager::quitAll(Client &client) {
    partAll_(client);
}

void ChannelManager::partAll_(Client &client, const std::string &msg,
                              bool announce) {
    std::map<std::string, Channel>::iterator it = channels_.begin();
    while (it != channels_.end()) {
        std::map<std::string, Channel>::iterator cur = it++;
        if (!cur->second.hasMember(client))
            continue;
        if (announce)
            cur->second.part(client, msg);
        else
            cur->second.quit(client);
        if (!cur->second.size())
            channels_.erase(cur);
    }
}

// delete a channel
void ChannelManager::removeChannel(const std::string &name) {
    std::map<std::string, Channel>::iterator it = findChannel_(name);
    if (it == channels_.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    channels_.erase(it);
}

void ChannelManager::broadcast(const std::string &name, const std::string &msg,
                               const Client *skip) {
    Channel &channel = at(name);
    channel.broadcast(msg, skip);
}
