#pragma once

/*
 * ClientManager owns every connected Client, registered or not.
 *
 * A connection has a socket fd from the moment accept() returns, but no
 * nickname until NICK is processed, and the nickname can change afterwards.
 * Clients are therefore keyed by fd; the nickname index (case-folded per
 * RFC 2812 section 2.2, see Utils::foldCase) is maintained on top of it and
 * only holds clients that currently have a nick.
 *
 *   accept()      -> add(fd, new Client(fd))     takes ownership
 *   NICK          -> setNick(client, "bob")      updates the index
 *   disconnect    -> channels.leaveAll(client)   part first, so no channel
 *                    remove(fd)                  keeps a freed Client *
 *   PRIVMSG bob   -> at("bob")                   throws ERR_NOSUCHNICK
 *   poll loop     -> find(fd)                    NULL if unknown, never throws
 */

#include "Client.hpp"

#include <cstddef>
#include <map>

class ChannelManager;
#include <string>

class ClientManager {
    std::map<int, Client *>    byFd_;
    std::map<std::string, int> byNick_; // case-folded nick -> fd

    // prevent copy and copy assignment
    ClientManager(const ClientManager &);
    ClientManager &operator=(const ClientManager &);

  public:
    ClientManager();
    ~ClientManager();

    // at(fd) returns the client owning `fd`, or throws an internal Error.
    Client &at(int fd);
    // at(name) returns the client whose nick is equivalent to `name`,
    // or throws Error(ERR_NOSUCHNICK).
    Client &at(const std::string &name);
    // find returns the client owning `fd`, or NULL. Never throws.
    Client *find(int fd);

    bool        hasClient(int fd) const;
    bool        hasNick(const std::string &name) const;
    std::size_t size() const;

    // add takes a file descriptor of a newly created socket
	// returns false if `fd` is already present.
    bool add(int fd);
    // setNick changes `client`'s nick and keeps the nick index in sync.
    // The caller validates the nick and checks hasNick() first.
    void setNick(Client &client, const std::string &nick);
    // remove deletes the client owning `fd`; no-op if unknown. The caller
    // parts it from its channels first (ChannelManager::leaveAll).
    // Only Server calls this: it parts the client from every channel, frees
    // it, and the caller has to close the socket too.
    void remove(int fd, ChannelManager &cm);
};
