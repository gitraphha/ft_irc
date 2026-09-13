#pragma once

#include <map>
#include <string>
#include <vector>

class Server;
class Client;

typedef bool (*Handler)(Server &server, Client &client,
                        const std::vector<std::string> &params);

class Command {
    bool   usablePreReg_;
    size_t minParams_;
    bool   requiresOp_;

  public:
    Handler handler;

    Command(Handler h, bool preReg, size_t minP, bool op = false)
        : usablePreReg_(preReg), minParams_(minP), requiresOp_(op),
          handler(h) {};

    bool isUsablePreReg() const {
        return usablePreReg_;
    }

    size_t getMinParams() const {
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
    CommandProcessor() {
        registerCommands();
    }

    void registerCommands();
    // parser TODO blnunez-
    bool parse(const std::string &line, std::string &name,
               std::vector<std::string> &params);
    bool run(Server &server, Client &client, const std::string &line);
    void add(const std::string &name, const Command &cmd);

    void inputTooLong(Server &srv, int const fd) {
        (void)srv;
        (void)fd;
        // tbd.
    };
};
