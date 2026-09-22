#!/usr/bin/env python3
"""
irctop -- a btop-style live dashboard for ircserv.

  LAUNCH mode (full picture): irctop starts the server on a pty and reads its
  log live, so it sees nicks, channels, every message, the bot and the DCC
  transfers, on top of what the OS shows (CPU, RSS, kernel send-queues):

      python3 irctop.py --start ../../ircserv 6667 pass
      python3 irctop.py --start ../../ircserv_bonus 6667 pass

  ATTACH mode (server already running somewhere else): only the outside view --
  CPU/RSS, TCP connections on the port with their kernel Send-Q, probe latency:

      python3 irctop.py --attach 6667 pass

Panels (fixed layout, each keeps its place; the log scrolls inside its own box):

  header      pid, port, uptime, poll() call sites found in the sources, probe
              round-trip in ms, server state
  CPU / MEM   gauge + 60 s graph of the server process (from /proc or ps)
  NET         bytes/s in and out of the server (from the log payloads)
  MESSAGES    IRC lines/s received and sent
  SYSCALLS    poll / accept / recv / send / close per second. Live counts need
              strace (Linux). Without it the panel shows the counts derived from
              the server log (recv/send lines, accepts, closes) and says so.
  CONNECTIONS current, accepted, closed, stalled (kernel Send-Q growing), the
              poll set (listeners + clients + transfer sockets), probe latency
  BOT / DCC   bot commands seen, current offer, transfer progress and result
  CLIENTS     per client: fd, nick, in/out lines, bytes out, kernel SendQ, idle,
              channels  (red = stalled)
  CHANNELS    members per channel
  SERVER LOG  scrolling, colour-coded

Keys: Tab cycles the focus clients -> log -> menu bar. In the menu bar: left/right
      pick an option (sort, graphs on/off, graph height, clients height, log
      follow/paused, glyphs unicode/ascii, snapshot, quit), Enter or up/down
      change it, b goes back. Up/down scroll the focused pane, End follows the
      log again, q quits. Shortcuts still work: c sort, p pause, g graphs,
      -/= graph height, [/] clients height, t snapshot.

--text N   prints N plain-text snapshots one second apart instead of the UI.
--no-strace  never start strace (Linux only feature).

stdlib only. macOS and Linux.
"""

import argparse
import collections
import os
import re
import select
import socket
import subprocess
import sys
import threading
import time

try:
    import curses
    HAVE_CURSES = True
except ImportError:
    HAVE_CURSES = False

try:
    import pty
    HAVE_PTY = True
except ImportError:
    HAVE_PTY = False

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
HIST = 120                     # seconds of history kept for the graphs
SPARK = "▁▂▃▄▅▆▇█"
BLOCKS = " ▁▂▃▄▅▆▇█"


def strip_ansi(s):
    return ANSI_RE.sub("", s)


def human(n):
    n = float(n)
    for unit in ("B", "K", "M", "G"):
        if n < 1024 or unit == "G":
            return ("%d%s" % (n, unit)) if unit == "B" else ("%.0f%s" % (n, unit))
        n /= 1024.0
    return "%.0fG" % n


def human_rate(n):
    n = float(n)
    if n < 1024:
        return "%d B/s" % n
    if n < 1024 * 1024:
        return "%.1f KB/s" % (n / 1024)
    return "%.2f MB/s" % (n / 1024 / 1024)


def spark(values, width):
    vals = list(values)[-width:]
    if not vals:
        return ""
    hi = max(vals) or 1
    return "".join(SPARK[min(7, int(v * 7 / hi))] for v in vals)


def graph_rows(values, width, rows, hi=None):
    """Multi-row block graph like btop. Returns `rows` strings of `width`
    chars, top row first, and the scale max used."""
    vals = list(values)[-width:]
    vals = [0] * (width - len(vals)) + vals
    top = hi if hi else (max(vals) if vals else 0)
    if not top:
        top = 1
    out = []
    levels = [min(rows * 8, int(round(v * rows * 8 / float(top)))) for v in vals]
    for r in range(rows - 1, -1, -1):          # r = row index from the bottom
        line = []
        for lv in levels:
            k = lv - r * 8
            line.append(BLOCKS[max(0, min(8, k))])
        out.append("".join(line))
    return out, top


def meter(width, frac):
    frac = max(0.0, min(1.0, frac))
    n = int(round(frac * width))
    return "█" * n + "░" * (width - n)


# ===========================================================================
# State derived from the server log + samplers
# ===========================================================================

class Client(object):
    __slots__ = ("fd", "peer", "nick", "user", "registered", "chans", "msgs_in",
                 "msgs_out", "bytes_out", "bytes_in", "last", "since", "sendq", "closed",
                 "sendq_prev", "stall_ticks")

    def __init__(self, fd, peer):
        self.fd = fd
        self.peer = peer            # "127.0.0.1:47088"
        self.nick = ""
        self.user = ""
        self.registered = False
        self.chans = set()
        self.msgs_in = 0
        self.msgs_out = 0
        self.bytes_in = 0
        self.bytes_out = 0
        self.last = time.time()
        self.since = time.time()
        self.sendq = 0
        self.sendq_prev = 0
        self.stall_ticks = 0
        self.closed = False


class Offer(object):
    __slots__ = ("file", "ip", "port", "size", "frm", "to", "status", "t0", "t1", "sent")

    def __init__(self, file, ip, port, size, frm, to):
        self.file, self.ip, self.port, self.size = file, ip, port, size
        self.frm, self.to = frm, to
        self.status = "offered"
        self.t0 = time.time(); self.t1 = None
        self.sent = 0


