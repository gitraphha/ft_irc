import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"


def server_alive():
    try:
        c = socket.create_connection((HOST, PORT), timeout=1)
        c.close()
        return True
    except OSError:
        return False


def probe(idx, payload):
    s = socket.create_connection((HOST, PORT))
    s.sendall(("PASS %s\r\nNICK probe%d\r\nUSER u 0 * :x\r\n"
               % (PW, idx)).encode())
    time.sleep(0.3)
    s.setblocking(False)
    try:
        while s.recv(4096):
            pass
    except BlockingIOError:
        pass
    s.setblocking(True)
    s.sendall(payload)
    time.sleep(0.4)
    s.setblocking(False)
    data = b""
    eof = False
    end = time.time() + 0.6
    while time.time() < end:
        try:
            chunk = s.recv(4096)
        except BlockingIOError:
            time.sleep(0.02)
            continue
        if not chunk:
            eof = True
            break
        data += chunk
    s.setblocking(True)
    s.close()
    return server_alive(), eof, data


CASES = [
    ("NUL byte in line",        b"PING \x00probe\r\n"),
    ("raw binary (256 bytes)",  bytes(range(256)) * 4),
    ("trailing CR (CRCRLF)",    b"PING :ok\r\r\n"),
    ("bare LF line",            b"PING :lf\n"),
    ("two bare LF lines",       b"PING :a\nPING :b\n"),
    ("only CRs",                b"\r\r\r\r"),
    ("empty lines",             b"\r\n\r\n\r\n"),
    ("spaces-only line",        b"    \r\n"),
    ("exact max line (510)",    b"A" * 510 + b"\r\nPING :ok\r\n"),
    ("one over max (511)",      b"A" * 511 + b"\r\nPING :ok\r\n"),
    ("huge line + trailing cmd", b"B" * 4096 + b"\r\nPING :ok\r\n"),
    ("re-register PASS",        b"PASS pass\r\nPING :ok\r\n"),
    ("long NICK (400 chars)",   b"NICK " + b"n" * 400 + b"\r\nPING :ok\r\n"),
    ("50 noisy PRIVMSGs",       (b"PRIVMSG #ghost :" + b"y" * 480 + b"\r\n") * 50),
]

print("case                        alive  closed  reply")
print("-" * 70)
all_ok = True
for i, (name, payload) in enumerate(CASES):
    alive, eof, data = probe(i, payload)
    first = repr(data[:40]) if data else "(nothing)"
    if not alive:
        all_ok = False
        mark = "*** SERVER DIED ***"
    else:
        mark = "ok"
    print("%-26s  %-5s  %-7s  %-45s %s"
          % (name, "yes" if alive else "NO", "yes" if eof else "no",
             first, mark))
print("-" * 70)
print("server survived every probe:", "YES" if all_ok else "NO - fix the crash")
