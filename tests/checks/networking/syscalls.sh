#!/bin/bash
# Runs the server with close/accept/send/recv traced (syscalls.c preloaded) and
# reads the return codes afterwards:
#   FAIL  close() returned -1 (the same fd closed twice) or accept() failed
#   ok    every close() succeeded once
# Other failed calls (send/recv) are listed for information; EAGAIN is normal.
#
# ircserv:        wrong password, QUIT, three over-long lines, an RST-closed
#                 client, a kicked client, SIGINT with clients connected
# ircserv_bonus:  the same, plus a full transfer, an offer that is rejected and
#                 a new one after it, and SIGINT in the middle of a transfer
#
#   bash tests/checks/networking/syscalls.sh [repo root] [password]
# Starts its own servers on free ports (the preload has to reach them).

set -u
ROOT=${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}
PASS=${2:-pass}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$HERE/.syscalls.so"
BIG="$ROOT/files/.syscalls_big.bin"
failed=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; failed=1; }

echo "[trace library]"
if [ "$(uname)" = Darwin ]; then
    cc -shared -o "$LIB" "$HERE/syscalls.c" 2>/dev/null && PRE="DYLD_INSERT_LIBRARIES=$LIB"
else
    cc -shared -fPIC -o "$LIB" "$HERE/syscalls.c" -ldl 2>/dev/null && PRE="LD_PRELOAD=$LIB"
fi
[ -f "$LIB" ] && ok "built ($(basename "$LIB"))" || { bad "could not build syscalls.c (needs cc)"; exit 1; }
cleanup() { [ -n "${SRV:-}" ] && kill $SRV 2>/dev/null; rm -f "${TRACE:-}" "$LIB" "$BIG"; }
trap cleanup EXIT

trace_run() {   # $1 = ircserv | ircserv_bonus
    local bin=$1 bonus=0
    [ "$bin" = ircserv_bonus ] && bonus=1
    [ -x "$ROOT/$bin" ] || { echo "       no $bin at $ROOT, skipped"; return; }
    echo "[$bin: every close path, traced]"
    TRACE=$(mktemp -t syscalls.XXXXXX)
    PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    [ $bonus = 1 ] && head -c 20000000 /dev/urandom > "$BIG"
    ( cd "$ROOT" && exec env $PRE "./$bin" "$PORT" "$PASS" 2>"$TRACE" >/dev/null ) &
    SRV=$!
    for i in $(seq 1 40); do
        python3 -c "import socket; socket.create_connection(('127.0.0.1', $PORT), timeout=0.5).close()" 2>/dev/null && break
        sleep 0.1
    done
    python3 - "$PORT" "$PASS" "$SRV" "$bonus" <<'PYEOF'
import socket, time, re, os, signal, sys, struct
PORT, PW, SRV, BONUS = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4] == "1"
def reg(n):
    s = socket.create_connection(("127.0.0.1", PORT)); s.settimeout(1)
    s.sendall(("PASS %s\r\nNICK %s\r\nUSER %s 0 * :x\r\n" % (PW, n, n)).encode()); time.sleep(0.3)
    try: s.recv(4096)
    except socket.timeout: pass
    return s
def offer(a, b, fname):
    a.sendall(("PRIVMSG DCCBot :!send bob %s\r\n" % fname).encode()); time.sleep(0.5)
    d = b.recv(4096).decode(errors="replace")
    m = re.search(r"DCC SEND \S+ (\S+) (\d+) (\d+)", d)
    return (int(m.group(2)), int(m.group(3))) if m else (None, None)
def pull(port):
    ds = socket.create_connection(("127.0.0.1", port)); ds.settimeout(3); got = 0
    try:
        while True:
            c = ds.recv(65536)
            if not c: break
            got += len(c)
    except socket.timeout: pass
    ds.close(); return got
a = reg("alice"); b = reg("bob"); a.sendall(b"JOIN #x\r\n"); b.sendall(b"JOIN #x\r\n"); time.sleep(0.3)
w = socket.create_connection(("127.0.0.1", PORT)); w.sendall(b"PASS nope\r\nNICK w\r\nUSER w 0 * :w\r\n"); time.sleep(0.4); w.close()
q = reg("quitter"); q.sendall(b"QUIT :bye\r\n"); time.sleep(0.4); q.close()
o = reg("longy")
for _ in range(3): o.sendall(b"X" * 600 + b"\r\n"); time.sleep(0.2)
time.sleep(0.4); o.close()
k = reg("killed"); k.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0)); k.close(); time.sleep(0.4)
a.sendall(b"KICK #x bob :out\r\n"); time.sleep(0.3)
print("  ok   wrong password, QUIT, over-long close, RST close, KICK")
if BONUS:
    b.sendall(b"JOIN #x\r\n"); time.sleep(0.2)
    port, size = offer(a, b, "test.txt"); b.sendall(b"PRIVMSG DCCBot :!accept\r\n"); time.sleep(0.3)
    got = pull(port); time.sleep(0.4)
    print(("  ok   full transfer: %d of %d bytes" if got == size else "  FAIL full transfer: %d of %d bytes") % (got, size))
    offer(a, b, "test.txt"); b.sendall(b"PRIVMSG DCCBot :!reject\r\n"); time.sleep(0.5)
    port, size = offer(a, b, "test.txt"); b.sendall(b"PRIVMSG DCCBot :!accept\r\n"); time.sleep(0.4)
    pull(port); time.sleep(0.4)
    print("  ok   offer rejected, new offer accepted and transferred")
    port, size = offer(a, b, ".syscalls_big.bin"); b.sendall(b"PRIVMSG DCCBot :!accept\r\n"); time.sleep(0.4)
    ds = socket.create_connection(("127.0.0.1", port)); time.sleep(0.6)      # connected, never reads
    os.kill(SRV, signal.SIGINT); time.sleep(1.0)
    print("  ok   SIGINT with a transfer in progress")
else:
    os.kill(SRV, signal.SIGINT); time.sleep(0.8)
    print("  ok   SIGINT with clients connected")
PYEOF
    kill -INT $SRV 2>/dev/null; sleep 0.5; kill $SRV 2>/dev/null; wait $SRV 2>/dev/null; SRV=""
    local closes dbl acc
    closes=$(grep -c "^\[sys\] close(" "$TRACE")
    dbl=$(grep "^\[sys\] close(" "$TRACE" | grep -c -- "-> -1")
    acc=$(grep "^\[sys\] accept(" "$TRACE" | grep -- "-> -1" | grep -vc "again\|Resource temporarily")
    if [ "$dbl" -eq 0 ]; then ok "every close() succeeded once ($closes calls)"; else
        bad "close() returned -1 $dbl time(s): the same fd closed twice"
        grep "^\[sys\] close(" "$TRACE" | grep -B1 -- "-> -1" | sed 's/^/       /'
    fi
    [ "$acc" -eq 0 ] && ok "no failed accept()" || bad "accept() failed $acc time(s)"
    grep "^\[sys\] \(send\|recv\)(" "$TRACE" | grep -- "-> -1" | sed 's/^\[sys\] //' | sort | uniq -c | sed 's/^ *\([0-9]*\) \(.*\)$/       \2  x\1  (info)/'
    rm -f "$TRACE" "$BIG"
}
trace_run ircserv
trace_run ircserv_bonus
exit $failed
