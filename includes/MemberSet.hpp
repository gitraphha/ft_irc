#pragma once

#include "Client.hpp"
#include "ModeSet.hpp"

#include <map>

/*
 * MemberSet stores const Client * and ModeSet
 * The server needs to loop through this and remove client, before freeing
 * Client
*/

class MemberSet {
    std::map<const Client *, ModeSet> members;

  public:
    MemberSet() {};
    MemberSet(const Client *client, const ModeSet &modes);

    void add(const Client *client, const ModeSet &modes);
    void remove(const Client *client);
    bool has(const Client *client) const;
    void set(const Client *client, const ModeSet &modes);
};
