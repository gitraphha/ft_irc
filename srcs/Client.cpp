#include "Client.hpp"

#include <cctype>

static char foldChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static std::string foldCase(const std::string &s) {
    std::string folded(s);
    for (std::string::size_type i = 0; i < folded.size(); i++)
        folded[i] = foldChar(folded[i]);
    return folded;
}

std::string Client::getPrefix() const {
    return nick_ + "!" + username_ + "@" + hostname_;
}

bool Client::hasIdentity() const {
    return !nick_.empty() && !username_.empty();
}

void Client::setNick(const std::string &name) {
    nick_ = name;
    nickCaseFolded_ = foldCase(name);
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

void Client::send(const std::string &msg) {
    sendBuffer_ += msg;
}

void Client::appendToRecvBuffer(const std::string &bytes) {
    recvBuffer_ += bytes;
}

bool Client::extractLine(std::string &line) {
    std::string::size_type pos = recvBuffer_.find('\n');
    if (pos == std::string::npos)
        return false;

    line = recvBuffer_.substr(0, pos);
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    recvBuffer_.erase(0, pos + 1);
    return true;
}

size_t Client::recvBufferSize() const {
    return recvBuffer_.size();
}

const std::string &Client::getSendBuffer() const {
    return sendBuffer_;
}

void Client::consumeSendBuffer(size_t count) {
    if (count >= sendBuffer_.size())
        sendBuffer_.clear();
    else
        sendBuffer_.erase(0, count);
}

bool Client::hasPendingOutput() const {
    return !sendBuffer_.empty();
}
