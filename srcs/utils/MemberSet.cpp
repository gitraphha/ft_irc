#include "MemberSet.hpp"

#include "Errors.hpp"

#include <utility> // std::make_pair

MemberSet::MemberSet(Client &client) {
    add(client, ModeSet());
}

MemberSet::MemberSet(Client &client, const ModeSet &modes) {
    add(client, modes);
}

// caller should add key before calling
void MemberSet::add(Client &client, const ModeSet &modes) {
    members_.insert(std::make_pair(&client, modes));
}

void MemberSet::remove(Client &client) {
    std::map<Client *, ModeSet>::iterator it = members_.find(&client);
    if (it != members_.end())
        members_.erase(it);
}

void MemberSet::setModes(Client &client, const ModeSet &modes) {
    std::map<Client *, ModeSet>::iterator it = members_.find(&client);
    if (it == members_.end())
        add(client, modes);
    else
        it->second = modes;
}

const std::map<Client *, ModeSet> MemberSet::getMembers() const {
    return members_;
}

size_t MemberSet::size() const {
    return members_.size();
}

bool MemberSet::has(Client &client) const {
    return (members_.find(&client) != members_.end());
}

bool MemberSet::hasAtLeast(Client &client, int mode) const {
    std::map<Client *, ModeSet>::const_iterator it = members_.find(&client);
    if (it == members_.end())
        return false;
    return it->second.has(mode);
}

bool MemberSet::hasAtLeast(Client &client, const ModeSet &modes) const {
    std::map<Client *, ModeSet>::const_iterator it = members_.find(&client);
    if (it == members_.end())
        return false;
    return it->second.hasAtLeast(modes);
}

void MemberSet::send(const std::string &msg, const Client *skip) const {
    std::map<Client *, ModeSet>::const_iterator it = members_.begin();
    for (; it != members_.end(); it++) {
        if (it->first && skip != it->first)
            it->first->send(msg);
    }
}

std::string MemberSet::namesList() const {
    std::string                                 out;
    std::map<Client *, ModeSet>::const_iterator it = members_.begin();
    for (; it != members_.end(); it++) {
        if (!it->first)
            continue; // shouldn't happen, maybe log error?
        if (it != members_.begin())
            out += " ";
        if (it->second.has(Mode::ChannelOperator))
            out += "@";
        out += it->first->getNick();
    }
    return out;
}
