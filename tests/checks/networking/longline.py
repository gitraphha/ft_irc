#!/usr/bin/env python3
# Three clients each dump 10k chars WITHOUT CRLF (in 3 chunks -> discard-close),
# while a normal 4th client must stay responsive throughout.
#   python3 longline_test.py [port] [password]
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"


def reg(nick, i):
    s = socket.create_connection((HOST, PORT))
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER u%d 0 * :x\r\n"
               % (PW, nick, i)).encode())
    time.sleep(0.2)
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)
    return s


def ok(cond, text):
    print("  %s %s" % ("ok  " if cond else "FAIL", text))


print("[three clients dump 10 KB without CRLF, a fourth stays normal]")
dumpy = [reg("dump%d" % i, i) for i in range(3)]
norm = reg("norm", 100)

for s in dumpy:
    s.sendall(b"X" * 4000)
time.sleep(0.05)
for s in dumpy:
    s.sendall(b"Y" * 4000)
time.sleep(0.05)
for s in dumpy:
    s.sendall(b"Z" * 2000)   # 3rd event -> discard limit closes them

t0 = time.time()
norm.sendall(b"PING :hi\r\n")
norm.settimeout(2)
got_pong = False
try:
    while True:
        c = norm.recv(2048)
        if not c:
            break
        if b"PONG" in c:
            got_pong = True
            break
except socket.timeout:
    pass
lat = time.time() - t0
ok(got_pong, "the normal client gets its PONG (%.3f s)" % lat)
closed = []
for i, s in enumerate(dumpy):
    s.settimeout(2)
    try:
        while True:
            c = s.recv(4096)
            if not c:
                closed.append(i)
                break
    except socket.timeout:
        pass
ok(len(closed) == 3, "the three dumpers are closed after their third chunk (%d of 3)" % len(closed))