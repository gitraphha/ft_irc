#pragma once

#include "Client.hpp"
#include "ModeSet.hpp"

#include <map>
#include <set>
#include <string>

typedef std::map<Client *, ModeSet> MemberMap;

/*
 * MemberSet stores Client * and ModeSet
 * The server needs to loop through this and remove client, before freeing
 * Client
*/

class MemberSet {
    MemberMap members_;

  public:
    MemberSet() {};
    MemberSet(Client &client);
    MemberSet(Client &client, const ModeSet &modes);

    int highestUserMode(Client &client) const;

    void add(Client &client, const ModeSet &modes);
    void remove(Client &client);
    bool setMode(Client &client, int mode, bool add);

    size_t size() const;
    bool   has(Client &client) const;
    bool   hasAtLeast(Client &client, int mode) const;

    void        send(const std::string &msg, const Client *skip = 0) const;
    std::string namesList() const;
    // collect adds every member to `out` (a set, so a client in several
    // channels is only counted once by the caller).
    void collect(std::set<Client *> &out) const;
};
