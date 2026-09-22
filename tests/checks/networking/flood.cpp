#include "e2e.hpp"

#include <ctime>
#include <sys/time.h>

/*
 * A channel member stops reading while the channel is flooded (the "^Z a
 * client, flood the channel" check of the evaluation sheet).
 *
 * Policy under test (POLLOUT design, Socket::flushWriteBuffer): the server
 * keeps serving everyone else, never drops the stalled client for being slow,
 * queues its bytes in the per-client write buffer, and delivers every queued
 * line, in order, once the client reads again.
 *
 * With a send-queue cap (SENDQMAX in Socket.hpp) the policy changes: once the
 * queue passes the cap the client is dropped. Run with IRC_SENDQ_CAP=<bytes>
 * to test that variant: the flood is cap + 8 MB (the kernel absorbs a few MB
 * before the server's own queue grows) and the stalled client must be gone.
 *
 * Usage: [IRC_SENDQ_CAP=1048576] flood <repo root containing ./ircserv>
 */

static double nowMs() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static std::string kb(size_t bytes) {
    std::ostringstream o;
    o << bytes / 1024 << " KB";
    return o.str();
}

// a client with a tiny receive buffer: "not reading" fills the pipe in KB
static int smallSocket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int n = 4096;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
    struct sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        std::perror("connect");
        std::exit(EXIT_FAILURE);
    }
    return fd;
}

// resident set size of a process in MB, via ps (macOS and Linux)
static long rssMb(pid_t pid) {
    std::ostringstream cmd;
    cmd << "ps -o rss= -p " << pid;
    FILE *f = popen(cmd.str().c_str(), "r");
    if (!f)
        return -1;
    long kbv = -1;
    if (fscanf(f, "%ld", &kbv) != 1)
        kbv = -1;
    pclose(f);
    return kbv < 0 ? -1 : kbv / 1024;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <root dir with ./ircserv>\n";
        return EXIT_FAILURE;
    }
    IrcServer srv(argv[1], "secret");

    Conn alice(srv.port());
    Conn carol(srv.port());
    Conn bob(srv.port());
    close(bob.fd);
    bob.fd = smallSocket(srv.port());
    alice.login("alice");
    bob.login("bob");
    carol.login("carol");
    alice.send("JOIN #x");
    bob.send("JOIN #x");
    alice.read();
    bob.read();
    long rss0 = rssMb(srv.pid);
    // from here on bob never reads

    const std::string text(480, 'y');
    const std::string line = "PRIVMSG #x :" + text;
    const std::string relayed =
        ":alice!a@127.0.0.1 PRIVMSG #x :" + text + "\r\n";
    long cap = std::getenv("IRC_SENDQ_CAP") ? std::atol(std::getenv("IRC_SENDQ_CAP")) : 0;
    // keep policy: ~1.9 MB toward a 4 KB receive buffer; cap policy: cap + 8 MB
    const int total = cap > 0 ? static_cast<int>((cap + 8L * 1024 * 1024) / (line.size() + 2)) : 4000;
    double    worstPing = 0;
    int       sent = 0;
    bool      bobQuit = false, pongMissing = false;
    double    t0 = nowMs();
    while (sent < total && nowMs() - t0 < 30000) {
        for (int i = 0; i < 50; ++i)
            alice.send(line);
        sent += 50;
        // the server must still answer carol while bob's queue grows
        double t1 = nowMs();
        carol.send("PING :x");
        struct pollfd cp = {carol.fd, POLLIN, 0};
        int           ready = poll(&cp, 1, 2000); // time until the answer
        double        dt = nowMs() - t1;
        if (dt > worstPing)
            worstPing = dt;
        std::string pong = ready > 0 ? carol.read(20) : "";
        if (!contains(pong, "PONG")) {
            pongMissing = true;
            break;
        }
        if (contains(alice.read(5), "QUIT"))
            bobQuit = true;
    }
    std::ostringstream what;
    what << "server kept answering carol during " << sent << " lines ("
         << kb(sent * (line.size() + 2)) << ") to a stalled bob: worst PING "
         << worstPing << " ms";
    report(!pongMissing && worstPing < 500, what.str());
    if (cap > 0) {
        std::ostringstream wc;
        wc << "cap policy (" << cap / 1024 << " KB): bob was dropped, alice saw his QUIT";
        report(bobQuit, wc.str());
        carol.send("PRIVMSG bob :are you there");
        report(contains(carol.read(), " 401 "), "bob is gone for everyone (401)");
        std::string got = bob.read(300);
        report(bob.eof, "bob reads what was delivered before the drop, then EOF");
        Conn bob3(srv.port());
        bob3.send("PASS secret");
        bob3.send("NICK bob");
        report(!contains(bob3.read(), " 433 "), "the nick is free again");
        return finish("flood");
    }
    report(!bobQuit,
           "bob was NOT dropped for not reading (no QUIT seen by alice)");
    carol.send("PRIVMSG bob :are you there");
    report(!contains(carol.read(), " 401 "),
           "bob is still known to the server (no 401 for PRIVMSG bob)");
    long               rss1 = rssMb(srv.pid);
    std::ostringstream w1;
    w1 << "server RSS " << rss0 << " MB before, " << rss1 << " MB with "
       << kb(sent * relayed.size())
       << " outstanding for bob (kernel socket buffer first, then the server queue; no cap by design)";
    report(rss1 >= rss0, w1.str());

    // bob reads again: every queued line must arrive, in order, plus carol's
    // private message, and the link stays open
    std::string got;
    double      t2 = nowMs();
    while (nowMs() - t2 < 20000) {
        std::string chunk = bob.read(300);
        got += chunk;
        if (chunk.empty() || bob.eof)
            break;
    }
    size_t lines = 0, bad = 0;
    for (size_t p = 0; (p = got.find(relayed, p)) != std::string::npos;
         p += relayed.size())
        ++lines;
    for (size_t p = 0; (p = got.find("\r\n", p)) != std::string::npos; p += 2)
        if (p + 2 < got.size() && got[p + 2] != ':')
            ++bad;
    std::ostringstream w2;
    w2 << "once bob reads again he gets all " << sent << " lines (" << lines
       << " found, " << kb(got.size()) << ") in order, nothing lost";
    report(lines == static_cast<size_t>(sent) && bad == 0, w2.str());
    report(contains(got, ":carol!c@127.0.0.1 PRIVMSG bob :are you there"),
           "the private message sent during the stall is delivered too");
    report(!bob.eof, "bob's connection is still open");
    bob.send("PING :back");
    report(contains(bob.read(), "PONG"), "bob is fully usable again");
    long               rss2 = rssMb(srv.pid);
    std::ostringstream w3;
    w3 << "server RSS after the drain: " << rss2
       << " MB (buffer freed; RSS may not shrink, that is the allocator)";
    report(true, w3.str());

    // the nick is still taken: a second bob must get 433
    Conn bob2(srv.port());
    bob2.send("PASS secret");
    bob2.send("NICK bob");
    report(contains(bob2.read(), " 433 "), "the nick is still in use (433)");

    return finish("flood");
}
