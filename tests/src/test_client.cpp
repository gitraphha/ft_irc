#include "Client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

/* Tests for Client buffering and connection state. */

static int  failures = 0;

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

static void expectLine(Client &client, const std::string &want) {
    std::string line;
    if (!client.extractLine(line)) {
        report(false, "expected a line \"" + want + "\", got none");
        return;
    }
    report(line == want, "line is \"" + want + "\"" +
                             (line == want ? "" : ", got \"" + line + "\""));
}

static void expectNoLine(Client &client, const std::string &why) {
    std::string line;
    report(!client.extractLine(line), why);
}

int main() {
    std::cout << "=== Client ===\n\n";

    {
        std::cout << "[one complete line]\n";
        Client c;
        c.appendToRecvBuffer("NICK bob\r\n");
        expectLine(c, "NICK bob");
        expectNoLine(c, "buffer is empty afterwards");
    }

    // TCP may split one command across multiple recv() calls.
    {
        std::cout << "[command split across three recv() calls]\n";
        Client c;
        c.appendToRecvBuffer("NI");
        expectNoLine(c, "nothing yet after \"NI\"");
        c.appendToRecvBuffer("CK bo");
        expectNoLine(c, "still nothing after \"CK bo\"");
        c.appendToRecvBuffer("b\r\n");
        expectLine(c, "NICK bob");
    }

    // One recv() may contain several complete commands.
    {
        std::cout << "[three commands in one chunk]\n";
        Client c;
        c.appendToRecvBuffer("PASS x\r\nNICK bob\r\nUSER b 0 * :B\r\n");
        expectLine(c, "PASS x");
        expectLine(c, "NICK bob");
        expectLine(c, "USER b 0 * :B");
        expectNoLine(c, "buffer is empty afterwards");
    }

    {
        std::cout << "[combined chunk may exceed one message limit]\n";
        Client            c;
        const std::string first(300, 'A');
        const std::string second(300, 'B');
        c.appendToRecvBuffer(first + "\r\n" + second + "\r\n");
        expectLine(c, first);
        expectLine(c, second);
        expectNoLine(c, "both individually valid lines were extracted");
    }

    {
        std::cout << "[complete line plus a partial one]\n";
        Client c;
        c.appendToRecvBuffer("NICK bob\r\nUSE");
        expectLine(c, "NICK bob");
        expectNoLine(c, "\"USE\" is held until the rest arrives");
        c.appendToRecvBuffer("R b 0 * :B\r\n");
        expectLine(c, "USER b 0 * :B");
    }

    // Accept bare '\n' for nc testing.
    {
        std::cout << "[bare newline from nc]\n";
        Client c;
        c.appendToRecvBuffer("NICK bob\n");
        expectLine(c, "NICK bob");
    }

    {
        std::cout << "[empty line]\n";
        Client c;
        c.appendToRecvBuffer("\r\n");
        expectLine(c, "");
    }

    {
        std::cout << "[nick is stored casefolded as well]\n";
        Client c;
        c.setNick("BoB[\\~");
        report(c.getNick() == "BoB[\\~", "getNick keeps the chosen spelling");
        report(c.getNickCaseFolded() == "bob{|^",
               "getNickCaseFolded uses IRC case mapping");
    }

    {
        std::cout << "[invites are casefolded and unique]\n";
        Client c;
        c.addInvite("#TeSt[Room]");
        c.addInvite("#test{room}");
        report(c.isInvitedTo("#TEST[ROOM]"),
               "equivalent channel spellings match");
        c.removeInvite("#test{room}");
        report(!c.isInvitedTo("#TeSt[Room]"),
               "one removal clears the unique invite");
    }

    {
        std::cout << "[registration state starts empty]\n";
        Client c;
        report(!c.isRegistered(), "a new client is not registered");
        report(!c.isPassAccepted(), "a new client has not sent PASS");
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
        std::cout << "[prefix]\n";
        Client c;
        report(c.getPrefix().empty(), "prefix is empty without an identity");
        c.setNick("bob");
        report(c.getPrefix().empty(), "prefix is empty with only a nickname");
        c.setUser("b", "Bob Example");
        report(c.getPrefix().empty(), "prefix is empty without a hostname");
        c.setHostname("127.0.0.1");
        report(c.getPrefix() == "bob!b@127.0.0.1", "prefix is nick!user@host");
    }

    {
        std::cout << "[send queues output]\n";
        Client c;
        report(!c.hasPendingOutput(), "nothing queued at first");
        c.send("PING :x\r\n");
        report(c.hasPendingOutput(), "output is queued");
        report(c.getSendBuffer() == "PING :x\r\n", "buffer holds the message");
        c.consumeSendBuffer(5);
        report(c.getSendBuffer() == ":x\r\n",
               "a partial write leaves the tail");
        c.consumeSendBuffer(100);
        report(!c.hasPendingOutput(), "consuming past the end empties it");
        c.send("NOTICE bob :queued\r\n");
        c.clearSendBuffer();
        report(!c.hasPendingOutput(), "clearSendBuffer discards all output");
    }

    std::cout << "\n";
    if (failures == 0)
        std::cout << "all checks passed\n";
    else
        std::cout << failures << " check(s) failed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
