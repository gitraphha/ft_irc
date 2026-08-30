#include "ClientManager.hpp"

#include "ChannelManager.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

ClientManager::~ClientManager() {
    std::map<std::string, Client *>::iterator it = clients_.begin();
    for (; it != clients_.end(); it++)
        delete it->second;
}

ClientManager::ClientManager() {}

std::map<std::string, Client *>::iterator
ClientManager::findClient_(const std::string &name) {
    return clients_.find(Utils::foldCase(name));
}

std::map<std::string, Client *>::const_iterator
ClientManager::findClient_(const std::string &name) const {
    return clients_.find(Utils::foldCase(name));
}

// 'at' returns an existing client with name equivalent to `name`,
// or throw an error
Client &ClientManager::at(const std::string &name) {
    std::map<std::string, Client *>::iterator it = findClient_(name);
    if (it == clients_.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHNICK, name));
    return *it->second;
}

std::map<int, std::string>::iterator ClientManager::findClient_(int fd) {
    return fdNick_.find(fd);
}

std::map<int, std::string>::const_iterator
ClientManager::findClient_(int fd) const {
    return fdNick_.find(fd);
}

// 'at' returns an existing client with fd given
// or throw an error (internal error message)
Client &ClientManager::at(int fd) {
    std::map<int, std::string>::iterator it = findClient_(fd);
    if (it == fdNick_.end())
        throw Error("can't find fd");
    return at(it->second);
}

bool ClientManager::hasClient(const std::string &name) const {
    return findClient_(name) != clients_.end();
}

// add should receive `new Client()` and add should take ownership of the pointer
bool ClientManager::add(Client &client, int fd) {
    if (hasClient(client.getNickCaseFolded()))
        return false;
    clients_.insert(std::make_pair(client.getNickCaseFolded(), &client));
    fdNick_.insert(std::make_pair(fd, client.getNickCaseFolded()));
    return true;
}

// kicks client out of every channel before freeing it, so Channel::MemberSet
// is not left pointing to freed memory
void ClientManager::remove(int fd, ChannelManager &cm) {
    std::map<int, std::string>::iterator it = findClient_(fd);
    if (it == fdNick_.end())
        return;
    std::string nick = it->second;
    fdNick_.erase(it);
    std::map<std::string, Client *>::iterator it_cl = findClient_(nick);
    if (it_cl == clients_.end())
        return;
    cm.quitAll(*it_cl->second);
    delete it_cl->second;
    clients_.erase(it_cl);
}