class State(object):
    def __init__(self):
        self.lock = threading.Lock()
        self.clients = {}           # fd -> Client
        self.log = collections.deque(maxlen=4000)   # (t, kind, text)
        # per-second histories
        self.in_hist = collections.deque(maxlen=HIST)      # lines in
        self.out_hist = collections.deque(maxlen=HIST)     # lines out
        self.bin_hist = collections.deque(maxlen=HIST)     # bytes in
        self.bout_hist = collections.deque(maxlen=HIST)    # bytes out
        self.rss_hist = collections.deque(maxlen=HIST)
        self.cpu_hist = collections.deque(maxlen=HIST)
        self.fd_hist = collections.deque(maxlen=HIST)
        self.conn_hist = collections.deque(maxlen=HIST)    # current connections
        self.probe_hist = collections.deque(maxlen=HIST)
        self.sys_hist = {k: collections.deque(maxlen=HIST) for k in ("poll", "accept", "recv", "send")}
        self.sys_cur = {k: 0 for k in self.sys_hist}
        self.sys_total = {k: 0 for k in self.sys_hist}
        self.sys_source = "log"     # "strace" | "log" | "none"
        self.sys_note = ""
        self.cur_in = 0; self.cur_out = 0; self.cur_bin = 0; self.cur_bout = 0
        self.total_in = 0; self.total_out = 0; self.total_bin = 0; self.total_bout = 0
        self.accepts = 0; self.closes = 0
        self.rss_mb = 0
        self.cpu = 0.0
        self.pid = None
        self.port = None
        self.started = time.time()
        self.errors = 0
        self.probe_ms = None
        self.fds = -1; self.socks = -1
        self.mode = "launch"
        self.paused = False
        self.scroll = 0
        self.sort = 0               # 0 fd, 1 traffic, 2 sendq
        self.status = ""
        self.poll_sites = None      # (count, [(file, line)]) from the source scan
        # bot / dcc
        self.bot_nick = ""
        self.bot_cmds = 0
        self.bot_last = ""
        self.bot_replies = 0
        self.ctcp = 0
        self.offer = None
        self.offers_done = 0
        self.dcc_fd = None
        self.dcc_sendq = 0
        self.strace_bytes = {}      # fd -> bytes sent (strace only)

    # ---- log parsing --------------------------------------------------------

    _re_conn = re.compile(r"Client connected: fd:(\d+) .*?addr:port: (\S+)")
    _re_from = re.compile(r"From FD (\d+): (.*)$")
    _re_sendto = re.compile(r"Sendto FD (\d+): (.*)$")
    _re_close = re.compile(r"(?:Closing client sockFd:|Client sockFd:)(\d+)")
    _re_dcc = re.compile(r"DCC SEND (\S+) (\S+) (\d+) (\d+)")

    def feed(self, raw):
        """One line of server output (ANSI already stripped)."""
        t = time.time()
        line = raw.rstrip("\r\n")
        line = re.sub(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ", "", line)
        line = "".join(ch if ch >= " " else "^" + chr(ord(ch) + 64) for ch in line)
        kind = "info"
        low = line.lower()
        if "recvmsg event" in low or "sendmsg event" in low:
            return                  # the header line; the payload follows
        with self.lock:
            m = self._re_conn.search(line)
            if m:
                fd = int(m.group(1))
                self.clients[fd] = Client(fd, m.group(2))
                self.accepts += 1
                if self.sys_source == "log":
                    self.sys_cur["accept"] += 1; self.sys_total["accept"] += 1
                kind = "conn"
            m = self._re_from.search(line)
            if m:
                fd = int(m.group(1)); payload = m.group(2)
                c = self.clients.get(fd)
                if c is None:
                    c = self.clients[fd] = Client(fd, "?")
                n = len(payload) + 2
                c.msgs_in += 1; c.bytes_in += n; c.last = t
                self.cur_in += 1; self.total_in += 1; self.cur_bin += n; self.total_bin += n
                if self.sys_source == "log":
                    self.sys_cur["recv"] += 1; self.sys_total["recv"] += 1
                self._on_in(c, payload)
                kind = "in"
            m = self._re_sendto.search(line)
            if m:
                fd = int(m.group(1)); payload = m.group(2)
                c = self.clients.get(fd)
                if c is None:
                    c = self.clients[fd] = Client(fd, "?")
                n = len(payload) + 2
                c.msgs_out += 1; c.bytes_out += n; c.last = t
                self.cur_out += 1; self.total_out += 1; self.cur_bout += n; self.total_bout += n
                if self.sys_source == "log":
                    self.sys_cur["send"] += 1; self.sys_total["send"] += 1
                self._on_out(c, payload)
                kind = "out"
            m = self._re_close.search(line)
            if m and ("closing client" in low or "closed connection" in low):
                fd = int(m.group(1))
                c = self.clients.get(fd)
                if c:
                    c.closed = True; c.chans.clear()
                self.closes += 1
                kind = "conn"
            if "[error" in low or "[except" in low or "error " in low[:60]:
                kind = "err"; self.errors += 1
            if "dcc" in low or "transfer" in low or "!send" in low or "!accept" in low or "!reject" in low:
                if kind in ("in", "out"):
                    kind = kind + "bot"
            self.log.append((t, kind, line))

    def _on_in(self, c, payload):
        parts = payload.split(" ", 2)
        cmd = parts[0].upper() if parts else ""
        if cmd == "NICK" and len(parts) > 1:
            c.nick = parts[1].lstrip(":")
        elif cmd == "USER" and len(parts) > 1:
            c.user = parts[1]
        elif cmd == "PRIVMSG" and len(parts) > 2:
            target, text = parts[1], parts[2].lstrip(":")
            if self.bot_nick and target.lower() == self.bot_nick.lower():
                self.bot_cmds += 1
                self.bot_last = "%s: %s" % (c.nick or "fd%d" % c.fd, text[:40])
                if text.startswith("\x01"):
                    self.ctcp += 1
            elif text.startswith("!") and not self.bot_nick:
                # first bot command before we know the bot nick: remember target
                self.bot_last = "%s: %s" % (c.nick or "fd%d" % c.fd, text[:40])

    def _on_out(self, c, payload):
        # numerics: ":irc.local 001 nick ..." => registered
        m = re.match(r":\S+ (\d{3}) (\S+)", payload)
        if m:
            if m.group(1) == "001":
                c.registered = True; c.nick = m.group(2)
            return
        # ":nick!user@host CMD args"
        m = re.match(r":([^!\s]+)!(\S+) (\S+)(?: (.*))?", payload)
        if not m:
            return
        who, host, cmd, rest = m.group(1), m.group(2), m.group(3).upper(), (m.group(4) or "")
        args = rest.split(" ")
        if host.startswith("bot@") or who.lower().endswith("bot"):
            if not self.bot_nick:
                self.bot_nick = who
            if who == self.bot_nick and cmd in ("PRIVMSG", "NOTICE"):
                self.bot_replies += 1
                text = rest.split(" :", 1)[1] if " :" in rest else rest
                d = self._re_dcc.search(text)
                if d:
                    self.offer = Offer(d.group(1), d.group(2), int(d.group(3)), int(d.group(4)),
                                       "?", c.nick or "fd%d" % c.fd)
                    self.dcc_fd = None
                    self.strace_bytes = {}
                    # the sender is the last client that talked to the bot
                    if ":" in self.bot_last:
                        self.offer.frm = self.bot_last.split(":", 1)[0]
                elif self.offer:
                    low = text.lower()
                    if "rejected" in low:
                        self.offer.status = "rejected"; self.offer.t1 = time.time(); self.offers_done += 1
                    elif "accepted" in low:
                        self.offer.status = "accepted, waiting for the receiver to connect"
                    elif "complete" in low:
                        self.offer.status = "complete"; self.offer.t1 = time.time(); self.offers_done += 1
                        self.offer.sent = self.offer.size
                    elif "failed" in low or "expired" in low or "timed out" in low:
                        self.offer.status = "failed"; self.offer.t1 = time.time(); self.offers_done += 1
        if who == c.nick:
            if cmd == "JOIN" and args and args[0]:
                c.chans.add(args[0].lstrip(":"))
            elif cmd == "PART" and args and args[0]:
                c.chans.discard(args[0])
            elif cmd == "NICK" and args and args[0]:
                c.nick = args[0].lstrip(":")
        if cmd == "KICK" and len(args) >= 2 and args[1] == c.nick:
            c.chans.discard(args[0])

    # ---- per-second sampling --------------------------------------------------

    def tick(self):
        with self.lock:
            self.in_hist.append(self.cur_in); self.out_hist.append(self.cur_out)
            self.bin_hist.append(self.cur_bin); self.bout_hist.append(self.cur_bout)
            self.cur_in = self.cur_out = self.cur_bin = self.cur_bout = 0
            self.rss_hist.append(self.rss_mb)
            self.cpu_hist.append(self.cpu)
            self.conn_hist.append(len([c for c in self.clients.values() if not c.closed]))
            self.probe_hist.append(self.probe_ms if self.probe_ms is not None else 0)
            for k in self.sys_hist:
                self.sys_hist[k].append(self.sys_cur[k]); self.sys_cur[k] = 0
            for c in self.clients.values():
                if c.closed:
                    continue
                if c.sendq > 0 and c.sendq >= c.sendq_prev:
                    c.stall_ticks += 1
                else:
                    c.stall_ticks = 0
                c.sendq_prev = c.sendq

    def channels(self):
        chans = collections.defaultdict(list)
        for c in self.clients.values():
            if c.closed:
                continue
            for ch in c.chans:
                chans[ch].append(c.nick or ("fd%d" % c.fd))
        return chans

    def live_clients(self):
        cl = [c for c in self.clients.values() if not c.closed]
        if self.sort == 1:
            cl.sort(key=lambda c: -(c.msgs_in + c.msgs_out))
        elif self.sort == 2:
            cl.sort(key=lambda c: -c.sendq)
        else:
            cl.sort(key=lambda c: c.fd)
        return cl

    def stalled(self):
        return [c for c in self.live_clients() if c.sendq >= 32768 or c.stall_ticks >= 3 and c.sendq > 0]


# ===========================================================================
# Samplers: process stats + kernel send queues + probe latency + strace
# ===========================================================================

_CLK = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100
_cpu_prev = {}


def ps_stats(pid):
    """(rss_mb, cpu_percent). Linux: /proc (CPU = delta since last call);
    macOS: ps."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            fields = f.read().rsplit(")", 1)[1].split()
        utime, stime = int(fields[11]), int(fields[12])
        with open("/proc/%d/statm" % pid) as f:
            rss = int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") // (1024 * 1024)
        now = time.time(); tot = (utime + stime) / float(_CLK)
        prev = _cpu_prev.get(pid)
        _cpu_prev[pid] = (now, tot)
        cpu = 0.0
        if prev and now > prev[0]:
            cpu = (tot - prev[1]) / (now - prev[0]) * 100.0
        return rss, max(0.0, cpu)
    except (OSError, IndexError, ValueError):
        pass
    try:
        out = subprocess.check_output(["ps", "-o", "rss=,%cpu=", "-p", str(pid)],
                                      stderr=subprocess.DEVNULL).decode().split()
        return int(out[0]) // 1024, float(out[1])
    except Exception:
        return 0, 0.0


def fd_counts(pid):
    """(open fds, of which sockets) for the process. Linux via /proc, macOS via lsof."""
    try:
        d = "/proc/%d/fd" % pid
        names = os.listdir(d)
        socks = 0
        for n in names:
            try:
                if os.readlink(os.path.join(d, n)).startswith("socket:"):
                    socks += 1
            except OSError:
                pass
        return len(names), socks
    except OSError:
        pass
    try:
        out = subprocess.check_output(["lsof", "-nP", "-p", str(pid)], stderr=subprocess.DEVNULL).decode().splitlines()[1:]
        socks = sum(1 for l in out if " TCP " in l or " IPv4 " in l or " IPv6 " in l)
        return len(out), socks
    except Exception:
        return -1, -1


def thread_count(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("Threads:"):
                    return int(line.split()[1])
    except OSError:
        pass
    try:
        out = subprocess.check_output(["ps", "-M", "-p", str(pid)], stderr=subprocess.DEVNULL).decode().splitlines()
        return max(0, len(out) - 1)
    except Exception:
        return -1


def find_pid_on_port(port):
    """Best effort: lsof (mac/linux) then ss (linux)."""
    try:
        out = subprocess.check_output(["lsof", "-nP", "-iTCP:%d" % port, "-sTCP:LISTEN", "-t"],
                                      stderr=subprocess.DEVNULL).decode().split()
        if out:
            return int(out[0])
    except Exception:
        pass
    try:
        out = subprocess.check_output(["ss", "-ltnp"], stderr=subprocess.DEVNULL).decode()
        for line in out.splitlines():
            if ":%d " % port in line:
                m = re.search(r"pid=(\d+)", line)
                if m:
                    return int(m.group(1))
    except Exception:
        pass
    return None


def kernel_sendq(port):
    """{peer 'ip:port': sendq_bytes} for established connections whose LOCAL end
    is the server port. macOS: netstat -an; Linux: /proc/net/tcp, then ss."""
    res = {}
    if sys.platform == "darwin":
        try:
            out = subprocess.check_output(["netstat", "-an", "-p", "tcp"],
                                          stderr=subprocess.DEVNULL).decode()
        except Exception:
            return res
        for line in out.splitlines():
            f = line.split()
            if len(f) < 6 or not f[0].startswith("tcp"):
                continue
            local, foreign, state = f[3], f[4], f[5]
            if state != "ESTABLISHED":
                continue
            lp = local.rsplit(".", 1)
            if len(lp) != 2 or lp[1] != str(port):
                continue
            fp = foreign.rsplit(".", 1)
            if len(fp) != 2:
                continue
            peer = "%s:%s" % (fp[0].replace("::ffff:", ""), fp[1])
            try:
                res[peer] = int(f[2])
            except ValueError:
                pass
        return res

    def _ip4(hexaddr):
        b = bytes.fromhex(hexaddr)
        return "%d.%d.%d.%d" % (b[3], b[2], b[1], b[0])

    def _ip6(hexaddr):
        if hexaddr[:24].upper() == "0000000000000000FFFF0000":
            return _ip4(hexaddr[24:32])
        return hexaddr.lower()

    for path, conv in (("/proc/net/tcp", _ip4), ("/proc/net/tcp6", _ip6)):
        try:
            with open(path) as f:
                next(f)
                for line in f:
                    fl = line.split()
                    if len(fl) < 5 or fl[3] != "01":
                        continue
                    laddr, lport = fl[1].rsplit(":", 1)
                    if int(lport, 16) != port:
                        continue
                    raddr, rport = fl[2].rsplit(":", 1)
                    txq = int(fl[4].split(":")[0], 16)
                    res["%s:%d" % (conv(raddr), int(rport, 16))] = txq
        except (OSError, StopIteration, ValueError):
            continue
    if res:
        return res
    try:
        out = subprocess.check_output(["ss", "-tn", "state", "established"],
                                      stderr=subprocess.DEVNULL).decode()
    except Exception:
        return res
    for line in out.splitlines():
        f = line.split()
        if len(f) < 4 or not f[0].isdigit():
            continue
        local, peer = f[2], f[3]
        if not local.endswith(":%d" % port):
            continue
        peer = peer.replace("[::ffff:", "").replace("]", "")
        try:
            res[peer] = int(f[1])
        except ValueError:
            pass
    return res


def scan_poll_sites(binary):
    """Count `poll(` call sites in the sources next to the binary (repo root).
    Comments are stripped; #ifdef BONUS code is included. Returns (n, sites)."""
    root = os.path.dirname(os.path.abspath(binary))
    files = []
    for sub in ("srcs", "src", "sources", "includes", "include", "inc"):
        d = os.path.join(root, sub)
        if os.path.isdir(d):
            for dp, _, fns in os.walk(d):
                files += [os.path.join(dp, fn) for fn in fns if fn.endswith((".cpp", ".hpp", ".h", ".cc", ".tpp"))]
    if not files:
        files = [os.path.join(root, fn) for fn in os.listdir(root) if fn.endswith((".cpp", ".hpp"))]
    sites = []
    rx = re.compile(r"(?<![\w:.>])poll\s*\(")
    for fn in files:
        try:
            src = open(fn, errors="ignore").read()
        except OSError:
            continue
        src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
        for i, line in enumerate(src.split("\n"), 1):
            code = line.split("//", 1)[0]
            code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
            if rx.search(code):
                sites.append((os.path.relpath(fn, root), i))
    return len(sites), sites


class Probe(threading.Thread):
    """A tiny registered client that PINGs every second and records the round trip."""

    def __init__(self, state, host, port, password):
        threading.Thread.__init__(self)
        self.daemon = True
        self.state = state; self.host = host; self.port = port; self.password = password
        self.stop = False

    def run(self):
        while not self.stop:
            try:
                s = socket.create_connection((self.host, self.port), timeout=3)
                s.sendall(("PASS %s\r\nNICK irctop\r\nUSER irctop 0 * :irctop\r\n"
                           % self.password).encode())
                s.settimeout(3)
                buf = b""
                t0 = time.time()
                while b"001" not in buf and time.time() - t0 < 3:
                    try:
                        buf += s.recv(4096)
                    except socket.timeout:
                        break
                while not self.stop:
                    t1 = time.time()
                    s.sendall(b"PING :irctop\r\n")
                    got = b""
                    while b"PONG" not in got and time.time() - t1 < 3:
                        try:
                            chunk = s.recv(4096)
                        except socket.timeout:
                            break
                        if not chunk:
                            raise OSError("closed")
                        got += chunk
                    self.state.probe_ms = (time.time() - t1) * 1000 if b"PONG" in got else None
                    time.sleep(1)
            except OSError:
                self.state.probe_ms = None
                time.sleep(2)


class Strace(threading.Thread):
    """Linux only: strace -p PID and count poll/accept/recv/send/close per second.
    Also sums sendto() results per fd (used for DCC transfer progress)."""
    SYSCALLS = "poll,ppoll,accept,accept4,recvfrom,recv,read,sendto,send,write,close"
    _rx = re.compile(r"^(?:\[pid\s+\d+\]\s+)?(\w+)\((\d*)")
    _rx_ret = re.compile(r"=\s*(-?\d+)\s*$")

    def __init__(self, state, pid):
        threading.Thread.__init__(self); self.daemon = True
        self.state = state; self.pid = pid; self.proc = None; self.stop = False

    def run(self):
        st = self.state
        try:
            self.proc = subprocess.Popen(["strace", "-p", str(self.pid), "-qq", "-e", "trace=" + self.SYSCALLS,
                                          "-s", "0"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        except OSError:
            st.sys_note = "strace not found"; return
        first = self.proc.stderr.readline().decode(errors="ignore")
        if "Operation not permitted" in first or "attach" in first.lower() and "denied" in first.lower():
            st.sys_note = "strace could not attach (ptrace not permitted)"; self.proc.kill(); return
        st.sys_source = "strace"; st.sys_note = "live via strace"
        for k in st.sys_total:
            st.sys_total[k] = 0
        lines = [first] if first else []
        while not self.stop:
            line = self.proc.stderr.readline()
            if not line:
                break
            lines = [line.decode(errors="ignore")]
            for l in lines:
                if "resumed>" in l:
                    continue
                m = self._rx.match(l)
                if not m:
                    continue
                name, fd = m.group(1), m.group(2)
                key = {"poll": "poll", "ppoll": "poll", "accept": "accept", "accept4": "accept",
                       "recvfrom": "recv", "recv": "recv", "read": "recv",
                       "sendto": "send", "send": "send", "write": "send"}.get(name)
                if key is None:
                    continue
                if key in ("recv", "send") and fd in ("0", "1", "2"):
                    continue                     # stdout logging, not network
                with st.lock:
                    st.sys_cur[key] += 1; st.sys_total[key] += 1
                    if key == "send" and fd:
                        r = self._rx_ret.search(l)
                        if r and int(r.group(1)) > 0:
                            st.strace_bytes[int(fd)] = st.strace_bytes.get(int(fd), 0) + int(r.group(1))
        if st.sys_source == "strace":
            st.sys_note = "strace ended"

    def end(self):
        self.stop = True
        if self.proc:
            try:
                self.proc.terminate()
            except Exception:
                pass


# ===========================================================================
# Server launcher (pty) -- keeps std::cout line-buffered
# ===========================================================================

class Launched(object):
    def __init__(self, state, binary, port, password):
        self.state = state
        self.proc = None
        argv = [os.path.abspath(binary), str(port), password]
        cwd = os.path.dirname(os.path.abspath(binary)) or "."
        if HAVE_PTY:
            self.master, slave = pty.openpty()
            self.proc = subprocess.Popen(argv, stdout=slave, stderr=slave, cwd=cwd, close_fds=True)
            os.close(slave)
            threading.Thread(target=self._pump_pty, daemon=True).start()
        else:
            self.master = None
            self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=cwd)
            threading.Thread(target=self._pump_pipe, daemon=True).start()
        state.pid = self.proc.pid

    def _pump_pty(self):
        buf = b""
        while True:
            try:
                data = os.read(self.master, 4096)
            except OSError:
                break
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.state.feed(strip_ansi(line.decode(errors="ignore")))
        self.state.feed("[irctop] server output ended")

    def _pump_pipe(self):
        for line in iter(self.proc.stdout.readline, b""):
            self.state.feed(strip_ansi(line.decode(errors="ignore")))

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def stop(self):
        if self.proc is None:
            return
        try:
            self.proc.terminate(); self.proc.wait(timeout=5)
        except Exception:
            try:
                self.proc.kill(); self.proc.wait(timeout=3)
            except Exception:
                pass


# ===========================================================================
# Sampler thread: every second, ps + kernel sendq + tick
# ===========================================================================

class Sampler(threading.Thread):
    def __init__(self, state):
        threading.Thread.__init__(self); self.daemon = True
        self.state = state; self.stop = False
        self.threads = -1

    def run(self):
        st = self.state
        n = 0
        while not self.stop:
            if st.mode == "attach":
                alive = False
                try:
                    os.kill(st.pid, 0); alive = True
                except (OSError, TypeError):
                    pass
                if not alive:
                    new = find_pid_on_port(st.port)
                    if new and new != st.pid:
                        st.pid = new
                        st.feed("[irctop] server on port %d restarted: now pid %d" % (st.port, new))
                    elif new is None and st.pid:
                        st.feed("[irctop] server on port %d is gone, waiting for the next one" % st.port)
                        st.pid = None
            if st.pid:
                rss, cpu = ps_stats(st.pid)
                st.rss_mb, st.cpu = rss, cpu
                st.fds, st.socks = fd_counts(st.pid)
                st.fd_hist.append(max(0, st.socks))
                if n % 5 == 0:
                    self.threads = thread_count(st.pid)
            q = kernel_sendq(st.port) if st.port else {}
            with st.lock:
                for c in st.clients.values():
                    if not c.closed:
                        c.sendq = q.get(c.peer, 0)
                if st.mode == "attach":
                    seen = set()
                    for peer, sq in q.items():
                        seen.add(peer)
                        key = hash(peer) & 0xffff
                        c = st.clients.get(key)
                        if c is None:
                            c = st.clients[key] = Client(key, peer)
                        c.sendq = sq; c.closed = False
                    for c in st.clients.values():
                        if c.peer not in seen:
                            c.closed = True
                off = st.offer
            if off and off.status in ("accepted, waiting for the receiver to connect", "transferring"):
                dq = kernel_sendq(off.port)
                st.dcc_sendq = sum(dq.values()) if dq else 0
                if dq:
                    off.status = "transferring"
                # strace: bytes sent on the one fd that is not a client fd
                if st.strace_bytes:
                    known = set(st.clients.keys())
                    cand = [b for fd, b in st.strace_bytes.items() if fd not in known]
                    if cand:
                        off.sent = max(cand)
            st.tick()
            n += 1
            time.sleep(1)


# ===========================================================================
# Text snapshot (no curses)
# ===========================================================================

def snapshot_text(st, width=100):
    up = int(time.time() - st.started)
    lines = []
    ps = st.poll_sites
    lines.append("ircserv  pid %s  port %s  up %02d:%02d:%02d  poll() sites %s  probe %s  RSS %d MB  CPU %.1f%%"
                 % (st.pid, st.port, up // 3600, (up // 60) % 60, up % 60,
                    ps[0] if ps else "?", ("%.1f ms" % st.probe_ms) if st.probe_ms is not None else "-",
                    st.rss_mb, st.cpu))
    lines.append("net  in %-12s out %-12s   msgs in %3d/s out %3d/s   conns %d (accepted %d, closed %d, stalled %d)"
                 % (human_rate((list(st.bin_hist) or [0])[-1]), human_rate((list(st.bout_hist) or [0])[-1]),
                    (list(st.in_hist) or [0])[-1], (list(st.out_hist) or [0])[-1],
                    len(st.live_clients()), st.accepts, st.closes, len(st.stalled())))
    lines.append("syscalls/s (%s): %s   totals: %s" % (
        st.sys_note or st.sys_source,
        "  ".join("%s %d" % (k, (list(st.sys_hist[k]) or [0])[-1]) for k in ("poll", "accept", "recv", "send")),
        "  ".join("%s %d" % (k, st.sys_total[k]) for k in ("poll", "accept", "recv", "send"))))
    listeners = max(0, st.socks - len(st.live_clients())) if st.socks >= 0 else -1
    lines.append("poll set: sockets %s = listeners %s + clients %d   fds open %s   msgs in %s out %s"
                 % (st.socks if st.socks >= 0 else "?", listeners if listeners >= 0 else "?",
                    len(st.live_clients()), st.fds if st.fds >= 0 else "?",
                    spark(st.in_hist, 20), spark(st.out_hist, 20)))
    if st.bot_nick or st.offer:
        o = st.offer
        lines.append("bot %s  commands %d  replies %d  ctcp %d  last: %s" % (st.bot_nick or "?", st.bot_cmds, st.bot_replies, st.ctcp, st.bot_last))
        if o:
            lines.append("dcc  %s -> %s  %s (%s)  port %d  %s%s" % (o.frm, o.to, o.file, human(o.size), o.port, o.status,
                         ("  sent %s" % human(o.sent)) if o.sent else ""))
    lines.append("-" * width)
    lines.append("%-4s %-12s %-3s %-16s %6s %6s %8s %8s %6s  %s"
                 % ("fd", "nick", "reg", "peer", "in", "out", "bytesOut", "SendQ", "idle", "channels"))
    now = time.time()
    for c in st.live_clients():
        lines.append("%-4s %-12s %-3s %-16s %6d %6d %8s %8s %5ds  %s"
                     % (c.fd, (c.nick or "-")[:12], "Y" if c.registered else "-", c.peer[:16],
                        c.msgs_in, c.msgs_out, human(c.bytes_out), human(c.sendq),
                        int(now - c.last), ",".join(sorted(c.chans))))
    lines.append("-" * width)
    chans = st.channels()
    if chans:
        for ch in sorted(chans):
            lines.append("%-14s (%d): %s" % (ch, len(chans[ch]), " ".join(sorted(chans[ch]))))
        lines.append("-" * width)
    with st.lock:
        tail = list(st.log)[-12:]
    for (t, kind, text) in tail:
        lines.append("%s %s" % (time.strftime("%H:%M:%S", time.localtime(t)), text[: width - 9]))
    return "\n".join(lines)


# ===========================================================================
# Curses UI -- boxed panels, btop style
# ===========================================================================

# Two glyph sets. Python's curses on macOS is often linked against Apple's old
# non-wide ncurses, which counts a multi-byte character as several cells and
# corrupts them on redraw; there we fall back to plain ASCII automatically.
GLYPHS = {
    "unicode": dict(blocks=" ▁▂▃▄▅▆▇█", fill="█", empty="░", tl="╭", tr="╮", bl="╰", br="╯", h="─", v="│",
                    sep="─", ok="✓", bad="✗", arrow="→", more="…", updown="↑/↓", lr="←/→"),
    "ascii":   dict(blocks=" .:-=+*#@", fill="#", empty="-", tl="+", tr="+", bl="+", br="+", h="-", v="|",
                    sep="-", ok="OK", bad="X", arrow="->", more="...", updown="up/down", lr="left/right"),
}


def curses_is_wide():
    """True when this Python's curses was built against a wide-char ncurses."""
    return hasattr(curses, "unget_wch")


def ui(stdscr, st, launched, sampler, probe, glyphs="auto"):
    curses.curs_set(0); stdscr.nodelay(True); stdscr.timeout(250); stdscr.keypad(True)
    curses.start_color(); curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_GREEN, -1); curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_RED, -1); curses.init_pair(4, curses.COLOR_CYAN, -1)
    curses.init_pair(5, curses.COLOR_MAGENTA, -1); curses.init_pair(6, curses.COLOR_BLUE, -1)
    curses.init_pair(7, curses.COLOR_WHITE, -1)
    C_GRN, C_YEL, C_RED, C_CYN, C_MAG, C_BLU, C_WHT = [curses.color_pair(i) for i in range(1, 8)]
    DIM = curses.A_DIM
    if glyphs == "auto":
        glyphs = "unicode" if curses_is_wide() else "ascii"
    G = GLYPHS[glyphs]

    # ---- view options (the menu bar edits these) --------------------------------
    opts = {"sort": 0, "graphs": True, "graph_rows": 0, "client_rows": 0, "follow": True,
            "glyphs": glyphs, "bytes": "human"}
    MENU = [  # (key, label, kind)  kind: cycle / toggle / int / action
        ("sort", "sort", "cycle"), ("graphs", "graphs", "toggle"), ("graph_rows", "graph height", "int"),
        ("client_rows", "clients height", "int"), ("follow", "log", "toggle"), ("glyphs", "glyphs", "cycle"),
        ("snapshot", "snapshot", "action"), ("quit", "quit", "action"),
    ]
    SORTS = ("fd", "most traffic", "biggest queue")
    focus = 1                     # 0 = clients table, 1 = log, 2 = menu bar
    menu_i = 0
    client_scroll = 0
    hscroll = {0: 0, 1: 0}        # horizontal scroll of the clients table / log
    KEYS = ("poll", "accept", "recv", "send")
    hint = ""

    def put(y, x, s, attr=0):
        h, w = stdscr.getmaxyx()
        if 0 <= y < h and 0 <= x < w:
            lim = w - x - (1 if y == h - 1 else 0)
            try:
                stdscr.addstr(y, x, s[: max(0, lim)], attr)
            except curses.error:
                pass

    def box(y, x, hh, ww, title, attr=C_CYN, focused=False):
        """Box; returns (iy, ix, ih, iw) of the inside."""
        if hh < 2 or ww < 4:
            return (y + 1, x + 1, max(0, hh - 2), max(0, ww - 2))
        put(y, x, G["tl"] + G["h"] * (ww - 2) + G["tr"], attr)
        for i in range(1, hh - 1):
            put(y + i, x, G["v"], attr); put(y + i, x + ww - 1, G["v"], attr)
        put(y + hh - 1, x, G["bl"] + G["h"] * (ww - 2) + G["br"], attr)
        if title:
            put(y, x + 2, " %s " % title[: ww - 6], attr | curses.A_BOLD | (curses.A_REVERSE if focused else 0))
        return (y + 1, x + 1, hh - 2, ww - 2)

    def sep(y, x, w_, attr=DIM):
        """Thin separator inside a panel (btop style)."""
        put(y, x, G["sep"] * max(0, w_), attr)

    def grad(frac):
        return C_GRN if frac < 0.5 else (C_YEL if frac < 0.8 else C_RED)

    def graph(y, x, rows, width, hist, hi=None, color=None):
        """Block graph coloured per column by its height (green→yellow→red)."""
        if rows <= 0 or width <= 0:
            return 0
        vals = list(hist)[-width:]
        vals = [0] * (width - len(vals)) + vals
        top = hi if hi else (max(vals) if vals else 0)
        top = top or 1
        blocks = G["blocks"]
        levels = [min(rows * 8, int(round(v * rows * 8 / float(top)))) for v in vals]
        for r in range(rows):                       # r = 0 is the top row
            rb = rows - 1 - r                       # row index from the bottom
            for i, lv in enumerate(levels):
                k = lv - rb * 8
                if k <= 0:
                    continue
                ch = blocks[min(8, k)]
                frac = vals[i] / float(top)
                put(y + r, x + i, ch, color if color is not None else grad(frac))
        return top

    def meter(width, frac):
        frac = max(0.0, min(1.0, frac)); n = int(round(frac * width))
        return G["fill"] * n + G["empty"] * (width - n)

    def clamp_rows(val, lo, hi_):
        return max(lo, min(hi_, val))

    def scale(hist, floor):
        """Graph ceiling: the recent peak, never below `floor` (so a quiet
        server shows small bars instead of a full-height wall)."""
        return max(float(floor), float(max(list(hist) or [0])))

    while True:
        h, w = stdscr.getmaxyx(); stdscr.erase()
        up = int(time.time() - st.started)
        alive = (launched.alive() if launched else True)
        st.sort = opts["sort"]; st.paused = not opts["follow"]
        clients = st.live_clients(); chans = st.channels(); stalled = st.stalled()
        ps = st.poll_sites
        G = GLYPHS[opts["glyphs"]]

        # ---- row 0: header ----------------------------------------------------------
        pollstr = ("poll() sites %d %s" % (ps[0], G["ok"] if ps[0] == 1 else G["bad"])) if ps else "poll() sites ?"
        hdr = " irctop   pid %s   port %s   up %02d:%02d:%02d   %s   threads %s   probe %s   %s" % (
            st.pid, st.port, up // 3600, (up // 60) % 60, up % 60, pollstr,
            sampler.threads if sampler.threads >= 0 else "?",
            ("%.1f ms" % st.probe_ms) if st.probe_ms is not None else "no PONG",
            "RUNNING" if alive else "SERVER EXITED")
        if w < 110 or h < 36:
            hdr += "   (terminal %dx%d; 110x36+ shows everything)" % (w, h)
        put(0, 0, hdr.ljust(w - 1), curses.A_REVERSE | (0 if alive else C_RED))
        if ps and ps[0] != 1:
            put(0, hdr.find("poll()"), "poll() sites %d %s" % (ps[0], G["bad"]), curses.A_REVERSE | C_RED | curses.A_BOLD)

        # ---- row 1: menu bar ----------------------------------------------------------
        def menu_value(key):
            if key == "sort":
                return SORTS[opts["sort"]]
            if key == "graphs":
                return "on" if opts["graphs"] else "off"
            if key == "graph_rows":
                return "%+d" % opts["graph_rows"] if opts["graph_rows"] else "auto"
            if key == "client_rows":
                return "%+d" % opts["client_rows"] if opts["client_rows"] else "auto"
            if key == "follow":
                return "follow" if opts["follow"] else "paused"
            if key == "glyphs":
                return opts["glyphs"]
            return ""
        x = 0
        put(1, 0, " " * (w - 1), DIM)
        for i, (key, label, kind) in enumerate(MENU):
            val = menu_value(key)
            txt = " %s%s " % (label, (": " + val) if val else "")
            sel = (focus == 2 and i == menu_i)
            put(1, x, txt, (curses.A_REVERSE | curses.A_BOLD) if sel else (C_CYN if focus == 2 else DIM))
            x += len(txt) + 1
        tail = ("  %s pick  Enter/%s change  b back" % (G["lr"], G["updown"])) if focus == 2 else "  Tab: menu"
        put(1, min(x, max(0, w - len(tail) - 1)), tail, C_YEL if focus == 2 else DIM)

        # ---- panels ---------------------------------------------------------------------
        base = 9 if h >= 44 else (8 if h >= 40 else 7)
        ah = clamp_rows(base + opts["graph_rows"], 5, 16)
        y = 2

        def draw_panels():
            nonlocal y
            cw = w // 4
            widths = [cw, cw, cw, w - 3 * cw]
            xs = [0, cw, 2 * cw, 3 * cw]

            # CPU
            iy, ix, ih, iw = box(y, xs[0], ah, widths[0], "CPU")
            cpu = st.cpu
            put(iy, ix, "%5.1f%% " % cpu, grad(min(1.0, cpu / 100.0)) | curses.A_BOLD)
            put(iy, ix + 7, meter(max(4, iw - 8), min(1.0, cpu / 100.0)), grad(min(1.0, cpu / 100.0)))
            sep(iy + 1, ix, iw)
            cpu_hi = max(10.0, max(list(st.cpu_hist) or [0.0]))
            graph(iy + 2, ix, ih - 3, iw, st.cpu_hist, hi=cpu_hi)
            put(iy + ih - 1, ix, ("scale %d%%  2 min" % cpu_hi)[:iw], DIM)

            # MEM
            iy, ix, ih, iw = box(y, xs[1], ah, widths[1], "MEMORY")
            rss_hi = max(list(st.rss_hist) or [1]) or 1
            put(iy, ix, "RSS %d MB" % st.rss_mb, C_CYN | curses.A_BOLD)
            put(iy, ix + 11, ("peak %d MB   open fds %s" % (rss_hi, st.fds if st.fds >= 0 else "?"))[: iw - 11], DIM)
            sep(iy + 1, ix, iw)
            graph(iy + 2, ix, ih - 3, iw, st.rss_hist, hi=max(rss_hi, 1) * 1.25, color=C_CYN)
            trend = ""
            if len(st.rss_hist) >= 30:
                d = list(st.rss_hist)[-1] - list(st.rss_hist)[-30]
                trend = ("growing +%d MB in 30 s" % d) if d > 0 else "flat"
            put(iy + ih - 1, ix, ("resident memory  %s" % trend)[:iw], C_RED if trend.startswith("growing") else DIM)

            # NET
            iy, ix, ih, iw = box(y, xs[2], ah, widths[2], "NETWORK  bytes/s")
            bi = (list(st.bin_hist) or [0])[-1]; bo = (list(st.bout_hist) or [0])[-1]
            gr = max(1, (ih - 3) // 2)
            put(iy, ix, ("in  %-11s total %s" % (human_rate(bi), human(st.total_bin)))[:iw], C_GRN | curses.A_BOLD)
            graph(iy + 1, ix, gr, iw, st.bin_hist, hi=scale(st.bin_hist, 2048), color=C_GRN)
            sep(iy + 1 + gr, ix, iw)
            put(iy + 2 + gr, ix, ("out %-11s total %s" % (human_rate(bo), human(st.total_bout)))[:iw], C_YEL | curses.A_BOLD)
            graph(iy + 3 + gr, ix, ih - 3 - gr, iw, st.bout_hist, hi=scale(st.bout_hist, 2048), color=C_YEL)

            # MESSAGES
            iy, ix, ih, iw = box(y, xs[3], ah, widths[3], "MESSAGES  IRC lines/s")
            mi = (list(st.in_hist) or [0])[-1]; mo = (list(st.out_hist) or [0])[-1]
            put(iy, ix, ("received %4d/s  total %d" % (mi, st.total_in))[:iw], C_GRN | curses.A_BOLD)
            graph(iy + 1, ix, gr, iw, st.in_hist, hi=scale(st.in_hist, 20), color=C_GRN)
            sep(iy + 1 + gr, ix, iw)
            put(iy + 2 + gr, ix, ("sent     %4d/s  total %d" % (mo, st.total_out))[:iw], C_YEL | curses.A_BOLD)
            graph(iy + 3 + gr, ix, ih - 3 - gr, iw, st.out_hist, hi=scale(st.out_hist, 20), color=C_YEL)

            # ---- panel row B: SYSCALLS | CONNECTIONS | BOT / DCC ------------------------
            y += ah
            bh = ah
            sw = w // 3
            bws = [sw, sw, w - 2 * sw]; bxs = [0, sw, 2 * sw]

            src = st.sys_source
            title = "SYSCALLS per second  (%s)" % ("live, strace" if src == "strace" else "counted from the server log")
            iy, ix, ih, iw = box(y, bxs[0], bh, bws[0], title, C_MAG)
            col = max(10, iw // 4)
            for i, k in enumerate(KEYS):
                cur = (list(st.sys_hist[k]) or [0])[-1]
                xx = ix + i * col
                if xx + 6 > ix + iw:
                    break
                cw_ = min(col - 1, ix + iw - xx)
                put(iy, xx, ("%-6s" % k), C_MAG | curses.A_BOLD)
                put(iy + 1, xx, ("%5d/s" % cur)[:cw_], curses.A_BOLD)
                if ih >= 7:
                    put(iy + 2, xx, ("total %d" % st.sys_total[k])[:cw_], DIM)
                    graph(iy + 4, xx, ih - 5, cw_, st.sys_hist[k], hi=scale(st.sys_hist[k], 20), color=C_MAG)
                else:
                    graph(iy + 3, xx, ih - 4, cw_, st.sys_hist[k], hi=scale(st.sys_hist[k], 20), color=C_MAG)
            sep(iy + (3 if ih >= 7 else 2), ix, iw)
            note = st.sys_note
            if src != "strace":
                note = "recv/send = logged lines, accept = logged connections; poll() itself is not visible without strace (Linux)"
            put(iy + ih - 1, ix, note[:iw], DIM)

            iy, ix, ih, iw = box(y, bxs[1], bh, bws[1], "CONNECTIONS", C_BLU)
            listeners = max(0, st.socks - len(clients)) if st.socks >= 0 else -1
            dcc = 0
            if st.offer and st.offer.status in ("offered", "accepted, waiting for the receiver to connect", "transferring"):
                dcc = 1 if st.offer.status == "offered" else 2
            txt = "open %d   accepted %d   closed %d   " % (len(clients), st.accepts, st.closes)
            put(iy, ix, txt[:iw], curses.A_BOLD)
            if stalled:
                put(iy, ix + len(txt), (" %d STALLED " % len(stalled))[: max(0, iw - len(txt))], C_RED | curses.A_BOLD | curses.A_REVERSE)
            else:
                put(iy, ix + len(txt), "stalled 0"[: max(0, iw - len(txt))], C_GRN)
            if listeners >= 0:
                extra = listeners - dcc
                put(iy + 1, ix, ("poll set: %d sockets = %d listener%s + %d clients%s" % (
                    st.socks, max(0, extra), "" if extra == 1 else "s", len(clients),
                    (" + %d transfer" % dcc) if dcc else ""))[:iw], C_BLU)
            else:
                put(iy + 1, ix, "poll set: ? (no /proc or lsof)", DIM)
            sep(iy + 2, ix, iw)
            pm = st.probe_ms
            lat = ("latency %s   (a registered client PINGs every second)" % (
                ("%.1f ms" % pm) if pm is not None else "no PONG within 3 s"))
            lat_attr = (C_RED | curses.A_BOLD) if pm is None else (grad(min(1.0, pm / 200.0)) | curses.A_BOLD)
            if ih >= 8:
                cg = max(1, (ih - 6) // 2)
                put(iy + 3, ix, ("open connections over time  (scale %d)" % scale(st.conn_hist, 4))[:iw], DIM)
                graph(iy + 4, ix, cg, iw, st.conn_hist, hi=scale(st.conn_hist, 4), color=C_BLU)
                sep(iy + 4 + cg, ix, iw)
                put(iy + 5 + cg, ix, lat[:iw], lat_attr)
                graph(iy + 6 + cg, ix, ih - 6 - cg, iw, st.probe_hist, hi=scale(st.probe_hist, 20))
            else:                                   # compact: latency line + one graph
                put(iy + 3, ix, lat[:iw], lat_attr)
                graph(iy + 4, ix, ih - 4, iw, st.probe_hist, hi=scale(st.probe_hist, 20))

            iy, ix, ih, iw = box(y, bxs[2], bh, bws[2], "BOT / FILE TRANSFER", C_GRN)
            if not st.bot_nick and not st.offer:
                put(iy, ix, "no bot seen yet", DIM)
                put(iy + 1, ix, "launch ircserv_bonus and send, from a client:", DIM)
                put(iy + 2, ix, "  PRIVMSG <botnick> :!help", DIM)
            else:
                put(iy, ix, ("bot %s   commands %d   replies %d   ctcp %d" % (st.bot_nick or "?", st.bot_cmds, st.bot_replies, st.ctcp))[:iw], curses.A_BOLD)
                put(iy + 1, ix, ("last command  %s" % st.bot_last)[:iw], DIM)
                sep(iy + 2, ix, iw)
                o = st.offer
                if o:
                    put(iy + 3, ix, ("offer  %s %s %s   %s (%s)   port %d" % (o.frm, G["arrow"], o.to, o.file, human(o.size), o.port))[:iw], C_GRN)
                    stt = o.status
                    colr = C_GRN if stt == "complete" else (C_RED if stt in ("rejected", "failed") else C_YEL)
                    dur = ((o.t1 or time.time()) - o.t0)
                    stl = "status  %s   %.1f s" % (stt, dur)
                    put(iy + 4, ix, stl[:iw], colr | curses.A_BOLD)
                    ry, rx = (iy + 5, ix) if ih >= 7 else (iy + 4, ix + len(stl) + 3)   # compact: same row
                    rw = iw - (rx - ix)
                    if stt == "transferring" or o.sent:
                        frac = min(1.0, o.sent / float(o.size)) if (o.size and o.sent) else 0.0
                        if o.sent:
                            lbl = "sent %s / %s " % (human(o.sent), human(o.size))
                            put(ry, rx, lbl[:rw], C_GRN)
                            put(ry, rx + len(lbl), (meter(max(4, rw - len(lbl) - 6), frac) + " %3d%%" % int(frac * 100))[: max(0, rw - len(lbl))], C_GRN)
                        else:
                            put(ry, rx, ("kernel queue on the transfer socket %s  (byte count needs strace)" % human(st.dcc_sendq))[:rw], C_YEL)
                    elif stt == "complete":
                        put(ry, rx, ("%s delivered in %.1f s  (%s)" % (human(o.size), dur, human_rate(o.size / dur if dur else 0)))[:rw], C_GRN)
                    if ih >= 8:
                        put(iy + ih - 1, ix, ("offers finished %d" % st.offers_done)[:iw], DIM)
            y += bh

        if opts["graphs"]:
            draw_panels()
        else:
            put(2, 1, "graphs hidden (menu: graphs on)", DIM); y = 3

        # ---- clients | channels ---------------------------------------------------------
        rest = h - y - 1
        ch_rows = clamp_rows(max(5, min(16, int(rest * 0.5))) + opts["client_rows"], 4, max(4, rest - 4))
        left_w = max(60, int(w * 0.64))
        iy, ix, ih, iw = box(y, 0, ch_rows, left_w, "CLIENTS %d   sorted by %s" % (len(clients), SORTS[opts["sort"]]), C_CYN, focus == 0)
        put(iy, ix, "%-4s %-12s %-11s %8s %8s %9s %11s %5s %s" % (
            "fd", "nick", "state", "lines in", "lines out", "bytes out", "unread by it", "idle", "channels"), curses.A_BOLD)
        sep(iy + 1, ix, iw)
        body = ih - 2
        if len(clients) > body:
            body = max(1, ih - 3)
        client_scroll = max(0, min(client_scroll, max(0, len(clients) - body)))
        now = time.time()
        for i, c in enumerate(clients[client_scroll: client_scroll + body]):
            is_stalled = c in stalled
            attr = C_RED | curses.A_BOLD if is_stalled else (C_YEL if c.sendq > 0 else 0)
            state = "stalled" if is_stalled else ("registered" if c.registered else "connecting")
            row = "%-4s %-12s %-11s %8d %8d %9s %11s %4ds %s" % (
                c.fd, (c.nick or "-")[:12], state, c.msgs_in, c.msgs_out,
                human(c.bytes_out), human(c.sendq), int(now - c.last), ",".join(sorted(c.chans)))
            put(iy + 2 + i, ix, row[hscroll[0]: hscroll[0] + iw].ljust(iw), attr)
        if len(clients) > body:
            put(iy + ih - 1, ix, ("%s %d/%d shown, Tab to CLIENTS then %s" % (G["more"], min(body, len(clients) - client_scroll), len(clients), G["updown"]))[:iw], DIM)
        iy, ix, ih, iw = box(y, left_w, ch_rows, w - left_w, "CHANNELS %d" % len(chans), C_CYN)
        put(iy, ix, "%-14s %7s  %s" % ("channel", "members", "who"), curses.A_BOLD)
        sep(iy + 1, ix, iw)
        for i, ch in enumerate(sorted(chans)[: ih - 2]):
            put(iy + 2 + i, ix, ("%-14s %7d  %s" % (ch, len(chans[ch]), " ".join(sorted(chans[ch]))))[:iw])
        if st.mode == "attach":
            put(iy + 2, ix, "(attach mode: no server log, so no nicks/channels)"[:iw], C_YEL)

        # ---- server log -----------------------------------------------------------------
        y += ch_rows
        with st.lock:
            entries = list(st.log)
        lh = h - y - 1
        iy, ix, ih, iw = box(y, 0, lh, w, "SERVER LOG  %d lines%s" % (
            len(entries), "  [paused]" if st.paused else ("" if opts["follow"] and st.scroll == 0 else "  [scrolled, End to follow]")), C_CYN, focus == 1)
        if ih > 0:
            if opts["follow"] and focus != 1:
                st.scroll = 0
            end = len(entries) - st.scroll
            for i, (t, kind, text) in enumerate(entries[max(0, end - ih): max(0, end)]):
                attr = {"in": C_GRN, "out": C_YEL, "inbot": C_GRN | curses.A_BOLD, "outbot": C_MAG | curses.A_BOLD,
                        "err": C_RED | curses.A_BOLD, "conn": C_CYN}.get(kind, 0)
                put(iy + i, ix, ("%s %s" % (time.strftime("%H:%M:%S", time.localtime(t)), text))[hscroll[1]: hscroll[1] + iw].ljust(iw), attr)

        foot = (" q quit   Tab cycle focus (clients / log / menu)   %s scroll   %s scroll sideways   End follow log   t text snapshot   "
                "'unread by it' = bytes in the kernel that this client has not read yet (red = stalled)" % (G["updown"], G["lr"]))
        if hint:
            foot = " " + hint
        put(h - 1, 0, foot.ljust(w - 1), curses.A_REVERSE)
        stdscr.refresh()

        # ---- keys -------------------------------------------------------------------------
        try:
            k = stdscr.getch()
        except curses.error:
            k = -1
        if k == -1:
            continue
        hint = ""
        if focus == 2:                                   # menu bar
            key, label, kind = MENU[menu_i]
            if k in (curses.KEY_LEFT, ord("h")):
                menu_i = (menu_i - 1) % len(MENU)
            elif k in (curses.KEY_RIGHT, ord("l"), 9):
                menu_i = (menu_i + 1) % len(MENU)
                if k == 9 and menu_i == 0:
                    focus = 0
            elif k in (ord("b"), ord("B"), 27):
                focus = 1
            elif k in (ord("q"), ord("Q")):
                break
            elif k in (10, 13, curses.KEY_ENTER, curses.KEY_UP, curses.KEY_DOWN, ord(" ")):
                step = -1 if k == curses.KEY_DOWN else 1
                if key == "sort":
                    opts["sort"] = (opts["sort"] + step) % 3
                elif key == "graphs":
                    opts["graphs"] = not opts["graphs"]
                elif key == "graph_rows":
                    opts["graph_rows"] = max(-4, min(8, opts["graph_rows"] + step))
                elif key == "client_rows":
                    opts["client_rows"] = max(-6, min(12, opts["client_rows"] + step))
                elif key == "follow":
                    opts["follow"] = not opts["follow"]
                    if opts["follow"]:
                        st.scroll = 0
                elif key == "glyphs":
                    opts["glyphs"] = "ascii" if opts["glyphs"] == "unicode" else "unicode"
                elif key == "snapshot":
                    sys.__stdout__.write(snapshot_text(st) + "\n"); sys.__stdout__.flush()
                    hint = "snapshot written to the terminal behind irctop (visible after q)"
                elif key == "quit":
                    break
            continue
        if k in (ord("q"), ord("Q")):
            break
        elif k == 9:
            focus = {0: 1, 1: 2}[focus]
        elif k == curses.KEY_BTAB:
            focus = {0: 2, 1: 0}[focus]
        elif k in (curses.KEY_UP, ord("k")):
            if focus == 1:
                st.scroll = min(len(entries), st.scroll + 1); opts["follow"] = False
            else:
                client_scroll = max(0, client_scroll - 1)
        elif k in (curses.KEY_DOWN, ord("j")):
            if focus == 1:
                st.scroll = max(0, st.scroll - 1); opts["follow"] = (st.scroll == 0)
            else:
                client_scroll += 1
        elif k in (curses.KEY_LEFT, curses.KEY_RIGHT) and focus in (0, 1):
            hscroll[focus] = max(0, hscroll[focus] + (8 if k == curses.KEY_RIGHT else -8))
        elif k in (curses.KEY_PPAGE, curses.KEY_NPAGE):
            if focus == 1:
                st.scroll = max(0, min(len(entries), st.scroll + (10 if k == curses.KEY_PPAGE else -10))); opts["follow"] = (st.scroll == 0)
            else:
                client_scroll = max(0, client_scroll + (-10 if k == curses.KEY_PPAGE else 10))
        elif k in (curses.KEY_END, ord("G")):
            st.scroll = 0; opts["follow"] = True; hscroll[1] = 0
        elif k in (ord("t"), ord("T")):
            sys.__stdout__.write(snapshot_text(st) + "\n"); sys.__stdout__.flush()
            hint = "snapshot written to the terminal behind irctop (visible after q)"
        elif k in (ord("c"), ord("C")):
            opts["sort"] = (opts["sort"] + 1) % 3
        elif k in (ord("p"), ord("P")):
            opts["follow"] = not opts["follow"]
        elif k in (ord("g"),):
            opts["graphs"] = not opts["graphs"]
        elif k == ord("-"):
            opts["graph_rows"] = max(-4, opts["graph_rows"] - 1)
        elif k in (ord("="), ord("+")):
            opts["graph_rows"] = min(8, opts["graph_rows"] + 1)
        elif k == ord("["):
            opts["client_rows"] = max(-6, opts["client_rows"] - 1)
        elif k == ord("]"):
            opts["client_rows"] = min(12, opts["client_rows"] + 1)
        elif k == curses.KEY_RESIZE:
            pass


# ===========================================================================
# main
# ===========================================================================

def main():
    p = argparse.ArgumentParser(description="btop-style live dashboard for ircserv.")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--start", nargs=3, metavar=("BINARY", "PORT", "PASSWORD"),
                   help="launch the server and read its log live (full picture)")
    g.add_argument("--attach", nargs=2, metavar=("PORT", "PASSWORD"),
                   help="monitor a server that is already running (external view only)")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--text", type=int, default=0, metavar="N",
                   help="print N plain-text snapshots one second apart instead of the curses UI")
    p.add_argument("--no-probe", action="store_true", help="don't keep a probe client connected")
    p.add_argument("--no-strace", action="store_true", help="never attach strace (Linux)")
    p.add_argument("--ascii", action="store_true", help="draw with plain ASCII (auto when curses lacks wide-char support)")
    p.add_argument("--unicode", action="store_true", help="force the unicode glyphs")
    a = p.parse_args()

    st = State()
    launched = None
    if a.start:
        binary, port, password = a.start[0], int(a.start[1]), a.start[2]
        if not os.path.isfile(binary):
            here = os.path.dirname(os.path.abspath(__file__))
            for cand in (os.path.join(here, "..", "..", os.path.basename(binary)), os.path.join(os.getcwd(), os.path.basename(binary))):
                if os.path.isfile(cand):
                    binary = os.path.abspath(cand); break
            else:
                print("binary not found: %s\n  run from tests/tester:  python3 irctop.py --start ../../ircserv 6667 pass\n"
                      "  or from the repo root: python3 tests/tester/irctop.py --start ./ircserv 6667 pass" % binary); sys.exit(1)
        st.mode = "launch"; st.port = port
        try:
            st.poll_sites = scan_poll_sites(binary)
        except Exception:
            st.poll_sites = None
        launched = Launched(st, binary, port, password)
        t0 = time.time()
        while time.time() - t0 < 6:
            try:
                socket.create_connection((a.host, port), timeout=0.5).close(); break
            except OSError:
                if not launched.alive():
                    print("server exited during startup; last log lines:")
                    for (_, _, txt) in list(st.log)[-8:]:
                        print("  " + txt)
                    sys.exit(1)
                time.sleep(0.1)
    else:
        port, password = int(a.attach[0]), a.attach[1]
        st.mode = "attach"; st.port = port
        st.pid = find_pid_on_port(port)
        if st.pid is None:
            st.feed("[irctop] nothing on port %d yet, waiting (start the server or the tester now)" % port)
        else:
            st.feed("[irctop] attached to pid %d on port %d -- external view: CPU/RSS, kernel SendQ per connection, probe latency" % (st.pid, port))

    sampler = Sampler(st); sampler.start()
    probe = None
    if not a.no_probe:
        probe = Probe(st, a.host, port, password); probe.start()
    tracer = None
    if sys.platform.startswith("linux") and not a.no_strace and st.pid:
        tracer = Strace(st, st.pid); tracer.start()
    elif sys.platform == "darwin":
        st.sys_note = "macOS: syscall counts need root (dtrace); showing log-derived counts"

    try:
        if a.text:
            for i in range(a.text):
                time.sleep(1)
                print(snapshot_text(st)); print()
        elif not HAVE_CURSES or not sys.stdout.isatty():
            print("no curses/tty available; use --text N for snapshots")
        else:
            curses.wrapper(ui, st, launched, sampler, probe, "ascii" if a.ascii else ("unicode" if a.unicode else "auto"))
    except KeyboardInterrupt:
        pass
    finally:
        sampler.stop = True
        if probe:
            probe.stop = True
        if tracer:
            tracer.end()
        if launched:
            launched.stop()


if __name__ == "__main__":
    main()
