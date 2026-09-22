#include "unit.hpp"

/*
 * Channel operator commands (evaluation sheet: "a regular user does not have
 * privileges to do channel operator actions; then test with an operator"):
 * JOIN, KICK, INVITE, TOPIC, MODE i t k o l, driven through
 * CommandProcessor::run without sockets (see unit.hpp).
 */

int main() {

    {
        std::cout << "[JOIN: first joiner is operator, others are not]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        a.send("JOIN #c");
        std::string w = a.drain();
        report(contains(w, ":alice!a@h JOIN #c"), "JOIN is echoed to the joiner");
        report(contains(w, " 353 alice = #c :@alice"), "353 names list shows @alice (operator)");
        report(contains(w, " 366 alice #c "), "366 end of names");
        b.send("JOIN #c");
        report(contains(a.drain(), ":bob!b@h JOIN #c"), "members see the new JOIN");
        report(contains(b.drain(), " 353 bob = #c :@alice bob"), "bob is listed without @");
        b.send("JOIN #c");
        report(!contains(b.drain(), " 4"), "joining twice is not an error");
        a.send("PART #nochan");
        report(contains(a.drain(), " 403 alice #nochan "), "PART of an unknown channel -> 403");
    }

    {
        std::cout << "[regular user has no operator privileges]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        Conn   c(server, "carol");
        a.send("JOIN #c"); b.send("JOIN #c"); a.drain(); b.drain();
        b.send("KICK #c alice");
        report(contains(b.drain(), " 482 bob #c "), "KICK by a member -> 482");
        b.send("MODE #c +i");
        report(contains(b.drain(), " 482 bob #c "), "MODE +i by a member -> 482");
        b.send("MODE #c +o bob");
        report(contains(b.drain(), " 482 bob #c "), "MODE +o by a member -> 482");
        a.send("MODE #c +t"); a.drain(); b.drain();
        b.send("TOPIC #c :hijack");
        report(contains(b.drain(), " 482 bob #c "), "TOPIC under +t by a member -> 482");
        c.send("KICK #c bob");
        report(contains(c.drain(), " 442 carol #c "), "KICK by a non-member -> 442");
        c.send("TOPIC #c");
        report(contains(c.drain(), " 442 carol #c "), "TOPIC by a non-member -> 442");
    }

    {
        std::cout << "[operator: KICK]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        a.send("JOIN #c"); b.send("JOIN #c"); a.drain(); b.drain();
        a.send("KICK #c bob :bye");
        report(contains(a.drain(), ":alice!a@h KICK #c bob :bye") &&
                   contains(b.drain(), ":alice!a@h KICK #c bob :bye"),
               "KICK is broadcast to everyone including the kicked user");
        b.send("PRIVMSG #c :still here?");
        report(contains(b.drain(), " 404 bob #c "), "kicked user is out (404 on PRIVMSG)");
        a.send("KICK #c bob");
        report(contains(a.drain(), " 441 alice bob #c "), "KICK of a non-member -> 441");
        a.send("KICK #c");
        report(contains(a.drain(), " 461 alice KICK "), "KICK without nick -> 461");
    }

    {
        std::cout << "[operator: INVITE and +i]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        a.send("JOIN #c"); a.drain();
        a.send("MODE #c +i"); a.drain();
        b.send("JOIN #c");
        report(contains(b.drain(), " 473 bob #c "), "JOIN an invite-only channel -> 473");
        a.send("INVITE bob #c");
        report(contains(a.drain(), " 341 alice #c bob"), "inviter gets 341 <channel> <nick>");
        report(contains(b.drain(), ":alice!a@h INVITE bob #c"), "invitee gets the INVITE");
        b.send("JOIN #c");
        report(contains(b.drain(), " 353 bob = #c "), "invited user can join");
        a.send("INVITE nobody #c");
        report(contains(a.drain(), " 401 alice nobody "), "INVITE an unknown nick -> 401");
        a.send("INVITE bob #c");
        report(contains(a.drain(), " 443 alice bob #c "), "INVITE a member -> 443");
        Conn c(server, "carol");
        b.send("INVITE carol #c");
        report(contains(b.drain(), " 482 bob #c "), "INVITE by a non-operator under +i -> 482");
    }

    {
        std::cout << "[operator: TOPIC and +t]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        a.send("JOIN #c"); b.send("JOIN #c"); a.drain(); b.drain();
        a.send("TOPIC #c");
        report(contains(a.drain(), " 331 alice #c "), "no topic -> 331");
        b.send("TOPIC #c :hello");
        report(contains(a.drain(), ":bob!b@h TOPIC #c :hello"), "without +t any member sets the topic, broadcast");
        b.drain();
        a.send("TOPIC #c");
        report(contains(a.drain(), " 332 alice #c :hello"), "TOPIC query -> 332");
        a.send("MODE #c +t"); a.drain(); b.drain();
        b.send("TOPIC #c :nope");
        report(contains(b.drain(), " 482 bob #c "), "under +t a member cannot set it -> 482");
        a.send("TOPIC #c :ops only");
        report(contains(b.drain(), ":alice!a@h TOPIC #c :ops only"), "the operator can");
    }

    {
        std::cout << "[operator: MODE +k +l +o and the query]\n";
        Server server(6667, "secret");
        Conn   a(server, "alice");
        Conn   b(server, "bob");
        Conn   c(server, "carol");
        a.send("JOIN #c"); a.drain();
        a.send("MODE #c +k secret");
        report(contains(a.drain(), ":alice!a@h MODE #c +k secret"), "+k is echoed");
        b.send("JOIN #c");
        report(contains(b.drain(), " 475 bob #c "), "JOIN without the key -> 475");
        b.send("JOIN #c wrong");
        report(contains(b.drain(), " 475 bob #c "), "JOIN with a wrong key -> 475");
        b.send("JOIN #c secret");
        report(contains(b.drain(), " 353 bob = #c "), "JOIN with the key works");
        a.drain();
        a.send("MODE #c -k"); a.drain(); b.drain();
        a.send("MODE #c +l 2"); a.drain(); b.drain();
        c.send("JOIN #c");
        report(contains(c.drain(), " 471 carol #c "), "JOIN over the limit -> 471");
        a.send("MODE #c");
        report(contains(a.drain(), " 324 alice #c +"), "MODE query -> 324 with the current modes");
        a.send("MODE #c +o bob");
        report(contains(b.drain(), ":alice!a@h MODE #c +o bob"), "+o is broadcast");
        b.send("MODE #c -l"); b.drain(); a.drain();
        c.send("JOIN #c");
        report(contains(c.drain(), " 353 carol = #c "), "the new operator lifted the limit");
        b.send("MODE #c -o alice"); a.drain(); b.drain(); c.drain();
        a.send("KICK #c bob");
        report(contains(a.drain(), " 482 alice #c "), "after -o alice is a regular member again");
        a.send("MODE #c +z");
        report(contains(a.drain(), " 472 alice z "), "unknown mode letter -> 472");
        a.send("MODE #nochan +i");
        report(contains(a.drain(), " 403 alice #nochan "), "MODE on an unknown channel -> 403");
    }

    return finish("operator");
}
