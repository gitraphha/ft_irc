#!/usr/bin/env python3
# Reproduces the uncaught-Error crash from command handlers.
# Run on the machine running the server:  python3 throw_test.py [port] [password]
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"


def reg(nick, user):
    s = socket.create_connection((HOST, PORT))
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER %s 0 * :x\r\n"
               % (PW, nick, user)).encode())
    time.sleep(0.3)
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)
    return s


def server_alive():
    try:
        t = socket.create_connection((HOST, PORT), timeout=1)
        t.close()
        return True
    except OSError:
        return False


def ok(cond, text):
    print("  %s %s" % ("ok  " if cond else "FAIL", text))


print("[MODE on a channel that does not exist]")
ok(server_alive(), "server up before")
a = reg("t1", "u1")
a.sendall(b"MODE #doesnotexist +o x\r\n")
time.sleep(0.8)
ok(server_alive(), "server still up after MODE #doesnotexist +o x")
a.close()

print("[a member who is not operator sends MODE #c +o]")
ok(server_alive(), "server up before")
op = reg("op", "u2")
op.sendall(b"JOIN #c\r\n")
time.sleep(0.2)
mem = reg("mem", "u3")
mem.sendall(b"JOIN #c\r\n")
time.sleep(0.2)
mem.sendall(b"MODE #c +o op\r\n")   # member, not op -> Channel.cpp throws 482
time.sleep(0.8)
ok(server_alive(), "server still up after the 482 path")
op.close()
mem.close()
