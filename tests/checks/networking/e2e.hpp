#pragma once
/*
 * End-to-end harness: starts ./ircserv on a free port and talks to it over
 * TCP like nc would.
 *
 *   IrcServer srv(rootDir, "secret");   // ./ircserv <port> secret
 *   Conn c(srv.port());                 // one client
 *   c.send("NICK bob");                 // "\r\n" added
 *   std::string got = c.read();         // "" if nothing came
 */

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

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

// a free TCP port: bind to 0 and read back what the kernel picked
static int freePort() {
    int                s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    if (bind(s, (struct sockaddr *)&a, len) != 0 ||
        getsockname(s, (struct sockaddr *)&a, &len) != 0) {
        close(s);
        return 6667;
    }
    int port = ntohs(a.sin_port);
    close(s);
    return port;
}

struct IrcServer {
    pid_t pid;
    int   port_;

    IrcServer(const std::string &rootDir, const std::string &password)
        : pid(-1), port_(freePort()) {
        std::ostringstream p;
        p << port_;
        std::string bin = rootDir + "/ircserv";
        pid = fork();
        if (pid == 0) {
            // server output only with IRC_TEST_VERBOSE=1
            if (!std::getenv("IRC_TEST_VERBOSE")) {
                int devnull = open("/dev/null", O_WRONLY);
                dup2(devnull, 1);
                dup2(devnull, 2);
            }
            execl(bin.c_str(), bin.c_str(), p.str().c_str(), password.c_str(),
                  (char *)0);
            std::perror("execl ./ircserv");
            _exit(127);
        }
        // wait until it accepts connections (up to 3 s)
        for (int i = 0; i < 60; i++) {
            int                s = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in a;
            std::memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_port = htons(port_);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            bool up = connect(s, (struct sockaddr *)&a, sizeof(a)) == 0;
            close(s);
            if (up)
                return;
            usleep(50000);
        }
        std::cerr << "server did not come up on port " << port_ << "\n";
        std::exit(EXIT_FAILURE);
    }

    ~IrcServer() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            int status;
            waitpid(pid, &status, 0);
        }
    }

    int port() const {
        return port_;
    }
};

struct Conn {
    int  fd;
    bool eof; // the server closed the connection

    explicit Conn(int port) : fd(socket(AF_INET, SOCK_STREAM, 0)), eof(false) {
        struct sockaddr_in a;
        std::memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
            std::perror("connect");
            std::exit(EXIT_FAILURE);
        }
    }

    ~Conn() {
        if (fd >= 0)
            close(fd);
    }

    void send(const std::string &line) {
        std::string s = line + "\r\n";
        ::send(fd, s.data(), s.size(), MSG_NOSIGNAL);
    }

    // everything received until the server is quiet for quietMs
    std::string read(int quietMs = 150) {
        std::string   out;
        char          buf[4096];
        struct pollfd pfd = {fd, POLLIN, 0};
        while (poll(&pfd, 1, quietMs) > 0 &&
               (pfd.revents & (POLLIN | POLLHUP))) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                eof = true;
                break;
            }
            out.append(buf, n);
        }
        return out;
    }

    // PASS/NICK/USER, welcome burst discarded
    void login(const std::string &nick, const std::string &pass = "secret") {
        send("PASS " + pass);
        send("NICK " + nick);
        send("USER " + nick.substr(0, 1) + " 0 * :" + nick);
        read();
    }
};
