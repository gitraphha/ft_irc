#include "Logger.hpp"
#include "Socket.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Socket: line buffering, the 510-character limit and the send path, over a
 * socketpair. The test writes one end like a client and drives the other
 * through updateBuffer_ / getCRLFLineFromBuffer like the poll loop does.
 *
 * Send path (POLLOUT design): sendMsg() only appends to the per-fd write
 * buffer; nothing touches the socket until the loop sees POLLOUT and calls
 * flushWriteBuffer(). A client that stops reading is never dropped for that:
 * its bytes wait in the buffer (hasPendingWrite) until it reads again.
 */

static int failures = 0;

static std::string toStr(size_t n) {
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

static void report(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

// fd is the server side (registered in Socket), peer the client side.
struct Conn {
    Logger logger;
    Socket sock;
    int    fd;
    int    peer;
    bool   more;
    bool   tooLong;

    Conn() : logger("test: "), sock(logger), more(false), tooLong(false) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            std::cerr << "socketpair failed\n";
            std::exit(EXIT_FAILURE);
        }
        fd = sv[0];
        peer = sv[1];
        fcntl(fd, F_SETFL, O_NONBLOCK); // like every client fd in Server
        sock.addSocket(fd, false);
    }

    ~Conn() {
        sock.closeSocket(fd);
        close(peer);
    }

    // write as the client, then let Socket recv() it
    bool feed(const std::string &bytes) {
        if (!bytes.empty())
            write(peer, bytes.data(), bytes.size());
        struct pollfd pfd = {fd, POLLIN, POLLIN};
        return sock.updateBuffer_(pfd);
    }

    std::string line() {
        return sock.getCRLFLineFromBuffer(fd, more, tooLong);
    }

    // what the server side sent so far, without blocking
    std::string readPeer() {
        char          buf[4096];
        std::string   out;
        struct pollfd pfd = {peer, POLLIN, 0};
        while (poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(peer, buf, sizeof(buf));
            if (n <= 0)
                break;
            out.append(buf, n);
        }
        return out;
    }
};

