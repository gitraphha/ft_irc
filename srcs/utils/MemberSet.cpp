#include "MemberSet.hpp"

#include "Errors.hpp"

#include <utility> // std::make_pair

MemberSet::MemberSet(Client *client) {
    add(client, ModeSet());
}

MemberSet::MemberSet(Client *client, const ModeSet &modes) {
    add(client, modes);
}

void MemberSet::add(Client *client, const ModeSet &modes) {
    members.insert(std::make_pair(client, modes));
}

void MemberSet::remove(Client *client) {
    std::map<Client *, ModeSet>::iterator it = members.find(client);
    if (it != members.end())
    	members.erase(it);
}

void MemberSet::set(Client *client, const ModeSet &modes) {
    std::map<Client *, ModeSet>::iterator it = members.find(client);
    if (it != members.end())
        it->second = modes;
    else
        add(client, modes);
}

size_t MemberSet::size() const {
    return members.size();
}

bool MemberSet::has(Client *client) const {
    return (members.find(client) != members.end());
}

bool MemberSet::hasAtLeast(Client *client, const ModeSet &modes) const {
    std::map<Client *, ModeSet>::const_iterator it = members.find(client);
    if (it == members.end())
        return false;
    return it->second.hasAtLeast(modes);
}

void MemberSet::send(const std::string &msg, const Client *skip) const {
    std::map<Client *, ModeSet>::const_iterator it = members.begin();
    for (; it != members.end(); it++) {
        if (it->first && skip != it->first)
            it->first->send(msg);
    }
}
