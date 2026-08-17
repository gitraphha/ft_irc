#include "CommandProcessor.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Handlers.hpp"
#include "Server.hpp"

Command &CommandProcessor::at(const std::string &name) {
    std::map<std::string, Command>::iterator it = commands.find(name);
    if (it == commands.end())
        throw Error(Reply::reply(Reply::ERR_UNKNOWNCOMMAND, name));
    return it->second;
}

bool CommandProcessor::parse(const std::string &line, std::string &cmd,
                             std::vector<std::string> &params) {
    return false;
}

// Run runs this command with the given client/message.
bool CommandProcessor::run(Server &server, Client &client,
                           const std::string &line) {
    std::string              name;
    std::vector<std::string> params;
    if (!parse(line, name, params))
        return false;

    Command &cmd = at(name);
    if (!client.isRegistered() && !cmd.isUsablePreReg())
        throw Error(Reply::reply(Reply::ERR_NOTREGISTERED, ""));
    if (cmd.getRequiresOp() && !client.isOperator())
        throw Error(Reply::reply(Reply::ERR_NOPRIVILEGES, ""));
    if (params.size() < cmd.getMinParams())
        throw Error(Reply::reply(Reply::ERR_NEEDMOREPARAMS, name));

    bool exiting = cmd.handler(server, client, params);
    // after each command, see if we can send registration to the client
    if (!exiting && !client.isRegistered()) {
        exiting = server.tryRegister(client);
    }
    return exiting;
}

// Register all commandsexecutable by a client connected to us.
void CommandProcessor::registerCommands() {
    add("CAP", Command(Handlers::cap, true, 1));
    add("INVITE", Command(Handlers::invite, false, 2));
    add("JOIN", Command(Handlers::join, false, 1));
    add("KICK", Command(Handlers::kick, false, 2));
    add("KILL", Command(Handlers::kill, false, 1, true));
    add("MODE", Command(Handlers::mode, false, 1));
    add("NAMES", Command(Handlers::names, false, 0));
    add("NICK", Command(Handlers::nick, true, 1));
    add("NOTICE", Command(Handlers::message, false, 2));
    add("OPER", Command(Handlers::oper, false, 1));
    add("PART", Command(Handlers::part, false, 1));
    add("PASS", Command(Handlers::pass, true, 1));
    add("PING", Command(Handlers::ping, true, 1));
    add("PONG", Command(Handlers::pong, true, 1));
    add("PRIVMSG", Command(Handlers::message, false, 2));
    add("QUIT", Command(Handlers::quit, true, 0));
    add("REHASH", Command(Handlers::rehash, false, 0, true));
    add("RENAME", Command(Handlers::rename, false, 2));
    add("TOPIC", Command(Handlers::topic, false, 1));
    add("USER", Command(Handlers::user, true, 4));
    add("VERSION", Command(Handlers::version, false, 0));
    add("WHO", Command(Handlers::who, false, 1));
    add("WHOIS", Command(Handlers::whois, false, 1));
}
