#include "ChannelManager.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

ChannelManager::ChannelManager() {}

Channel &ChannelManager::get(const std::string &name) {
    std::map<std::string, Channel>::iterator it =
        channels.find(Utils::caseFolded(name));
    if (it != channels.end())
        return it->second;
    throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
}

const Channel &ChannelManager::get(const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it =
        channels.find(Utils::caseFolded(name));
    if (it != channels.end())
        return it->second;
    throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
}

bool ChannelManager::has(const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it =
        channels.find(Utils::caseFolded(name));
    return it != channels.end();
}

bool ChannelManager::has(Client *client, const std::string &name) const {
    std::map<std::string, Channel>::const_iterator it =
        channels.find(Utils::caseFolded(name));
    if (it == channels.end())
        return false;
    return it->second.has(client);
}

void ChannelManager::join(Client *client, const std::string &name,
                          const std::string &key) {
    if (!has(name)) {
        std::string nameCaseFolded = Utils::caseFolded(name);
        channels.insert(
            std::make_pair(nameCaseFolded, Channel(name, nameCaseFolded)));
    }
    Channel &channel = get(name);
    channel.join(client, key);
}

void ChannelManager::part(Client *client, const std::string &name) {
    Channel &channel = get(name);
    channel.part(client);
    // if no members remove channel
    if (!channel.size())
        channels.erase(Utils::caseFolded(name));
}

void ChannelManager::remove(const std::string &name) {
    if (!has(name))
        throw Error(Reply::reply(Reply::ERR_NOSUCHCHANNEL, name));
    channels.erase(Utils::caseFolded(name));
}

void ChannelManager::broadcast(const std::string &name, const std::string &msg,
                               const Client *skip) const {
    const Channel &channel = get(name);
    channel.broadcast(msg, skip);
}

void ChannelManager::addInvite(const std::string &name,
                               const std::string &nick) {
    Channel &channel = get(name);
    channel.addInvite(nick);
}

void ChannelManager::removeInvite(const std::string &name,
                                  const std::string &nick) {
    Channel &channel = get(name);
    channel.removeInvite(nick);
}
