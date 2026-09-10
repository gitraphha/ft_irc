#pragma once

/*
 * Client stores per-connection state only. Server owns sockets and syscalls;
 * command handlers own validation and IRC behavior.
 */

#include "Reply.hpp"

#include <cstddef>
#include <set>
#include <string>

class Client {
    int                   fd_;
    std::string           nick_;
    std::string           nickCaseFolded_;
    std::string           username_;
    std::string           hostname_;
    std::string           realname_;
    bool                  registered_;
    std::string           recvBuffer_;
    std::string           sendBuffer_;
    bool                  passAccepted_;
    bool                  oper_;
    std::set<std::string> invitedTo_;

  public:
    Client()
        : fd_(-1), registered_(false), passAccepted_(false), oper_(false) {}

    explicit Client(int fd)
        : fd_(fd), registered_(false), passAccepted_(false), oper_(false) {}

    int         getFd() const;
    std::string getNick() const;
    std::string getNickCaseFolded() const;
    std::string getUsername() const;
    std::string getHostname() const;
    std::string getRealname() const;

    bool isRegistered() const;
    bool isOperator() const;
    bool hasPassAccepted() const;

    std::string getPrefix() const;
    // Nick the numerics are addressed to: "*" until the client has one.
    std::string getReplyTarget() const;
    // "ERROR :Closing link: (nick!user@host) [reason]", the line a client gets
    // right before its connection is closed (QUIT, wrong PASS, shutdown).
    void sendClosingMsg(const std::string &reason);
    bool hasIdentity() const;
    void setNick(const std::string &name);
    void setUser(const std::string &user, const std::string &real);
    void setHostname(const std::string &host);

    void setRegistered(bool value);
    void setPassAccepted(bool value);
    void setOperator(bool value);

    bool isInvitedTo(const std::string &name) const;
    void addInvite(const std::string &name);
    void removeInvite(const std::string &name);

    void send(const std::string &msg);
    void sendReply(const std::string &serverName, const Reply::Message &msg);
    void appendToRecvBuffer(const std::string &bytes);
    bool extractLine(std::string &line);
    std::size_t        recvBufferSize() const;
    const std::string &getSendBuffer() const;
    void               consumeSendBuffer(std::size_t count);
    void               clearSendBuffer();

    bool hasPendingOutput() const;
};
