#pragma once

#include <algorithm> // move to .cpp when implemnting all functions
#include <string>
#include <vector>

// draft: to be completed by blnunez-

class Client {
    std::string              nick;
    std::string              nickCaseFolded;
    std::string              username;
    std::string              hostname;
    std::string              realname;
    bool                     registered;
    std::string              recvBuffer;
    std::string              sendBuffer;
    // operator status needed fot KILL and REHASH
    bool                     oper;
    // casefolded name of channels invited to
    std::vector<std::string> invitedTo;

  public:
    Client() : registered(false), oper(false) {};

    std::string getNick() const {
        return nick;
    }

    std::string getNickCaseFolded() const {
        return nickCaseFolded;
    }

    bool isRegistered() const {
        return registered;
    }

    bool isOperator() const {
        return oper;
    }

    // TODO setters

    bool isInvitedTo(const std::string &chan) const {
        std::vector<std::string>::const_iterator it =
            std::find(invitedTo.begin(), invitedTo.end(), chan);
        return it != invitedTo.end();
    }

    void addInvite(const std::string &name) {
        invitedTo.push_back(name);
    }

    void removeInvite(const std::string &name) {
        std::vector<std::string>::iterator it =
            std::find(invitedTo.begin(), invitedTo.end(), name);
        if (it != invitedTo.end())
            invitedTo.erase(it);
    }

    void send(const std::string &msg);
};
