#pragma once
/*
 * Unit-test harness for the command layer, without sockets.
 *
 * Since the POLLOUT rework every reply goes Client::send -> Server::sendMsg ->
 * Socket::sendMsg (per-fd write buffer) and Server::sendMsg logs each message
 * as "Sendto FD <fd>: <line>" on std::cout. The Socket buffer is private to the
 * Server and the fds here are fake, so the tests read the replies back from
 * that log: std::cout is redirected around every command, then the captured
 * "Sendto FD n" lines are handed back as "<line>\r\n", exactly what the loop
 * would have written to the socket.
 *
 *   Server server(6667, "secret");
 *   Conn   c(server, 4, "127.0.0.1");     // unregistered connection
 *   c.send("NICK bob");                   // runs the command like the loop
 *   std::string out = c.drain();          // replies queued since last drain
 *
 *   Conn r(server, "alice");              // already registered alice!a@h
 */

#include "Client.hpp"
#include "CommandProcessor.hpp"
#include "Server.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

static bool contains(const std::string &hay, const std::string &needle) {
    return hay.find(needle) != std::string::npos;
}

static int finish(const char *name) {
    (void)name;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

// Everything printed on std::cout while this object lives.
struct CoutCapture {
    std::ostringstream buf;
    std::streambuf    *old;
    CoutCapture() : old(std::cout.rdbuf(buf.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old); }
};

static std::string stripAnsi(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') {
            while (i < s.size() && s[i] != 'm')
                ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

// "Sendto FD <fd>: <line>" log entries for one fd -> "<line>\r\n" joined.
static std::string sentTo(const std::string &log, int fd) {
    std::ostringstream key;
    key << "Sendto FD " << fd << ": ";
    std::string        clean = stripAnsi(log), out;
    std::istringstream in(clean);
    std::string        line;
    while (std::getline(in, line)) {
        if (line.compare(0, key.str().size(), key.str()) == 0)
            out += line.substr(key.str().size()) + "\r\n";
    }
    return out;
}

// One fake connection: a Client owned by the server's ClientManager (like
// after accept()), fed through this file's own CommandProcessor.
struct Conn;
static std::vector<Conn *> liveConns; // every reply is routed to its Conn

struct Conn {
    Server          &server;
    CommandProcessor cmds;
    Client          *client;
    int              fd;
    bool             closed;
    std::string      pending; // replies queued since the last drain()

    static int nextFd() {
        static int n = 4;
        return n++;
    }

    // unregistered connection from `host`
    Conn(Server &s, int fd_, const std::string &host)
        : server(s), client(0), fd(fd_), closed(false) {
        cmds.registerCommands();
        s.getClientManager().add(fd, host, s);
        client = &s.getClientManager().at(fd);
        liveConns.push_back(this);
    }

    // registered as nick!<first letter>@h, like after PASS/NICK/USER
    Conn(Server &s, const std::string &nick)
        : server(s), client(0), fd(nextFd()), closed(false) {
        cmds.registerCommands();
        s.getClientManager().add(fd, "h", s);
        client = &s.getClientManager().at(fd);
        s.getClientManager().setNick(*client, nick);
        client->setUser(nick.substr(0, 1), nick);
        client->setPassAccepted(true);
        client->setRegistered(true);
        liveConns.push_back(this);
    }

    ~Conn() {
        liveConns.erase(std::remove(liveConns.begin(), liveConns.end(), this),
                        liveConns.end());
    }

    // Runs one line like the event loop; returns true when the handler asks
    // the loop to close the connection (QUIT, wrong password, ...).
    bool send(const std::string &line) {
        std::string log;
        bool        close;
        {
            CoutCapture cap;
            close = cmds.run(server, *client, line);
            log = cap.buf.str();
        }
        closed = closed || close;
        for (size_t i = 0; i < liveConns.size(); ++i)
            if (&liveConns[i]->server == &server)
                liveConns[i]->collect(log);
        return close;
    }

    void collect(const std::string &log) { pending += sentTo(log, fd); }

    std::string drain() {
        std::string s = pending;
        pending.clear();
        return s;
    }
};

