#!/usr/bin/env python3
# Overlong-line tests for the ft_irc reader.
# Run on the machine that is running the server:
#   python3 overlong_test.py [port] [password]
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"


def read_until_quiet(s, timeout):
    s.setblocking(False)
    data = b""
    end = time.time() + timeout
    eof = False
    while time.time() < end:
        try:
            chunk = s.recv(4096)
        except BlockingIOError:
            time.sleep(0.05)
            continue
        if not chunk:
            eof = True
            break
        data += chunk
    s.setblocking(True)
    return data, eof


def connect(nick, user):
    s = socket.create_connection((HOST, PORT))
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER %s 0 * :x\r\n"
               % (PW, nick, user)).encode())
    s.setblocking(False)
    end = time.time() + 2.5
    data = b""
    while time.time() < end:
        try:
            chunk = s.recv(4096)
        except BlockingIOError:
            time.sleep(0.02)
            continue
        if not chunk:
            break
        data += chunk
        if b" 001 " in data:
            break
    s.setblocking(True)
    return s, data


def drain(s):
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)


def ok(cond, text):
    print("  %s %s" % ("ok  " if cond else "FAIL", text))


print("[a 600-byte line and NAMES in one packet]")
s, reg = connect("ovlA", "u1")
drain(s)
s.sendall(b"A" * 600 + b"\r\nNAMES\r\n")
data, eof = read_until_quiet(s, 2.0)
got417 = b"417" in data
got366 = b"366" in data
ok(got417, "417 for the over-long line")
ok(got366, "366 for the NAMES that followed in the same packet")
s.close()

print("[600 bytes three times, never a CRLF]")
s, reg = connect("ovlB", "u2")
drain(s)
all_data = b""
any_closed = False
s.sendall(b"B" * 600)                 # 1st poll: discardMode -> 417
time.sleep(0.3)
d, e = read_until_quiet(s, 0.5)
all_data += d
any_closed = any_closed or e
s.sendall(b"B" * 600)                 # 2nd poll: discard keeps counting
time.sleep(0.3)
s.sendall(b"B" * 600)                 # 3rd poll: limit -> closeFd
d, e = read_until_quiet(s, 1.5)
all_data += d
any_closed = any_closed or e

got417 = b"417" in all_data
ok(got417, "417 was sent")
ok(any_closed, "connection closed after the third over-long chunk")
s.close()
