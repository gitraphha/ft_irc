#include "ClientManager.hpp"

#include "ChannelManager.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

ClientManager::ClientManager() {}

ClientManager::~ClientManager() {
    std::map<int, Client *>::iterator it = byFd_.begin();
    for (; it != byFd_.end(); it++)
        delete it->second;
}

Client &ClientManager::at(int fd) {
    std::map<int, Client *>::iterator it = byFd_.find(fd);
    if (it == byFd_.end())
        throw Error("ClientManager: no client for fd");
    return *it->second;
}

Client &ClientManager::at(const std::string &name) {
    std::map<std::string, int>::iterator it =
        byNick_.find(Utils::foldCase(name));
    if (it == byNick_.end())
        throw Error::reply(Reply::ERR_NOSUCHNICK, name);
    return at(it->second);
}

Client *ClientManager::find(int fd) {
    std::map<int, Client *>::iterator it = byFd_.find(fd);
    if (it == byFd_.end())
        return 0;
    return it->second;
}

bool ClientManager::hasClient(int fd) const {
    return byFd_.find(fd) != byFd_.end();
}

bool ClientManager::hasNick(const std::string &name) const {
    return byNick_.find(Utils::foldCase(name)) != byNick_.end();
}

std::size_t ClientManager::size() const {
    return byFd_.size();
}

bool ClientManager::add(int fd, std::string const &hostname, Server &server) {
    if (hasClient(fd))
        return false;
    Client *client = new Client(fd, hostname, &server);
    byFd_.insert(std::make_pair(fd, client));
    if (!client->getNick().empty())
        byNick_.insert(std::make_pair(client->getNickCaseFolded(), fd));
    return true;
}

void ClientManager::setNick(Client &client, const std::string &nick) {
    if (!client.getNick().empty())
        byNick_.erase(client.getNickCaseFolded());
    client.setNick(nick);
    if (!nick.empty())
        byNick_.insert(
            std::make_pair(client.getNickCaseFolded(), client.getFd()));
}

// parts the client from every channel before freeing it, so no MemberSet is
// left pointing to freed memory
void ClientManager::remove(int fd, ChannelManager &cm) {
    std::map<int, Client *>::iterator it = byFd_.find(fd);
    if (it == byFd_.end())
        return;
    Client *client = it->second;
    cm.leaveAll(*client);
    if (!client->getNick().empty())
        byNick_.erase(client->getNickCaseFolded());
    byFd_.erase(it);
    delete client;
}
