#pragma once

/*
 * Client stores per-connection state only. Server owns sockets and syscalls;
 * command handlers own validation and IRC behavior.
 */

#include "Reply.hpp"

#include <cstddef>
#include <ctime>
#include <map>
#include <string>

class Server;

class Client {
    Server                            *server_;
    int                                fd_;
    std::string                        nick_;
    std::string                        nickCaseFolded_;
    std::string                        username_;
    std::string                        hostname_;
    std::string                        realname_;
    bool                               registered_;
    bool                               passAccepted_;
    bool                               oper_;
    std::map<std::string, std::time_t> invitedTo_;

  public:
    Client()
        : server_(0), fd_(-1), registered_(false), passAccepted_(false),
          oper_(false) {}

    explicit Client(int fd, std::string const &hostname, Server *server)
        : server_(server), fd_(fd), hostname_(hostname), registered_(false),
          passAccepted_(false), oper_(false) {}

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
    bool        hasIdentity() const;
    void        setNick(const std::string &name);
    void        setUser(const std::string &user, const std::string &real);
    void        setHostname(const std::string &host);
    void        setServer(Server *server);

    void setRegistered(bool value);
    void setPassAccepted(bool value);
    void setOperator(bool value);

    // invites are bound to channel incarnation time.
    // an invite from a channel channel that was destoyed and recreated does
    // not admit anyone into the new channel
    bool isInvitedTo(const std::string &name, std::time_t createdTime) const;
    void addInvite(const std::string &name, std::time_t createdTime);
    void removeInvite(const std::string &name);

    // all send routed to `Server::sendMsg()`
    void send(const std::string &msg);
    void sendReply(const Reply::Message &msg);
    // "ERROR :Closing link: (nick!user@host) [reason]", the line a client gets
    // right before its connection is closed (QUIT, wrong PASS, shutdown).
    void sendClosingMsg(const std::string &reason);
};
