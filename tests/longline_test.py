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


print("opening 3 dumpers and 1 normal client...")
dumpy = [reg("dump%d" % i, i) for i in range(3)]
norm = reg("norm", 100)

print("3 dumpers hold a 10k-no-CRLF line (3 chunks each)...")
for s in dumpy:
    s.sendall(b"X" * 4000)
time.sleep(0.05)
for s in dumpy:
    s.sendall(b"Y" * 4000)
time.sleep(0.05)
for s in dumpy:
    s.sendall(b"Z" * 2000)   # 3rd event -> discard limit closes them

print("now the normal client PINGs - must be answered promptly:")
t0 = time.time()
norm.sendall(b"PING :hi\r\n")
norm.settimeout(2)
ok = False
try:
    while True:
        c = norm.recv(2048)
        if not c:
            break
        if b"PONG" in c:
            ok = True
            break
except socket.timeout:
    pass
lat = time.time() - t0
print("  PONG latency: %.3fs  %s" % (lat, "OK" if ok else "TIMED OUT"))

print("do the dumpers get closed after their 3rd chunk?")
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
print("  closed dumpers:", len(closed), "(expect 3)",
      "PASS" if len(closed) == 3 else "FAIL")

if ok and len(closed) == 3:
    print("RESULT: PASS - server stayed responsive and reaped the abusers")
else:
    print("RESULT: FAIL - server was blocked or did not close dumpers")