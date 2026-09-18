import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"

N = 20
STALL_IDX = N - 1  # the member that stops reading


def reg(i):
    s = socket.create_connection((HOST, PORT))
    s.sendall(("PASS %s\r\nNICK usr%02d\r\nUSER u%d 0 * :x\r\n"
               % (PW, i, i)).encode())
    time.sleep(0.15)
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
        c = socket.create_connection((HOST, PORT), timeout=1)
        c.close()
        return True
    except OSError:
        return False


def drain(s):
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)


clients = [reg(i) for i in range(N)]
for s in clients:
    s.sendall(b"JOIN #big\r\n")
time.sleep(0.5)
for s in clients:
    drain(s)

print("members in #big:", N, "(one member, usr%02d, stops reading; rest live)" % STALL_IDX)

# broadcast storm: 3 senders x 20 msgs -> each message goes to all members,
# the stalled member's socket buffer fills and server send() starts failing
for i in range(3):
    for k in range(20):
        clients[i].sendall(("PRIVMSG #big :flood %d\r\n" % k).encode())

# JOIN/PART churn on one member
m = clients[10]
for k in range(60):
    m.sendall(b"PART #big\r\n")
    m.sendall(b"JOIN #big\r\n")
    time.sleep(0.005)

# abrupt RST disconnects (no QUIT) mid-flow
for k in (3, 7, 15):
    clients[k].setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                          struct.pack("ii", 1, 0))
    clients[k].close()
    clients[k] = None

time.sleep(1.5)

# every still-healthy, non-stalled member must answer a final PING with PONG
lost = []
for i, s in enumerate(clients):
    if s is None or i == STALL_IDX:
        continue
    drain(s)
    s.sendall(b"PING :final\r\n")
    s.settimeout(2.5)
    ok = False
    got = b""
    try:
        while True:
            c = s.recv(2048)
            if not c:
                break
            got += c
            if b"PONG" in got:
                ok = True
                break
    except (socket.timeout, OSError):
        pass
    if not ok:
        lost.append(i)

alive = server_alive()
for s in clients:
    try:
        if s is not None:
            s.close()
    except Exception:
        pass

print("  server alive     :", "YES" if alive else "NO (DIED)")
print("  healthy members that lost their PING/PONG:", len(lost), (lost or "none"))
print("  RESULT:",
      "PASS" if (alive and not lost) else "FAIL - inspect state above")
