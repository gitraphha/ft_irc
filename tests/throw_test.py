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


print("== Trigger 1: MODE on a channel that does not exist ==")
print("   server up:", "YES" if server_alive() else "NO")
a = reg("t1", "u1")
a.sendall(b"MODE #doesnotexist +o x\r\n")
time.sleep(0.8)
print("   server still up:",
      "YES (no crash; #doesnotexist may be handled without throwing)"
      if server_alive() else "NO -> CRASH reproduced (server died)")
a.close()

print("== Trigger 2: non-op issues +o to an op (two connections) ==")
print("   server up:", "YES" if server_alive() else "NO")
op = reg("op", "u2")
op.sendall(b"JOIN #c\r\n")
time.sleep(0.2)
mem = reg("mem", "u3")
mem.sendall(b"JOIN #c\r\n")
time.sleep(0.2)
mem.sendall(b"MODE #c +o op\r\n")   # member, not op -> Channel.cpp throws 482
time.sleep(0.8)
print("   server still up:",
      "YES (crash fixed)" if server_alive() else "NO -> CRASH reproduced (server died)")
op.close()
mem.close()
