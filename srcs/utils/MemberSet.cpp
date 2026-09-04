#include "MemberSet.hpp"

#include "Errors.hpp"

#include <utility> // std::make_pair

MemberSet::MemberSet(Client &client) {
    add(client, ModeSet());
}

MemberSet::MemberSet(Client &client, const ModeSet &modes) {
    add(client, modes);
}

int MemberSet::highestUserMode(Client &client) const {
    MemberMap::const_iterator it = members_.find(&client);
    if (it == members_.end())
        return Mode::NoMode;
    return it->second.highestUserMode();
}

// caller should add key before calling
void MemberSet::add(Client &client, const ModeSet &modes) {
    members_.insert(std::make_pair(&client, modes));
}

void MemberSet::remove(Client &client) {
    MemberMap::iterator it = members_.find(&client);
    if (it != members_.end())
        members_.erase(it);
}

// returns true if the client exists on hte channel
bool MemberSet::setMode(Client &client, int mode, bool add) {
    MemberMap::iterator it = members_.find(&client);
    if (it == members_.end())
        return false;
    it->second.setMode(mode, add);
    return true;
}

size_t MemberSet::size() const {
    return members_.size();
}

bool MemberSet::has(Client &client) const {
    return (members_.find(&client) != members_.end());
}

bool MemberSet::hasAtLeast(Client &client, int mode) const {
    MemberMap::const_iterator it = members_.find(&client);
    if (it == members_.end())
        return false;
    for (size_t i = 0; i < Mode::totalChannelUserModes; i++) {
        int m = Mode::channelUserModes[i];
        if (it->second.hasMode(m))
            return true;
        if (m == mode)
            return false;
    }
    return false;
}

void MemberSet::send(const std::string &msg, const Client *skip) const {
    MemberMap::const_iterator it = members_.begin();
    for (; it != members_.end(); it++) {
        if (it->first && skip != it->first)
            it->first->send(msg);
    }
}

// returns the list of users joined to the channel
std::string MemberSet::namesList() const {
    std::string                                 out;
    MemberMap::const_iterator it = members_.begin();
    for (; it != members_.end(); it++) {
        if (!it->first)
            continue; // shouldn't happen, maybe log error?
        if (it != members_.begin())
            out += " ";
        size_t total = Mode::totalChannelUserModes;
        for (size_t i = 0; i < total; i++) {
            int m = Mode::channelUserModes[i];
            if (it->second.hasMode(m)) {
                out += Mode::channelModePrefix(m);
                break;
            }
        }
        out += it->first->getNick();
    }
    return out;
}

void MemberSet::collect(std::set<Client *> &out) const {
    for (std::map<Client *, ModeSet>::const_iterator it = members_.begin();
         it != members_.end(); ++it)
        out.insert(it->first);
}
