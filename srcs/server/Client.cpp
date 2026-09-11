#include "Client.hpp"

#include "Server.hpp"
#include "Utils.hpp"

int Client::getFd() const {
    return fd_;
}

std::string Client::getNick() const {
    return nick_;
}

std::string Client::getNickCaseFolded() const {
    return nickCaseFolded_;
}

std::string Client::getUsername() const {
    return username_;
}

std::string Client::getHostname() const {
    return hostname_;
}

std::string Client::getRealname() const {
    return realname_;
}

bool Client::isRegistered() const {
    return registered_;
}

bool Client::isOperator() const {
    return oper_;
}

bool Client::hasPassAccepted() const {
    return passAccepted_;
}

std::string Client::getPrefix() const {
    if (!hasIdentity())
        return "*";
    return nick_ + "!" + username_ + "@" +
           (hostname_.empty() ? std::string("localhost") : hostname_);
}

// 431/432/433/464 all arrive before registration completes, so the nick is
// used as soon as there is one.
std::string Client::getReplyTarget() const {
    return nick_.empty() ? "*" : nick_;
}

bool Client::hasIdentity() const {
    return !nick_.empty() && !username_.empty();
}

void Client::setNick(const std::string &name) {
    nick_ = name;
    nickCaseFolded_ = Utils::foldCase(name);
}

void Client::setUser(const std::string &user, const std::string &real) {
    username_ = user;
    realname_ = real;
}

void Client::setHostname(const std::string &host) {
    hostname_ = host;
}

void Client::setServer(Server *server) {
    server_ = server;
}

void Client::setRegistered(bool value) {
    registered_ = value;
}

void Client::setPassAccepted(bool value) {
    passAccepted_ = value;
}

void Client::setOperator(bool value) {
    oper_ = value;
}

bool Client::isInvitedTo(const std::string &name) const {
    return invitedTo_.find(Utils::foldCase(name)) != invitedTo_.end();
}

void Client::addInvite(const std::string &name) {
    invitedTo_.insert(Utils::foldCase(name));
}

void Client::removeInvite(const std::string &name) {
    invitedTo_.erase(Utils::foldCase(name));
}

// ---- Send message -----------------------------------------------------------

void Client::send(const std::string &msg) {
    if (server_)
        server_->sendMsg(fd_, msg);
}

void Client::sendReply(const Reply::Message &msg) {
    if (!server_)
        return;
    const std::string &text =
        Reply::build(server_->getServerName(), getReplyTarget(), msg);
    server_->sendMsg(fd_, text);
}

// "nick!user@host", or "*" until NICK and USER are both known. The host is
// set by the server from the accepted socket address; until then (or if it
// never is) "localhost" keeps the prefix well-formed for clients.
void Client::sendClosingMsg(const std::string &reason) {
    send("ERROR :Closing link: (" + getPrefix() + ") [" + reason + "]\r\n");
}
