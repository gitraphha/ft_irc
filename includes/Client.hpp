#pragma once

#include <string>
#include <vector>

// draft: to be completed by blunez-

class Client {
    std::string nick;
    std::string nickCaseFolded;
    bool        registered;
    bool        oper;
    std::vector<std::string>
        invitedTo; // casefolded name of channels invited to

  public:
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
