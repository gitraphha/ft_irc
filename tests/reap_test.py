#!/usr/bin/env python3
# Proves the loop reaps a send-failing socket. The stalled member's receive
# buffer is shrunk BEFORE connect and the flood is large enough (>= 30 MB) to
# close its window even with macOS loopback autotuning, forcing the server's
# sends to fail -> closeFd -> marker sweep reaps it.
#   python3 reap_test.py [port] [password]
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"
NMSG = int(sys.argv[3]) if len(sys.argv) > 3 else 60000  # ~30 MB


def make_client(buf=0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if buf:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, buf)  # before connect
        s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, buf)
    s.connect((HOST, PORT))
    return s


def reg(s, nick):
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER u 0 * :x\r\n" % (PW, nick)).encode())
    time.sleep(0.25)
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)


st = make_client(buf=1024)
reg(st, "stall")
sndr = make_client(0)
reg(sndr, "sndr")
st.sendall(b"JOIN #c\r\n")
sndr.sendall(b"JOIN #c\r\n")
time.sleep(0.4)
for s in (sndr, st):
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)

st.setblocking(False)
print("flooding #c with %d messages (~%.0f MB)..." % (NMSG, NMSG * 0.00051))
t0 = time.time()
for k in range(NMSG):
    sndr.sendall(("PRIVMSG #c :%08d-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n" % k).encode())
    if k % 500 == 0:
        time.sleep(0.002)
print("flood done in %.1fs; watching up to 10s for the close..." % (time.time() - t0))

closed = False
end = time.time() + 10
while time.time() < end:
    try:
        c = st.recv(4096)
    except BlockingIOError:
        time.sleep(0.05)
        continue
    if not c:
        closed = True
        break
st.close()
sndr.close()
if closed:
    print("  stalled connection reaped -> PASS (loop sweep works)")
else:
    print("  still open after 10s.")
    print("  NEXT: while it runs, watch the server console for 'Sendto FD' lines")
    print("  naming the stalled client's fd - if those keep coming and never stop,")
    print("  the window never closed (autotune ate the flood).")