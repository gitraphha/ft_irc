#include "ClientManager.hpp"

#include "Errors.hpp"
#include "Utils.hpp"

ClientManager::ClientManager() {}

std::map<std::string, Client*>::iterator
ClientManager::findClient(const std::string &name) {
    return clients.find(Utils::foldCase(name));
}

std::map<std::string, Client*>::const_iterator
ClientManager::findClient(const std::string &name) const {
    return clients.find(Utils::foldCase(name));
}

// 'at' returns an existing client with name equivalent to `name`,
// or throw an error
Client *ClientManager::at(const std::string &name) {
    std::map<std::string, Client*>::iterator it = findClient(name);
    if (it == clients.end())
        throw Error(Reply::reply(Reply::ERR_NOSUCHNICK, name));
    return it->second;
}

std::map<int, std::string>::iterator ClientManager::findClient(int fd) {
    return fdNick.find(fd);
}

std::map<int, std::string>::const_iterator
ClientManager::findClient(int fd) const {
    return fdNick.find(fd);
}

// 'at' returns an existing client with name equivalent to `name`,
// or throw an error (internal error message)
Client *ClientManager::at(int fd) {
    std::map<int, std::string>::iterator it = findClient(fd);
    if (it == fdNick.end())
        throw Error("can't find fd");
    return clients.at(it->second);
}

bool ClientManager::hasClient(const std::string &name) const {
    return findClient(name) != clients.end();
}

bool ClientManager::add(Client *client, int fd) {
    if (!client || hasClient(client->getNickCaseFolded()))
        return false;
    clients.insert(std::make_pair(client->getNickCaseFolded(), client));
    fdNick.insert(std::make_pair(fd, client->getNickCaseFolded()));
    return true;
}

void ClientManager::remove(int fd) {
    std::map<int, std::string>::iterator it = findClient(fd);
    if (it == fdNick.end())
        return;
    std::string nick = it->second;
    fdNick.erase(it);
    std::map<std::string, Client*>::iterator it_cl = findClient(nick);
    if (it_cl != clients.end())
        clients.erase(it_cl);
}
