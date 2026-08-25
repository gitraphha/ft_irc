#include "Reply.hpp"
#include "Errors.hpp"

#include <iostream>

int main() {
    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::ERR_NOSUCHNICK, "bob"));

    std::cout << Reply::build("irc.local", "*",
                              Reply::reply(Reply::ERR_NICKNAMEINUSE, "alice"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::ERR_NEEDMOREPARAMS, "PRIVMSG"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::RPL_WELCOME, "",
                                           "Welcome to the Internet Relay Network alice!user@host"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::RPL_TOPIC, "#chan", "a topic"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::RPL_NAMREPLY, "= #chan", "@alice bob"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::RPL_ENDOFNAMES, "#chan"));

    std::cout << Reply::build("irc.local", "alice",
                              Reply::reply(Reply::RPL_TOPIC, "#chan", ""));

    std::cout << Reply::build("irc.local", "*",
                              Reply::reply(Reply::ERR_NOTREGISTERED, ""));

    try {
        throw Error(Reply::reply(Reply::ERR_NOTONCHANNEL, "#chan"));
    } catch (const Error &e) {
        std::cout << "caught, hasReply=" << e.hasReply() << "\n";
        if (e.hasReply())
            std::cout << Reply::build("irc.local", "alice", e.reply());
    }

    try {
        throw Error("internal: channel name invalid");
    } catch (const Error &e) {
        std::cout << "internal, hasReply=" << e.hasReply()
                  << " what=" << e.what() << "\n";
    }

	try {
        throw Error(Reply::reply(Reply::ERR_USERNOTINCHANNEL, "bob #chan"));
    } catch (const Error &e) {
        std::cout << "caught, hasReply=" << e.hasReply() << "\n";
        if (e.hasReply())
            std::cout << Reply::build("irc.local", "alice", e.reply());
    }

	// or directly send an error
	// std::cout << Reply::build("irc.local", "alice",
	// 						Reply::reply(Reply::ERR_USERNOTINCHANNEL, "bob #chan"));
    return 0;
}
