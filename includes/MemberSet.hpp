#pragma once

#include "Client.hpp"
#include "ModeSet.hpp"

#include <map>

/*
 * MemberSet stores Client * and ModeSet
 * The server needs to loop through this and remove client, before freeing
 * Client
*/

class MemberSet {
    std::map<Client *, ModeSet> members;

  public:
    MemberSet() {};
    MemberSet(Client *client);
    MemberSet(Client *client, const ModeSet &modes);

    void    add(Client *client, const ModeSet &modes);
    void    remove(Client *client);
	void    set(Client *client, const ModeSet &modes);

	size_t  size() const;
    bool    has(Client *client) const;
    bool    hasAtLeast(Client *client, const ModeSet &modes) const;

    void    send(const std::string &msg, const Client *skip = 0) const;
};
