#include "Client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

/*
 * Tests for Client: per-connection state only. Since the POLLOUT rework the
 * Client has no buffers of its own -- Socket owns the recv/send buffers (see
 * test_socket) and Client::send() hands the bytes to Server::sendMsg. With no
 * Server attached, send() is a no-op, which is what these checks rely on.
 */

static int failures = 0;

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

int main() {

    {
        std::cout << "[construction]\n";
        Client c;
        report(c.getFd() == -1, "default client has fd -1");
        Client d(7, "10.0.0.2", 0);
        report(d.getFd() == 7, "fd is stored");
        report(d.getHostname() == "10.0.0.2", "hostname is stored");
        report(!d.isRegistered() && !d.hasPassAccepted() && !d.isOperator(),
               "starts unregistered, no PASS, not operator");
    }

    {
        std::cout << "[nick is stored casefolded as well]\n";
        Client c;
        c.setNick("BoB[\\~");
        report(c.getNick() == "BoB[\\~", "getNick keeps the chosen spelling");
        report(c.getNickCaseFolded() == "bob{|^",
               "getNickCaseFolded uses IRC (rfc1459) case mapping");
    }

    {
        std::cout << "[invites are per channel and expire with the channel]\n";
        Client c;
        c.addInvite("#room", 100);
        report(c.isInvitedTo("#room", 100), "invited to the channel");
        report(!c.isInvitedTo("#room", 200),
               "a channel re-created later (new creation time) is not covered");
        report(!c.isInvitedTo("#other", 100), "another channel is not covered");
        c.removeInvite("#room");
        report(!c.isInvitedTo("#room", 100), "removal clears the invite");
    }

    {
        std::cout << "[registration state starts empty]\n";
        Client c;
        report(!c.isRegistered(), "a new client is not registered");
        report(!c.hasPassAccepted(), "a new client has not sent PASS");
        report(!c.hasIdentity(), "a new client has neither NICK nor USER");
    }

    // NICK and USER may arrive in either order.
    {
        std::cout << "[NICK then USER]\n";
        Client c;
        c.setNick("bob");
        report(!c.hasIdentity(), "NICK alone is not enough");
        c.setUser("b", "Bob Example");
        report(c.hasIdentity(), "NICK and USER together are enough");
        report(c.getUsername() == "b" && c.getRealname() == "Bob Example",
               "username and realname are stored");
    }

    {
        std::cout << "[USER then NICK]\n";
        Client c;
        c.setUser("b", "Bob Example");
        report(!c.hasIdentity(), "USER alone is not enough");
        c.setNick("bob");
        report(c.hasIdentity(), "the other order works the same");
    }

    {
        std::cout << "[prefix and reply target]\n";
        Client c;
        report(c.getPrefix() == "*", "prefix is * without an identity");
        report(c.getReplyTarget() == "*", "numerics go to * without a nick");
        c.setNick("bob");
        report(c.getPrefix() == "*", "prefix is * with only a nickname");
        report(c.getReplyTarget() == "bob", "numerics go to the nick once set");
        c.setUser("b", "Bob Example");
        report(c.getPrefix() == "bob!b@localhost",
               "prefix falls back to localhost without a hostname");
        c.setHostname("127.0.0.1");
        report(c.getPrefix() == "bob!b@127.0.0.1", "prefix is nick!user@host");
    }

    {
        std::cout << "[flags]\n";
        Client c;
        c.setPassAccepted(true);
        c.setRegistered(true);
        c.setOperator(true);
        report(c.hasPassAccepted() && c.isRegistered() && c.isOperator(),
               "setters flip the flags");
        c.setOperator(false);
        report(!c.isOperator(), "operator flag can be cleared");
    }

    {
        std::cout << "[send without a server is a safe no-op]\n";
        Client c(9, "h", 0);
        c.send("PING :x\r\n");
        c.sendClosingMsg("bye");
        report(true, "no server attached: nothing to deliver to, no crash");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
