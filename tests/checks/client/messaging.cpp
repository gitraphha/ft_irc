#include "unit.hpp"

#include "ChannelManager.hpp"

/*
 * Tests for QUIT, PING, PONG (Connection.cpp), NOTICE and PRIVMSG
 * (Messaging.cpp), driven through CommandProcessor::run exactly as the event
 * loop does, without any socket (see unit.hpp). Clients are put into the
 * registered state directly, so this file does not depend on the registration
 * commands.
 *
 * Conn::send returns true when the handler asks the loop to close the link;
 * error numerics are read back with drain() (handlers no longer throw).
 */

int main() {

    {
        std::cout << "[PING / PONG]\n";
        Server server(6667, "secret");
        Conn   c(server, "bob");
        report(!c.send("PING abc") &&
                   c.drain() == ":irc.local PONG irc.local :abc\r\n",
               "PING -> PONG <server> :<token>");
        c.send("PING");
        report(contains(c.drain(), " 409 bob :No origin specified"),
               "PING without token -> 409 (not 461)");
        report(!c.send("PONG abc") && c.drain().empty(),
               "PONG accepted silently");

        Conn fresh(server, 9, "h"); // not registered: PING must still work
        report(!fresh.send("PING x") &&
                   contains(fresh.drain(), "PONG irc.local :x"),
               "PING is usable before registration");
    }

    {
        std::cout << "[QUIT reaches channel peers exactly once]\n";
        Server          server(6667, "secret");
        Conn            a(server, "alice");
        Conn            b(server, "bob");
        Conn            c(server, "carol");
        ChannelManager &cm = server.getChannelManager();
        cm.join(*a.client, "#x");
        cm.join(*b.client, "#x");
        cm.join(*a.client, "#y");
        cm.join(*b.client, "#y"); // alice and bob share two channels
        a.drain();
        b.drain();
        c.drain();

        report(a.send("QUIT :bye") && a.closed,
               "QUIT returns true: the loop must close the link");
        report(a.drain() == "ERROR :Closing link: (alice!a@h) [Quit: bye]\r\n",
               "quitter gets ERROR :Closing link (no prefix)");
        report(b.drain() == ":alice!a@h QUIT :bye\r\n",
               "peer in two shared channels sees the QUIT once");
        report(c.drain().empty(), "someone sharing no channel sees nothing");

        Conn d(server, "dave");
        report(d.send("QUIT") && d.closed &&
                   contains(d.drain(), "[Quit: Client Quit]"),
               "QUIT without a message uses the default reason");

        Conn fresh(server, 9, "h"); // unregistered client may QUIT too
        report(fresh.send("QUIT") &&
                   contains(fresh.drain(), "ERROR :Closing link: (*)"),
               "QUIT before registration closes with prefix *");
    }

    {
        std::cout << "[NOTICE delivers like PRIVMSG and never replies]\n";
        Server          server(6667, "secret");
        Conn            a(server, "alice");
        Conn            b(server, "bob");
        Conn            c(server, "carol");
        ChannelManager &cm = server.getChannelManager();
        cm.join(*a.client, "#room");
        cm.join(*b.client, "#room");
        a.drain();
        b.drain();

        report(!a.send("NOTICE #room :psst") && a.drain().empty(),
               "channel NOTICE: sender is not echoed");
        report(b.drain() == ":alice!a@h NOTICE #room :psst\r\n",
               "channel NOTICE: other member receives it");
        report(!c.send("NOTICE #room :hi") && c.drain().empty(),
               "NOTICE to a channel you are not in -> silence (no 404)");
        report(!c.send("NOTICE #nochan :hi") && c.drain().empty(),
               "NOTICE to an unknown channel -> silence (no 403)");
        report(!c.send("NOTICE nobody :hi") && c.drain().empty(),
               "NOTICE to an unknown nick -> silence (no 401)");
        report(!c.send("NOTICE") && c.drain().empty(),
               "NOTICE without target -> silence (no 411)");
        report(!c.send("NOTICE #room") && c.drain().empty(),
               "NOTICE without text -> silence (no 412)");
        report(!a.send("NOTICE bob :hey") && a.drain().empty() &&
                   b.drain() == ":alice!a@h NOTICE bob :hey\r\n",
               "NOTICE to a nick: only that nick gets it");
    }

    {
        std::cout << "[PRIVMSG delivers and replies on errors]\n";
        Server          server(6667, "secret");
        Conn            a(server, "alice");
        Conn            b(server, "bob");
        Conn            c(server, "carol");
        ChannelManager &cm = server.getChannelManager();
        cm.join(*a.client, "#room");
        cm.join(*b.client, "#room");
        a.drain();
        b.drain();

        report(!a.send("PRIVMSG #room :hello") && a.drain().empty(),
               "channel PRIVMSG: sender is not echoed");
        report(b.drain() == ":alice!a@h PRIVMSG #room :hello\r\n",
               "channel PRIVMSG: other member receives it");
        report(!a.send("PRIVMSG bob :hi bob") && a.drain().empty() &&
                   b.drain() == ":alice!a@h PRIVMSG bob :hi bob\r\n",
               "PRIVMSG to a nick");
        report(!c.send("PRIVMSG #room :hi") &&
                   contains(c.drain(), " 404 carol #room "),
               "PRIVMSG to a channel you are not in -> 404");
        report(!c.send("PRIVMSG #nochan :hi") &&
                   contains(c.drain(), " 403 carol #nochan "),
               "PRIVMSG to an unknown channel -> 403");
        report(!c.send("PRIVMSG nobody :hi") &&
                   contains(c.drain(), " 401 carol nobody "),
               "PRIVMSG to an unknown nick -> 401");
        report(!c.send("PRIVMSG") && contains(c.drain(), " 411 carol "),
               "PRIVMSG without target -> 411");
        report(!c.send("PRIVMSG #room") && contains(c.drain(), " 412 carol "),
               "PRIVMSG without text -> 412");
        report(!a.send("PRIVMSG bob,#room :both") && a.drain().empty() &&
                   b.drain() == ":alice!a@h PRIVMSG bob :both\r\n"
                                ":alice!a@h PRIVMSG #room :both\r\n",
               "PRIVMSG with two targets");
    }

    return finish("cmds2");
}
