import socket
import sys
import time

host = "localhost"
port = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
password = sys.argv[2] if len(sys.argv) > 2 else "pass"

def connect(nick, user):
    s = socket.create_connection((host, port))
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER %s 0 * :x\r\n"
               % (password, nick, user)).encode())
    # read until registration completes (welcome numerics) so delivery works
    time.sleep(0.3)
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)
    return s

# A: registers, then NEVER reads again (stalls its TCP receive window)
a = connect("stalled", "u1")

# B: floods A with enough max-length PRIVMSGs to fill A's kernel buffer
b = connect("flooder", "u2")
body = "x" * 480
for i in range(400):
    b.sendall(("PRIVMSG stalled :%s\r\n" % body).encode())
    time.sleep(0.01)

print("flood sent; A is silently stalled. sleeping 3s...")
time.sleep(3)
print("done. check lsof now: A should be gone with the sweep, present without it")
# leave sockets open so A lingers if leaked
time.sleep(60)