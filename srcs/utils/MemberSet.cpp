#include "MemberSet.hpp"

#include <utility> // std::make_pair

MemberSet::MemberSet(const Client *client, const ModeSet &modes) {
    add(client, modes);
}

void MemberSet::add(const Client *client, const ModeSet &modes) {
    members.insert(std::make_pair(client, modes));
}

void MemberSet::remove(const Client *client) {
    std::map<const Client *, ModeSet>::iterator it = members.find(client);
    if (it != members.end())
        members.erase(it);
}

bool MemberSet::has(const Client *client) const {
    return (members.find(client) != members.end());
}

void MemberSet::set(const Client *client, const ModeSet &modes) {
    std::map<const Client *, ModeSet>::iterator it = members.find(client);
    if (it != members.end())
        it->second = modes;
    else
        add(client, modes);
}
