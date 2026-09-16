#ifdef BONUS

#include "Bot.hpp"

#include "CTCP.hpp"
#include "Client.hpp"
#include "Errors.hpp"
#include "Reply.hpp"
#include "Server.hpp"
#include "TransferManager.hpp"
#include "Utils.hpp"

#include <sstream>

Bot::Bot() : name_("DCCBot") {}

std::string Bot::getPrefix(const std::string &serverName) const {
    return name_ + "!bot@" + serverName;
}

std::string Bot::getVersion(const std::string &serverName) const {
    return "I am " + name_ + " on " + serverName;
}

/*
PRIVMSG <target> <cmd>

Targeted to bot (bot command or ctcp)
	bot <cmd> / #bot <cmd> / dccbot <ctcp>
Bot command
	#chan <bot_cmd>

NOTE: Should be called only when params > 1 && !params[0].empty() && !params[1].empty().
This will intercept any user with nickname bot / BOT / #bot -> but OK for bonus.
*/
bool Bot::isForBot(Server                         &server,
                   const std::vector<std::string> &params) const {
    const std::string &target = Utils::foldCase(params[0]);
    if (target == Utils::foldCase(name_) || target == "bot" || target == "#bot")
        return true;
    return (server.getChannelManager().isChannelName(params[0]) &&
            isBotCommand_(params[1]));
}

bool Bot::isBotCommand_(const std::string &body) const {
    if (body.empty())
        return false;
    std::vector<std::string> words = Utils::split(body, ' ');
    std::string cmd = Utils::trimPrefix(Utils::foldCase(words[0]), "!");
    if (cmd.empty())
        return false;

    static const std::string botCmds[] = {"help", "version", "whoami",
                                          "send", "accept",  "reject"};
    static const size_t      totalbotCmds = sizeof(botCmds) / sizeof(*botCmds);
    for (size_t i = 0; i < totalbotCmds; i++) {
        if (botCmds[i] == cmd)
            return true;
    }
    return false;
}

// should reply "Do you mean xxx" -> like a compiler.
// make this a bit smarter?
std::string Bot::unknown() const {
    return "Unknown command. Try !help";
}

std::string Bot::help() const {
    return "Commands: !help, !version, !whoami, !send, !accept, !reject";
}

/*
bot !send <nick> <file>
	bot is initiator, opens a listener,
	replies buildCTCP("DCC SEND ...") in PRIVMSG to requester

NOTE: DCC receiver connects to the offer IP.
127.0.0.1 macOS returns broken accepted socket (SO_ERROR=9 / POLLHUP) when a
process connects to its own LAN IP, which breaks the transfers for same-host
receivers
*/
void Bot::handleSend(Server &server, Client &client, const std::string &nick,
                     const std::string &filename) const {
    try {
        Client          &target = server.getClientManager().at(nick);
        TransferManager &tm = server.getTransferManager();
        if (!tm.openOffer(&client, &target, filename)) {
            client.send(Reply::fromClient(
                getPrefix(server.getServerName()), "PRIVMSG", target.getNick(),
                "bot busy or no such file under 'files/' or socket failed"));
            // TODO make error specific with enum
            return;
        }
        Transfer          *t = tm.current();
        std::ostringstream dcc;
        dcc << "DCC SEND " << t->filename << " 127.0.0.1 " << t->port << " "
            << t->filesize;
        std::string msg = CTCP::buildCTCP(dcc.str());
        target.send(Reply::fromClient(getPrefix(server.getServerName()),
                                      "PRIVMSG", target.getNick(), msg));
        client.send(Reply::fromClient(getPrefix(server.getServerName()),
                                      "PRIVMSG", target.getNick(),
                                      "offer sent to " + nick));
    } catch (Error &e) {
        if (e.hasReply())
            client.sendReply(e.reply());
    }
}

void Bot::handleCTCP(const std::string &serverName, Client &client,
                     const std::string &body) const {
    std::string              ctcp = CTCP::extractCTCP(body);
    std::vector<std::string> words = Utils::split(ctcp, ' ');
    const std::string &text = Utils::foldCase(words.empty() ? "" : words[0]);
    std::string        msg;
    if (text == "version")
        msg = getVersion(serverName);
    else if (text.compare(0, 4, "ping") == 0)
        msg = ctcp;
    else
        msg = unknown();
    client.send(Reply::fromClient(getPrefix(serverName), "NOTICE",
                                  client.getNick(), CTCP::buildCTCP(msg)));
}

// bot <bot_cmd>
// #chan <bot_cmd>
void Bot::handleMessage(Server &server, Client &client,
                        const std::string &body) const {
    std::vector<std::string> words = Utils::split(body, ' ');
    std::string cmd = Utils::foldCase(words.empty() ? "" : words[0]);
    cmd = Utils::trimPrefix(cmd, "!");
    std::string msg;
    if (cmd.empty() || cmd == "help")
        msg = help();
    else if (cmd == "version")
        msg = getVersion(server.getServerName());
    else if (cmd == "whoami")
        msg = "You are " + client.getNick();
    else if (cmd == "send") {
        if (words.size() < 3 || words[1].empty() || words[2].empty())
            client.sendReply(Reply::reply(Reply::ERR_NEEDMOREPARAMS, "SEND"));
        else
            handleSend(server, client, words[1], words[2]);
        return;
    } else if (cmd == "accept" || cmd == "reject") {
        bool ok = server.getTransferManager().setOfferStatus(&client,
                                                             cmd == "accept");
        msg = ok ? (cmd == "accept" ? "transfer accepted - connect to receive"
                                    : "transfer rejected")
                 : "no pending transfers for you";
    } else
        msg = unknown();
    client.send(Reply::fromClient(getPrefix(server.getServerName()), "PRIVMSG",
                                  client.getNick(), msg));
}

#endif
