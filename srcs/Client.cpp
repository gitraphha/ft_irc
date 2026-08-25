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
    return nick + "!" + username + "@" + hostname;
}

bool Client::hasIdentity() const {
    return !nick.empty() && !username.empty();
}

void Client::setNick(const std::string &name) {
    nick = name;
    nickCaseFolded = foldCase(name);
}

void Client::setUser(const std::string &user, const std::string &real) {
    username = user;
    realname = real;
}

void Client::setHostname(const std::string &host) {
    hostname = host;
}

void Client::setRegistered(bool value) {
    registered = value;
}

void Client::setPassAccepted(bool value) {
    passAccepted = value;
}

void Client::setOperator(bool value) {
    oper = value;
}

void Client::send(const std::string &msg) {
    sendBuffer += msg;
}

void Client::appendToRecvBuffer(const std::string &bytes) {
    recvBuffer += bytes;
}

bool Client::extractLine(std::string &line) {
    std::string::size_type pos = recvBuffer.find('\n');
    if (pos == std::string::npos)
        return false;

    line = recvBuffer.substr(0, pos);
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);

    recvBuffer.erase(0, pos + 1);
    return true;
}

size_t Client::recvBufferSize() const {
    return recvBuffer.size();
}

const std::string &Client::getSendBuffer() const {
    return sendBuffer;
}

void Client::consumeSendBuffer(size_t count) {
    if (count >= sendBuffer.size())
        sendBuffer.clear();
    else
        sendBuffer.erase(0, count);
}

bool Client::hasPendingOutput() const {
    return !sendBuffer.empty();
}