// tiny kernel buffers: a few KB behave like a client that stopped reading
static void smallPipe(Conn &c) {
    int n = 4096;
    setsockopt(c.fd, SOL_SOCKET, SO_SNDBUF, &n, sizeof(n));
    setsockopt(c.peer, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
}

static void expectLine(Conn &c, const std::string &want) {
    std::string got = c.line();
    std::string what = want.size() > 40
                           ? "line of " + toStr(want.size()) + " chars"
                           : "line is \"" + want + "\"";
    if (got != want)
        what += got.size() > 40 ? ", got " + toStr(got.size()) + " chars"
                                : ", got \"" + got + "\"";
    report(got == want && !c.tooLong, what);
}

int main() {

    {
        std::cout << "[one complete line]\n";
        Conn c;
        c.feed("NICK bob\r\n");
        expectLine(c, "NICK bob");
        report(!c.more, "nothing left in the buffer afterwards");
    }

    // TCP may split one command across several recv() calls.
    {
        std::cout << "[command split across three recv() calls]\n";
        Conn c;
        c.feed("NI");
        report(c.line().empty() && !c.more, "nothing yet after \"NI\"");
        c.feed("CK bo");
        report(c.line().empty() && !c.more, "still nothing after \"CK bo\"");
        c.feed("b\r\n");
        expectLine(c, "NICK bob");
    }

    // One recv() may contain several complete commands.
    {
        std::cout << "[three commands in one chunk]\n";
        Conn c;
        c.feed("PASS x\r\nNICK bob\r\nUSER b 0 * :B\r\n");
        expectLine(c, "PASS x");
        report(c.more, "buffer reports more commands");
        expectLine(c, "NICK bob");
        expectLine(c, "USER b 0 * :B");
        report(!c.more, "buffer is empty afterwards");
    }

    {
        std::cout << "[complete line plus a partial one]\n";
        Conn c;
        c.feed("NICK bob\r\nUSE");
        expectLine(c, "NICK bob");
        report(c.line().empty() && !c.more,
               "\"USE\" is held until the rest arrives");
        c.feed("R b 0 * :B\r\n");
        expectLine(c, "USER b 0 * :B");
    }

    // A bare '\n' (nc without -c, most Linux nc) ends a line as well.
    {
        std::cout << "[bare newline from nc]\n";
        Conn c;
        c.feed("NICK bob\nUSER b 0 * :B\n");
        expectLine(c, "NICK bob");
        expectLine(c, "USER b 0 * :B");
    }

    {
        std::cout << "[empty line]\n";
        Conn c;
        c.feed("\r\nNICK bob\r\n");
        report(c.line().empty() && c.more,
               "empty line comes out empty, next one still pending");
        expectLine(c, "NICK bob");
    }

    // 510 characters of content, whatever the line ending (RFC 2812: 512
    // bytes including "\r\n").
    {
        std::cout << "[510-character limit]\n";
        const std::string ok510(510, 'a');
        const std::string long511(511, 'b');
        const std::string long512(512, 'c');
        Conn              c;
        c.feed(ok510 + "\r\n");
        expectLine(c, ok510);
        report(!c.tooLong, "510 chars + CRLF is accepted");

        c.feed(ok510 + "\n");
        expectLine(c, ok510);
        report(!c.tooLong, "510 chars + bare LF is accepted");

        c.feed(long511 + "\r\n");
        std::string got = c.line();
        report(got.empty() && c.tooLong, "511 chars + CRLF is rejected");

        c.feed("NICK ok\r\n");
        expectLine(c, "NICK ok");
        c.feed(long511 + "\n");
        got = c.line();
        report(got.empty() && c.tooLong, "511 chars + bare LF is rejected");
        report(!c.sock.socketCloseMarker(c.fd),
               "one over-long line does not close the socket");

        c.feed("NICK bob\r\n");
        expectLine(c, "NICK bob");
        report(true, "a normal line after an over-long one works again");

        c.feed(long512 + "\r\n");
        c.line();
        c.feed(long512 + "\r\n");
        c.line();
        report(!c.sock.socketCloseMarker(c.fd),
               "two more over-long lines: still open (limit is 3 in a row)");
        c.feed(long512 + "\r\n");
        c.line();
        report(c.sock.socketCloseMarker(c.fd),
               "third over-long line in a row marks the socket for closing");
    }

    {
        std::cout << "[client closes the connection]\n";
        Conn c;
        close(c.peer);
        c.peer = -1;
        report(!c.feed(""), "updateBuffer_ returns false when recv() gives 0");
        report(c.sock.socketCloseMarker(c.fd), "socket is marked for closing");
        c.peer = dup(c.fd); // so the destructor has something to close
    }

    {
        std::cout << "[sendMsg queues, flushWriteBuffer sends]\n";
        Conn c;
        c.sock.sendMsg(c.fd, "PONG irc.local :x\r\n");
        report(c.readPeer().empty(),
               "sendMsg alone writes nothing to the socket (waits for POLLOUT)");
        report(c.sock.hasPendingWrite(c.fd), "the message is pending");
        report(c.sock.flushWriteBuffer(c.fd), "flush on POLLOUT sends it all");
        report(c.readPeer() == "PONG irc.local :x\r\n",
               "message reaches the client unchanged");
        report(!c.sock.hasPendingWrite(c.fd), "nothing pending afterwards");
        c.sock.sendMsg(c.fd, ":a NOTICE b :1\r\n");
        c.sock.sendMsg(c.fd, ":a NOTICE b :2\r\n");
        c.sock.flushWriteBuffer(c.fd);
        report(c.readPeer() == ":a NOTICE b :1\r\n:a NOTICE b :2\r\n",
               "two messages arrive in order");
        c.sock.sendMsg(999, "lost\r\n");
        report(!c.sock.hasPendingWrite(999) && c.sock.flushWriteBuffer(999),
               "sendMsg/flush on an unknown fd are ignored");
    }

    {
        std::cout << "[a client that never reads is kept, its bytes wait]\n";
        Conn c;
        smallPipe(c);
        const std::string big(4000, 'z');
        size_t            queued = 0;
        for (int i = 0; i < 100; ++i) {          // 400 KB toward a 4 KB pipe
            c.sock.sendMsg(c.fd, big + "\r\n");
            queued += big.size() + 2;
        }
        report(!c.sock.socketCloseMarker(c.fd),
               "queueing 400 KB for a stalled client does not mark it for closing");
        bool drained = c.sock.flushWriteBuffer(c.fd);
        report(!drained, "flush stops when the kernel buffer is full (send would block)");
        report(!c.sock.socketCloseMarker(c.fd),
               "a blocked send is not an error: the client is NOT dropped");
        report(c.sock.hasPendingWrite(c.fd), "the rest stays pending (POLLOUT interest)");
        std::string got = c.readPeer();
        report(!got.empty() && got.size() < queued,
               "the client can read what fitted in the kernel buffer (" +
                   toStr(got.size()) + " of " + toStr(queued) + " bytes)");
        report(got.compare(0, big.size(), big) == 0,
               "the part that was delivered is intact");
    }

    {
        std::cout << "[the backlog drains as the client reads (one flush per POLLOUT)]\n";
        Conn c;
        smallPipe(c);
        const std::string big(65536, 'q'); // far more than the pipe holds
        c.sock.sendMsg(c.fd, big + "\r\n");
        c.sock.sendMsg(c.fd, "PING :x\r\n");
        c.sock.flushWriteBuffer(c.fd);
        std::string got = c.readPeer();
        report(!got.empty() && got.size() < big.size() + 2,
               "only part of a 64 KB message reaches the client at once");
        report(c.readPeer().empty(),
               "the rest does not arrive without another flush");
        int flushes = 0;
        while (c.sock.hasPendingWrite(c.fd) && flushes < 1000) {
            c.sock.flushWriteBuffer(c.fd);       // the loop does this on POLLOUT
            ++flushes;
            got += c.readPeer();                 // the client reads
        }
        report(!c.sock.hasPendingWrite(c.fd), "everything drained after " +
                                                  toStr(flushes) + " POLLOUT flushes");
        report(got == big + "\r\nPING :x\r\n",
               "bytes arrive in order, nothing lost or duplicated");
    }

    {
        std::cout << "[a client marked for closing gets nothing more]\n";
        Conn c;
        c.sock.sendMsg(c.fd, "one\r\n");
        c.sock.flushWriteBuffer(c.fd);
        report(c.readPeer() == "one\r\n", "before: messages flow");
        // the loop marks fds via Socket only; simulate the marker the way
        // sendMsg does when the buffer can no longer grow
        report(c.sock.getSockIOData(c.fd) != 0, "per-fd data exists");
        c.sock.getSockIOData(c.fd)->closeFd = true;
        c.sock.sendMsg(c.fd, "two\r\n");
        report(!c.sock.hasPendingWrite(c.fd) && c.sock.socketCloseMarker(c.fd),
               "sendMsg after the marker queues nothing; marker stays set");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
