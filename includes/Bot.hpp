// #ifdef BONUS

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
    Bot(const std::string &name);

    std::string getPrefix(const std::string &serverName) const;
    std::string getVersion(const std::string &serverName) const;

    bool isForBot(Server &server, const std::vector<std::string> &params) const;
    std::string unknown() const;
    std::string help() const;

	bool handleSend(Server &server, Client &client, std::vector<std::string> words);
    bool handleCTCP(Server &server, Client &client,
                    const std::string &body) const;
    bool handleMessage(const std::string &serverName, Client &client,
                       const std::string &body) const;
};

#endif
