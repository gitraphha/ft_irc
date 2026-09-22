#include "Errors.hpp"
#include "Reply.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static int failures = 0;

static void report(bool ok, const std::string &what) {
    if (!ok)
        failures++;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
}

static void same(const std::string &got, const std::string &want, const std::string &what) {
    report(got == want, what + (got == want ? "" : "  (got " + got.substr(0, got.size() - 2) + ")"));
}

int main() {
    std::cout << "[numeric replies: :server code target params :text]\n";
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::ERR_NOSUCHNICK, "bob")),
         ":irc.local 401 alice bob :No such nick/channel\r\n", "401 nick :No such nick/channel");
    same(Reply::build("irc.local", "*", Reply::reply(Reply::ERR_NICKNAMEINUSE, "alice")),
         ":irc.local 433 * alice :Nickname is already in use\r\n", "433 to * before registration");
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::ERR_NEEDMOREPARAMS, "PRIVMSG")),
         ":irc.local 461 alice PRIVMSG :Not enough parameters\r\n", "461 command :Not enough parameters");
    same(Reply::build("irc.local", "alice",
                      Reply::reply(Reply::RPL_WELCOME, "", "Welcome to the Internet Relay Network alice!user@host")),
         ":irc.local 001 alice :Welcome to the Internet Relay Network alice!user@host\r\n",
         "001 with a caller-supplied text");
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::RPL_TOPIC, "#chan", "a topic")),
         ":irc.local 332 alice #chan :a topic\r\n", "332 channel :topic");
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::RPL_NAMREPLY, "= #chan", "@alice bob")),
         ":irc.local 353 alice = #chan :@alice bob\r\n", "353 = channel :names");
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::RPL_ENDOFNAMES, "#chan")),
         ":irc.local 366 alice #chan :End of NAMES list\r\n", "366 channel :End of NAMES list");
    same(Reply::build("irc.local", "alice", Reply::reply(Reply::RPL_TOPIC, "#chan", "")),
         ":irc.local 332 alice #chan :\r\n", "332 with an empty topic keeps the colon");
    same(Reply::build("irc.local", "*", Reply::reply(Reply::ERR_NOTREGISTERED, "")),
         ":irc.local 451 * :You have not registered\r\n", "451 with no parameter");
    same(Reply::build("irc.local", "*", Reply::reply(Reply::ERR_INPUTTOOLONG, "")),
         ":irc.local 417 * :Input line was too long\r\n", "417 with no parameter");

    std::cout << "[Error carrying a reply]\n";
    try {
        throw Error::reply(Reply::ERR_NOTONCHANNEL, "#chan");
    } catch (const Error &e) {
        report(e.hasReply(), "Error::reply has a reply");
        same(Reply::build("irc.local", "alice", e.reply()),
             ":irc.local 442 alice #chan :You're not on that channel\r\n", "the reply builds as 442");
    }
    try {
        throw Error("internal: channel name invalid");
    } catch (const Error &e) {
        report(!e.hasReply(), "a plain Error has no reply");
        report(std::string(e.what()) == "internal: channel name invalid", "what() is the message");
    }
    try {
        throw Error::reply(Reply::ERR_USERNOTINCHANNEL, "bob #chan");
    } catch (const Error &e) {
        same(Reply::build("irc.local", "alice", e.reply()),
             ":irc.local 441 alice bob #chan :They aren't on that channel\r\n", "441 with two parameters");
    }
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
