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
    std::string              nick;
    std::string              nickCaseFolded;
    std::string              username;
    std::string              hostname;
    std::string              realname;
    bool                     registered;
    std::string              recvBuffer;
    std::string              sendBuffer;
    bool                     passAccepted;
    bool                     oper;
    std::vector<std::string> invitedTo;

  public:
    Client() : registered(false), passAccepted(false), oper(false) {}

    std::string getNick() const {
        return nick;
    }

    std::string getNickCaseFolded() const {
        return nickCaseFolded;
    }

    std::string getUsername() const {
        return username;
    }

    std::string getHostname() const {
        return hostname;
    }

    std::string getRealname() const {
        return realname;
    }

    bool isRegistered() const {
        return registered;
    }

    bool isOperator() const {
        return oper;
    }

    bool hasPassAccepted() const {
        return passAccepted;
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

    void               send(const std::string &msg);
    void               appendToRecvBuffer(const std::string &bytes);
    bool               extractLine(std::string &line);
    size_t             recvBufferSize() const;
    const std::string &getSendBuffer() const;
    void               consumeSendBuffer(size_t count);

    bool               hasPendingOutput() const;
};