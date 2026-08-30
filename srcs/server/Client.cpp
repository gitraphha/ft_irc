#include "Client.hpp"

#include "Utils.hpp"

#include <algorithm>

std::string Client::getPrefix() const {
    if (!hasIdentity() || hostname_.empty())
        return "*";
    return nick_ + "!" + username_ + "@" + hostname_;
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
    const std::string folded = Utils::foldCase(name);
    return std::find(invitedTo_.begin(), invitedTo_.end(), folded) !=
           invitedTo_.end();
}

void Client::addInvite(const std::string &name) {
    const std::string folded = Utils::foldCase(name);
    if (std::find(invitedTo_.begin(), invitedTo_.end(), folded) ==
        invitedTo_.end())
        invitedTo_.push_back(folded);
}

void Client::removeInvite(const std::string &name) {
    const std::string                  folded = Utils::foldCase(name);
    std::vector<std::string>::iterator it =
        std::find(invitedTo_.begin(), invitedTo_.end(), folded);
    if (it != invitedTo_.end())
        invitedTo_.erase(it);
}

void Client::send(const std::string &msg) {
    sendBuffer_ += msg;
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
