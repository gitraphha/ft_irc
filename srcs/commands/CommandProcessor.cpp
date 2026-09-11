#include "CommandProcessor.hpp"

#include "Client.hpp"
#include "Errors.hpp"
#include "Handlers.hpp"
#include "Parser.hpp"
#include "Registration.hpp"
#include "Server.hpp"

#include <utility>

CommandProcessor::CommandProcessor() {
    registerCommands();
}

bool CommandProcessor::parse(const std::string &line, std::string &cmd,
                             std::vector<std::string> &params) {
    return Parser::parse(line, cmd, params);
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
    add("NICK", Command(Handlers::nick, true, 0));      // 431, not 461
    add("NOTICE", Command(Handlers::notice, false, 0)); // never replies
    add("OPER", Command(Handlers::oper, false, 1));
    add("PART", Command(Handlers::part, false, 1));
    add("PASS", Command(Handlers::pass, true, 1));
    add("PING", Command(Handlers::ping, true, 0)); // 409, not 461
    add("PONG", Command(Handlers::pong, true, 0));
    add("PRIVMSG", Command(Handlers::privmsg, false, 0)); // 411/412
    add("QUIT", Command(Handlers::quit, true, 0));
    add("REHASH", Command(Handlers::rehash, false, 0, true));
    add("RENAME", Command(Handlers::rename, false, 2));
    add("TOPIC", Command(Handlers::topic, false, 1));
    add("USER", Command(Handlers::user, true, 4));
    add("VERSION", Command(Handlers::version, false, 0));
    add("WHO", Command(Handlers::who, false, 1));
    add("WHOIS", Command(Handlers::whois, false, 1));
}

bool CommandProcessor::sendError_(Client &client, Reply::Code code,
                                  const std::string &params) {
    client.sendReply(Reply::reply(code, params));
    return false;
}

// Run runs this command with the given client/message.
bool CommandProcessor::run(Server &server, Client &client,
                           const std::string &line) {
    std::string              name;
    std::vector<std::string> params;
    if (!parse(line, name, params))
        return false;

    std::map<std::string, Command>::iterator it = commands_.find(name);
    if (it == commands_.end())
        return sendError_(client, Reply::ERR_UNKNOWNCOMMAND, name);
    Command &cmd = it->second;
    if (!client.isRegistered() && !cmd.isUsablePreReg())
        return sendError_(client, Reply::ERR_NOTREGISTERED);
    if (client.isRegistered() &&
        (cmd.handler == Handlers::pass || cmd.handler == Handlers::user))
        return sendError_(client, Reply::ERR_ALREADYREGISTRED);
    if (cmd.getRequiresOp() && !client.isOperator())
        return sendError_(client, Reply::ERR_NOPRIVILEGES);
    if (params.size() < cmd.getMinParams())
        return sendError_(client, Reply::ERR_NEEDMOREPARAMS, name);

    bool exiting = cmd.handler(server, client, params);
    // after each command, see if we can send registration to the client
    if (!exiting && !client.isRegistered())
        exiting = Registration::tryRegister(server, client);
    return exiting;
}

void CommandProcessor::add(const std::string &name, const Command &cmd) {
    commands_.insert(std::make_pair(name, cmd));
}
