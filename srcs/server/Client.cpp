#include "Client.hpp"

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

// "nick!user@host", or "*" until NICK and USER are both known. The host is
// set by the server from the accepted socket address; until then (or if it
// never is) "localhost" keeps the prefix well-formed for clients.
void Client::sendClosingMsg(const std::string &reason) {
    send("ERROR :Closing link: (" + getPrefix() + ") [" + reason + "]\r\n");
}

std::string Client::getPrefix() const {
    if (!hasIdentity())
        return "*";
    return nick_ + "!" + username_ + "@" +
           (hostname_.empty() ? std::string("localhost") : hostname_);
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

void Client::send(const std::string &msg) {
    sendBuffer_ += msg;
}

void Client::sendReply(const std::string    &serverName,
                       const Reply::Message &msg) {
    sendBuffer_ += Reply::build(serverName, getReplyTarget(), msg);
}

// 431/432/433/464 all arrive before registration completes, so the nick is
// used as soon as there is one.
std::string Client::getReplyTarget() const {
    return nick_.empty() ? "*" : nick_;
}

void Client::appendToRecvBuffer(const std::string &bytes) {
    recvBuffer_ += bytes;
}

bool Client::extractLine(std::string &line) {
    const std::string::size_type pos = recvBuffer_.find('\n');
    if (pos == std::string::npos)
        return false;

    line = recvBuffer_.substr(0, pos);
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    recvBuffer_.erase(0, pos + 1);
    return true;
}

std::size_t Client::recvBufferSize() const {
    return recvBuffer_.size();
}

const std::string &Client::getSendBuffer() const {
    return sendBuffer_;
}

void Client::consumeSendBuffer(std::size_t count) {
    if (count >= sendBuffer_.size())
        sendBuffer_.clear();
    else
        sendBuffer_.erase(0, count);
}

void Client::clearSendBuffer() {
    sendBuffer_.clear();
}

bool Client::hasPendingOutput() const {
    return !sendBuffer_.empty();
}
