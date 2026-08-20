#pragma once

#include <map>
#include <string>
#include <vector>

class Server;
class Client;

typedef bool (*Handler)(Server &server, Client &client,
                        std::vector<std::string> &params);

class Command {
    bool usablePreReg_;
    int  minParams_;
    bool requiresOp_;

  public:
    Handler handler;

    Command() {};
    Command(Handler h, bool preReg, int minP, bool op = false)
        : usablePreReg_(preReg), minParams_(minP), requiresOp_(op),
          handler(h) {};

    bool isUsablePreReg() const {
        return usablePreReg_;
    }

    int getMinParams() const {
        return minParams_;
    }

    bool getRequiresOp() const {
        return requiresOp_;
    }
};

class CommandProcessor {
    std::map<std::string, Command> commands_;
    Command                       &at_(const std::string &name);

  public:
    void registerCommands();
    // parser TODO blnunez-
    bool parse(const std::string &line, std::string &name,
               std::vector<std::string> &params);
    bool run(Server &server, Client &client, const std::string &line);
    void add(const std::string &name, const Command &cmd);
};
