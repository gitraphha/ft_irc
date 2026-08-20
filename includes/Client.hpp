#pragma once

#include <algorithm> // move to .cpp when implemnting all functions
#include <string>
#include <vector>

// draft: to be completed by blnunez-

class Client {
    std::string              nick_;
    std::string              nickCaseFolded_;
    std::string              username_;
    std::string              hostname_;
    std::string              realname_;
    bool                     registered_;
    std::string              recvBuffer_;
    std::string              sendBuffer_;
    // operator status needed fot KILL and REHASH
    bool                     oper_;
    // casefolded name of channels invited to
    std::vector<std::string> invitedTo_;

  public:
    Client() : registered_(false), oper_(false) {};

    std::string getNick() const {
        return nick_;
    }

    std::string getNickCaseFolded() const {
        return nickCaseFolded_;
    }

    bool isRegistered() const {
        return registered_;
    }

    bool isOperator() const {
        return oper_;
    }

    // TODO setters

    bool isInvitedTo(const std::string &chan) const {
        std::vector<std::string>::const_iterator it =
            std::find(invitedTo_.begin(), invitedTo_.end(), chan);
        return it != invitedTo_.end();
    }

    void addInvite(const std::string &name) {
        invitedTo_.push_back(name);
    }

    void removeInvite(const std::string &name) {
        std::vector<std::string>::iterator it =
            std::find(invitedTo_.begin(), invitedTo_.end(), name);
        if (it != invitedTo_.end())
            invitedTo_.erase(it);
    }

    void send(const std::string &msg);
};
