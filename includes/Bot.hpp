#ifdef BONUS

#pragma once

#include <string>
#include <vector>

class Client;
class Server;

class Bot {
    std::string name_;
    bool        isBotCommand_(const std::string &param) const;

  public:
    Bot();

    std::string getPrefix(const std::string &serverName) const;
    std::string getVersion(const std::string &serverName) const;

    bool isForBot(Server &server, const std::vector<std::string> &params) const;

    void handleMessage(Server &server, Client &client,
                       const std::string &body) const;
    void handleSend(Server &server, Client &client, const std::string &nick,
                    const std::string &filename) const;
    void handleCTCP(const std::string &serverName, Client &client,
                    const std::string &body) const;

    std::string unknown() const;
    std::string help() const;
};

#endif
