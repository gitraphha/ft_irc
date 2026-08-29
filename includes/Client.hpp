#pragma once

/*
 * Client stores per-connection state only. Server owns sockets and syscalls;
 * command handlers own validation and IRC behavior.
 */

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

    bool        isInvitedTo(const std::string &name) const;
    void        addInvite(const std::string &name);
    void        removeInvite(const std::string &name);

    void        send(const std::string &msg);
    void        appendToRecvBuffer(const std::string &bytes);
    bool        extractLine(std::string &line);
    std::size_t recvBufferSize() const;
    const std::string &getSendBuffer() const;
    void               consumeSendBuffer(std::size_t count);
    void               clearSendBuffer();

    bool               hasPendingOutput() const;
};
