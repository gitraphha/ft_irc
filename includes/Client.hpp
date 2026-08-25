#pragma once

/*
 * Client stores per-connection state only. Server owns sockets and syscalls;
 * command handlers own validation and IRC behavior.
 */

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

class Client {
    std::string              nick_;
    std::string              nickCaseFolded_;
    std::string              username_;
    std::string              hostname_;
    std::string              realname_;
    bool                     registered_;
    std::string              recvBuffer_;
    std::string              sendBuffer_;
    bool                     passAccepted_;
    bool                     oper_;
    std::vector<std::string> invitedTo_;

  public:
    Client() : registered_(false), passAccepted_(false), oper_(false) {}

    std::string getNick() const {
        return nick_;
    }

    std::string getNickCaseFolded() const {
        return nickCaseFolded_;
    }

    std::string getUsername() const {
        return username_;
    }

    std::string getHostname() const {
        return hostname_;
    }

    std::string getRealname() const {
        return realname_;
    }

    bool isRegistered() const {
        return registered_;
    }

    bool isOperator() const {
        return oper_;
    }

    bool hasPassAccepted() const {
        return passAccepted_;
    }

    std::string getPrefix() const;
    bool        hasIdentity() const;
    void        setNick(const std::string &name);
    void        setUser(const std::string &user, const std::string &real);
    void        setHostname(const std::string &host);

    void        setRegistered(bool value);
    void        setPassAccepted(bool value);
    void        setOperator(bool value);

    bool        isInvitedTo(const std::string &chan) const {
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

    void               send(const std::string &msg);
    void               appendToRecvBuffer(const std::string &bytes);
    bool               extractLine(std::string &line);
    size_t             recvBufferSize() const;
    const std::string &getSendBuffer() const;
    void               consumeSendBuffer(size_t count);

    bool               hasPendingOutput() const;
};