#include "unit.hpp"

#include "Errors.hpp"

/*
 * Tests for connection registration (RFC 2812 section 3.1): PASS, NICK, USER,
 * CAP and Registration::tryRegister, driven through CommandProcessor::run
 * exactly as the event loop does, without any socket (see unit.hpp for how
 * the replies are read back).
 *
 * Error replies are no longer thrown: CommandProcessor::run sends the numeric
 * to the client and returns, so every check reads the numeric from drain().
 */

int main() {

    {
        std::cout << "[PASS NICK USER in RFC order]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "127.0.0.1");
        report(!c.send("PASS secret"), "PASS accepted (link stays open)");
        report(c.drain().empty(), "PASS sends nothing");
        report(!c.send("NICK bob"), "NICK accepted");
        report(c.drain().empty(), "NICK sends nothing before registration");
        report(!c.client->isRegistered(), "not registered before USER");
        report(!c.send("USER b 0 * :Bob Example"), "USER accepted");
        std::string w = c.drain();
        report(c.client->isRegistered(), "registered after NICK+USER");
        report(contains(w, ":irc.local 001 bob :Welcome to the Internet Relay "
                           "Network bob!b@127.0.0.1\r\n"),
               "001 welcome with nick!user@host");
        report(contains(w, ":irc.local 002 bob :Your host is irc.local"),
               "002 yourhost");
        report(contains(w, ":irc.local 003 bob :This server was created"),
               "003 created");
        report(contains(w, ":irc.local 004 bob irc.local 1.0  itkol"),
               "004 myinfo");
        report(contains(w, ":irc.local 005 bob CHANTYPES=&#+! NICKLEN=30"),
               "005 isupport");
        report(contains(w, ":irc.local 422 bob :MOTD File is missing\r\n"),
               "422 nomotd ends the burst");
        report(!c.closed, "connection stays open");
        report(server.getClientManager().hasNick("BOB"),
               "nick indexed case-insensitively");
        report(&server.getClientManager().at("bob") == c.client,
               "at(nick) finds the client");
    }

    {
        std::cout << "[CAP LS first (irssi), PASS between NICK and USER]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "::1");
        report(!c.send("CAP LS 302"), "CAP LS accepted");
        report(contains(c.drain(), ":irc.local CAP * LS :"),
               "CAP LS gets an empty capability list");
        report(!c.send("NICK alice"), "NICK before PASS is fine");
        report(!c.send("PASS secret"), "PASS after NICK accepted");
        report(c.drain().empty() && !c.client->isRegistered(),
               "nothing sent, still waiting for USER");
        report(!c.send("USER a 0 * :Alice"), "USER completes registration");
        report(c.client->isRegistered(), "registered");
        report(contains(c.drain(), " 001 alice "), "welcome sent");
        report(!c.send("CAP END"), "CAP END ignored");
        report(c.drain().empty(), "CAP END sends nothing");
    }

    {
        std::cout << "[wrong password]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("PASS nope");
        c.send("NICK bob");
        report(c.send("USER b 0 * :B"), "USER with the wrong PASS asks to close the link");
        std::string w = c.drain();
        report(contains(w, ":irc.local 464 bob :Password incorrect\r\n"),
               "464 password mismatch");
        report(contains(w, "ERROR :Closing link"), "ERROR closing link");
        report(c.closed, "handler asks the loop to close the connection");
        report(!c.client->isRegistered(), "never registered");
    }

    {
        std::cout << "[no PASS at all]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("NICK bob");
        c.send("USER b 0 * :B");
        report(contains(c.drain(), " 464 "), "464 when PASS was never sent");
        report(c.closed, "connection closed");
    }

    {
        std::cout << "[NICK errors]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("NICK");
        report(contains(c.drain(), " 431 * :No nickname given"),
               "NICK without parameter -> 431 (not 461)");
        c.send("NICK 1bob");
        report(contains(c.drain(), " 432 * 1bob :Erroneous"),
               "nick starting with a digit -> 432");
        report(!c.send("NICK bo b"), "extra param ignored, first is taken");
        report(c.client->getNick() == "bo", "nick is 'bo'");
        c.send("NICK #bob");
        report(contains(c.drain(), " 432 bo #bob"),
               "nick with # -> 432, reply addressed to current nick");
        c.send("NICK abcdefghijklmnopqrstuvwxyz01234");
        report(contains(c.drain(), " 432 "),
               "31 chars -> 432 (NICKLEN 30)");
        report(!c.send("NICK [b-o_b]^`\\{|}") &&
                   c.client->getNick() == "[b-o_b]^`\\{|}",
               "all RFC specials accepted");
        report(!c.send("NICK bob"), "back to bob");

        Conn d(server, 5, "h");
        d.send("NICK BOB");
        report(contains(d.drain(), " 433 * BOB :Nickname is already in use"),
               "same nick different case -> 433");
        report(!d.send("NICK carol"), "free nick accepted");
        report(!c.send("NICK b[o]b"), "bob renames to b[o]b");
        d.send("NICK B{O}B");
        report(contains(d.drain(), " 433 carol B{O}B"),
               "[] vs {} equivalence (rfc1459 casemapping) -> 433");
        report(!c.send("NICK bob"), "back to bob");
        report(!c.send("NICK Bob") && c.client->getNick() == "Bob",
               "a client may change the case of its own nick");
    }

    {
        std::cout << "[after registration]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("PASS secret");
        c.send("NICK bob");
        c.send("USER b 0 * :B");
        c.drain();
        c.send("PASS secret");
        report(contains(c.drain(), " 462 bob :Unauthorized command"),
               "PASS after registration -> 462");
        c.send("USER b 0 * :B");
        report(contains(c.drain(), " 462 "),
               "USER after registration -> 462");
        report(!c.send("NICK robert"), "NICK change accepted");
        report(c.drain() == ":bob!b@h NICK robert\r\n",
               "rename echoed with the old prefix");
        report(server.getClientManager().hasNick("robert") &&
                   !server.getClientManager().hasNick("bob"),
               "nick index updated");
    }

    {
        std::cout << "[unregistered client using a registered-only command]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("JOIN #a");
        report(contains(c.drain(), " 451 * :You have not registered"),
               "JOIN before registration -> 451");
        c.send("USER b");
        report(contains(c.drain(), " 461 * USER :Not enough parameters"),
               "USER with 1 param -> 461");
        c.send("BOGUS");
        report(contains(c.drain(), " 421 * BOGUS :Unknown command"),
               "unknown command -> 421");
        c.send("PASS");
        report(contains(c.drain(), " 461 * PASS"),
               "PASS without param -> 461");
    }

    {
        std::cout << "[ClientManager lifecycle]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        Conn   d(server, 5, "h");
        report(server.getClientManager().size() == 2,
               "two connections without nicks");
        report(!server.getClientManager().add(4, "h", server),
               "duplicate fd rejected");
        report(server.getClientManager().find(9) == 0,
               "find unknown fd -> NULL");
        c.send("NICK bob");
        server.getClientManager().remove(4, server.getChannelManager());
        report(!server.getClientManager().hasNick("bob") &&
                   !server.getClientManager().hasClient(4),
               "remove drops fd and nick index");
        report(server.getClientManager().size() == 1, "one left");
        bool threw = false;
        try {
            server.getClientManager().at("bob");
        } catch (const Error &e) {
            threw = e.hasReply() && e.reply().code == Reply::ERR_NOSUCHNICK;
        }
        report(threw, "at(unknown nick) throws ERR_NOSUCHNICK");
        server.getClientManager().remove(9, server.getChannelManager());
        report(true, "remove(unknown fd) is a no-op");
    }

    return finish("registration");
}
