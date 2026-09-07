#include "Client.hpp"
#include "CommandProcessor.hpp"
#include "Errors.hpp"
#include "Server.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

/*
 * Tests for connection registration (RFC 2812 section 3.1): PASS, NICK, USER,
 * CAP and Handlers::tryRegister, driven through CommandProcessor::run exactly
 * as the event loop does, without any socket.
 *
 * Links against the real Server.cpp, so this test breaks loudly if the Server
 * constructor changes on main. Registration is driven through this file's own
 * CommandProcessor, so it does not depend on Server registering its commands.
 */

static int failures = 0;

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

// A fake connection: a Client registered in the server's ClientManager, and
// a way to feed it lines like the event loop would.
struct Conn {
    Server          &server;
    CommandProcessor cmds;
    Client          *client;
    bool             closed;
    std::string      out;   // everything the server queued for this client
    std::string      error; // numeric reply carried by the last thrown Error

    Conn(Server &s, int fd, const std::string &host)
        : server(s), closed(false) {
        cmds.registerCommands();
        client = new Client(fd);
        client->setHostname(host);
        server.getClientManager().add(fd, client);
    }

    // send feeds one line; returns false if the handler threw an Error, in
    // which case `error` holds the numeric reply exactly as the loop sends it.
    bool send(const std::string &line) {
        error.clear();
        try {
            closed = cmds.run(server, *client, line) || closed;
        } catch (const Error &e) {
            if (e.hasReply())
                error = Reply::build(
                    server.getServerName(),
                    client->getNick().empty() ? "*" : client->getNick(),
                    e.reply());
            else
                error = e.what();
            return false;
        }
        return true;
    }

    // drain returns what was queued since the last drain.
    std::string drain() {
        std::string s = client->getSendBuffer();
        client->clearSendBuffer();
        out += s;
        return s;
    }
};

static bool contains(const std::string &hay, const std::string &needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    std::cout << "=== Registration ===\n\n";

    {
        std::cout << "[PASS NICK USER in RFC order]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "127.0.0.1");
        report(c.send("PASS secret"), "PASS accepted");
        report(c.drain().empty(), "PASS sends nothing");
        report(c.send("NICK bob"), "NICK accepted");
        report(c.drain().empty(), "NICK sends nothing before registration");
        report(!c.client->isRegistered(), "not registered before USER");
        report(c.send("USER b 0 * :Bob Example"), "USER accepted");
        std::string w = c.drain();
        report(c.client->isRegistered(), "registered after NICK+USER");
        report(contains(w, ":irc.local 001 bob :Welcome to the Internet Relay "
                           "Network bob!b@127.0.0.1\r\n"),
               "001 welcome with nick!user@host");
        report(contains(w, ":irc.local 002 bob :Your host is irc.local"),
               "002 yourhost");
        report(contains(w, ":irc.local 003 bob :This server was created"),
               "003 created");
        report(contains(w, ":irc.local 004 bob irc.local 1.0 o itkol"),
               "004 myinfo");
        report(contains(w, ":irc.local 005 bob CHANTYPES=# NICKLEN=30"),
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
        report(c.send("CAP LS 302"), "CAP LS accepted");
        report(c.drain() == ":irc.local CAP * LS :\r\n",
               "CAP LS gets an empty list");
        report(c.send("NICK alice"), "NICK before PASS is fine");
        report(c.send("PASS secret"), "PASS after NICK accepted");
        report(c.drain().empty() && !c.client->isRegistered(),
               "nothing sent, still waiting for USER");
        report(c.send("USER a 0 * :Alice"), "USER completes registration");
        report(c.client->isRegistered(), "registered");
        report(contains(c.drain(), " 001 alice "), "welcome sent");
        report(c.send("CAP END"), "CAP END ignored");
        report(c.drain().empty(), "CAP END sends nothing");
    }

    {
        std::cout << "[wrong password]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        c.send("PASS nope");
        c.send("NICK bob");
        report(c.send("USER b 0 * :B"), "USER handled");
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
        report(!c.send("NICK") &&
                   contains(c.error, " 431 * :No nickname given"),
               "NICK without parameter -> 431 (not 461)");
        report(!c.send("NICK 1bob") &&
                   contains(c.error, " 432 * 1bob :Erroneous"),
               "nick starting with a digit -> 432");
        report(c.send("NICK bo b"), "extra param ignored, first is taken");
        report(c.client->getNick() == "bo", "nick is 'bo'");
        report(!c.send("NICK #bob") && contains(c.error, " 432 bo #bob"),
               "nick with # -> 432, reply addressed to current nick");
        report(!c.send("NICK abcdefghijklmnopqrstuvwxyz01234") &&
                   contains(c.error, " 432 "),
               "31 chars -> 432 (NICKLEN 30)");
        report(c.send("NICK [b-o_b]^`\\{|}") &&
                   c.client->getNick() == "[b-o_b]^`\\{|}",
               "all RFC specials accepted");
        report(c.send("NICK bob"), "back to bob");

        Conn d(server, 5, "h");
        report(!d.send("NICK BOB") &&
                   contains(d.error, " 433 * BOB :Nickname is already in use"),
               "same nick different case -> 433");
        report(d.send("NICK carol"), "free nick accepted");
        report(c.send("NICK b[o]b"), "bob renames to b[o]b");
        report(!d.send("NICK B{O}B") && contains(d.error, " 433 carol B{O}B"),
               "[] vs {} equivalence (rfc1459 casemapping) -> 433");
        report(c.send("NICK bob"), "back to bob");
        report(c.send("NICK Bob") && c.client->getNick() == "Bob",
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
        report(!c.send("PASS secret") &&
                   contains(c.error, " 462 bob :Unauthorized command"),
               "PASS after registration -> 462");
        report(!c.send("USER b 0 * :B") && contains(c.error, " 462 "),
               "USER after registration -> 462");
        report(c.send("NICK robert"), "NICK change accepted");
        report(c.drain() == ":bob!b@h NICK :robert\r\n",
               "rename echoed with the old prefix");
        report(server.getClientManager().hasNick("robert") &&
                   !server.getClientManager().hasNick("bob"),
               "nick index updated");
    }

    {
        std::cout << "[unregistered client using a registered-only command]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        report(!c.send("JOIN #a") &&
                   contains(c.error, " 451 * :You have not registered"),
               "JOIN before registration -> 451");
        report(!c.send("USER b") &&
                   contains(c.error, " 461 * USER :Not enough parameters"),
               "USER with 1 param -> 461");
        report(!c.send("BOGUS") &&
                   contains(c.error, " 421 * BOGUS :Unknown command"),
               "unknown command -> 421");
        report(!c.send("PASS") && contains(c.error, " 461 * PASS"),
               "PASS without param -> 461");
    }

    {
        std::cout << "[ClientManager lifecycle]\n";
        Server server(6667, "secret");
        Conn   c(server, 4, "h");
        Conn   d(server, 5, "h");
        report(server.getClientManager().size() == 2,
               "two connections without nicks");
        Client *dup = new Client(4);
        report(!server.getClientManager().add(4, dup), "duplicate fd rejected");
        delete dup;
        report(server.getClientManager().find(9) == 0,
               "find unknown fd -> NULL");
        c.send("NICK bob");
        server.getClientManager().remove(4);
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
        server.getClientManager().remove(9);
        report(true, "remove(unknown fd) is a no-op");
    }

    std::cout << "\n"
              << (failures ? "FAILED" : "ALL OK") << " (" << failures
              << " failures)\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
