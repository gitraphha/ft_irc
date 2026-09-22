#!/usr/bin/env python3
"""
ft_irc tester  --  an inspectable, evaluator-facing test harness for 42 ft_irc servers.

Give this folder to anyone doing the project. It drives a server over real TCP,
checks it against the subject + RFC 2812, and records everything so you can SEE
what happened: every line each client sent/received, the server's own log per
scenario, and for each check what it tests, why, expected vs. actual, and how to
fix it. Statuses:

    PASS  (green)  required behaviour works
    FAIL  (red)    a real problem
    NOTE  (amber)  NOT a failure: an optional RFC extra, or a design choice the
                   subject leaves open -- shown with your choice and the alternatives
    SKIP  (grey)   could not run (e.g. no bonus binary)

Implementation profile
----------------------
The subject leaves several things open (line endings, over-long lines, what to do
with a client that stops reading, ...). Those are NOT judged; they are described
by a PROFILE you can set to match your design, so the tester checks what you
intended rather than guessing:

    python3 test_irc.py --show-config            # every option, default, meaning
    python3 test_irc.py --set slow_client=drop   # override one
    (or put key=value lines in tests/tester/tester.conf, or use the menu)

Modes
-----
    mandatory   subject command set + numerics
    robustness  framing, line limits, slow readers, churn, many clients
    load        slow-reader load test: N clients that read 1 byte at a time
    bonus       DCC bot + real file transfer (needs the bonus binary)
    compare     with-bot vs no-bot capture saved to files
    all         everything
    terminal    manual raw IRC terminal
Add --live to watch the server window and the clients' windows move while it runs.

Design notes
------------
* The server is launched on a pseudo-terminal so its stdout stays line-buffered
  and the "server screen" can be captured per scenario in real time.
* Each launched server gets a fresh free port, so phases never race each other
  for a port, and the tester waits until the server actually accepts.
* stdlib only. macOS and Linux.
"""

import argparse
import atexit
import collections
import signal
import datetime
import html
import io
import os
import re
import shlex
import select
import socket
import subprocess
import sys
import threading
import time

try:
    import pty
    HAVE_PTY = True
except ImportError:
    HAVE_PTY = False

try:
    import curses
    HAVE_CURSES = True
except ImportError:
    HAVE_CURSES = False

# helpers shared with irctop.py when it sits next to this file
try:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from irctop import kernel_sendq, ps_stats, spark as _spark, fd_counts, State as MonState
except Exception:
    MonState = None

    def fd_counts(pid):
        return -1, -1
                                  # standalone fallbacks
    def kernel_sendq(port):
        return {}

    def ps_stats(pid):
        try:
            out = subprocess.check_output(["ps", "-o", "rss=,%cpu=", "-p", str(pid)],
                                          stderr=subprocess.DEVNULL).decode().split()
            return int(out[0]) // 1024, float(out[1])
        except Exception:
            return 0, 0.0

    def _spark(values, width):
        vals = list(values)[-width:]
        if not vals:
            return ""
        hi = max(vals) or 1
        return "".join("▁▂▃▄▅▆▇█"[min(7, int(v * 7 / hi))] for v in vals)


# ===========================================================================
# Implementation profile (non-mandatory design choices)
# ===========================================================================

# key, default, kind (list of choices | int | float | str), description
PROFILE_SPEC = [
    ("line_endings", "lf_ok", ["lf_ok", "crlf_only"],
     "Accept a bare \\n as a line ending (lf_ok, what nc/evaluators send) or require \\r\\n only."),
    ("max_line", 510, int,
     "Max message content in bytes, NOT counting CRLF. RFC 2812: 510 (512 with CRLF)."),
    ("overlong", "reject", ["reject", "truncate", "ignore"],
     "What an over-long line gets: reject (417 ERR_INPUTTOOLONG or close), truncate to max_line, or ignore silently."),
    ("overlong_strikes", 3, int,
     "Close the link after N consecutive over-long lines. 0 = never close for this."),
    ("slow_client", "keep", ["keep", "drop"],
     "A client that never reads: keep it (its queue grows) or drop it when sends fail."),
    ("missing_mode_arg", "any", ["461", "specific", "any"],
     "Reply to MODE +k/+l/+o with no argument: 461 ERR_NEEDMOREPARAMS, a specific numeric (467/696), or any defined reply."),
    ("user_modes", "none", ["none", "supported"],
     "Whether user modes (MODE <nick> ...) exist. The subject only requires channel modes."),
    ("welcome_burst", "yes", ["yes", "no"],
     "Whether 002 003 004 005 follow 001 (clients like irssi expect them; the subject doesn't require them)."),
    ("casemapping", "rfc1459", ["rfc1459", "ascii", "none"],
     "How nicks and channel names are case-folded when compared."),
    ("nonmember_send", "reject", ["reject", "deliver"],
     "PRIVMSG/NOTICE to a channel you are not in: reject (404, the +n default) or deliver."),
    ("bot_nick", "DCCBot", str, "Nick of the bonus bot (bonus only)."),
    ("clients", 20, int, "Client count for the many-clients scenario."),
    ("slow_clients", 30, int, "Client count for the slow-reader load scenario."),
    ("rcvbuf", 2048, int,
     "Receive buffer in bytes for stalled/slow test clients. Smaller fills the pipe sooner."),
    ("flood_mb", 4, int, "MB to queue toward a stalled client in the flood scenario."),
    ("timeout", 4.0, float, "Seconds to wait for a reply in each check."),
]

PROFILE = {}
PROFILE_SOURCE = {}


def profile_defaults():
    return dict((k, d) for (k, d, _, _) in PROFILE_SPEC)


def profile_set(key, value, source):
    spec = dict((k, (d, kind, desc)) for (k, d, kind, desc) in PROFILE_SPEC)
    if key not in spec:
        raise ValueError("unknown option %r (see --show-config)" % key)
    d, kind, _ = spec[key]
    if isinstance(kind, list):
        if value not in kind:
            raise ValueError("%s must be one of %s" % (key, "/".join(kind)))
        PROFILE[key] = value
    elif kind is int:
        PROFILE[key] = int(value)
    elif kind is float:
        PROFILE[key] = float(value)
    else:
        PROFILE[key] = str(value)
    PROFILE_SOURCE[key] = source


def load_profile(conf_path, sets):
    PROFILE.clear(); PROFILE_SOURCE.clear()
    PROFILE.update(profile_defaults())
    for k in PROFILE:
        PROFILE_SOURCE[k] = "default"
    if conf_path and os.path.isfile(conf_path):
        for line in open(conf_path):
            line = line.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            k, v = [x.strip() for x in line.split("=", 1)]
            profile_set(k, v, os.path.basename(conf_path))
    for s in sets or []:
        if "=" not in s:
            raise ValueError("--set needs key=value, got %r" % s)
        k, v = [x.strip() for x in s.split("=", 1)]
        profile_set(k, v, "--set")


def show_config():
    print("\nImplementation profile -- the subject leaves these open; set them to match YOUR design.\n")
    print("  %-18s %-12s %-10s %s" % ("option", "current", "source", "meaning / choices"))
    print("  " + "-" * 100)
    for (k, d, kind, desc) in PROFILE_SPEC:
        cur = PROFILE.get(k, d)
        choices = ("[%s]" % "|".join(kind)) if isinstance(kind, list) else ("(%s)" % kind.__name__)
        print("  %-18s %-12s %-10s %s %s" % (k, cur, PROFILE_SOURCE.get(k, "default"), desc, choices))
    print("\n  set with:  --set key=value   (repeatable)   or   key=value lines in tester.conf\n")


def configure_interactive():
    show_config()
    ans = ask("Edit values? [y/N] ", "n").strip().lower()
    if ans not in ("y", "yes"):
        return
    print("\nEnter = keep current, 'b' = back (keeps what you changed so far). Options in [brackets].\n")
    for (k, d, kind, desc) in PROFILE_SPEC:
        cur = PROFILE.get(k, d)
        choices = ("[%s]" % "|".join(kind)) if isinstance(kind, list) else ""
        print("  %s -- %s %s" % (k, desc, choices))
        ans = ask("    %s [%s]: " % (k, cur), "b").strip()
        if ans.lower() == "b":
            break
        if ans:
            try:
                profile_set(k, ans, "menu")
            except ValueError as e:
                print("    ignored: %s" % e)
    print("\nprofile now: %s\n" % profile_summary())


def profile_summary():
    """Only what differs from the defaults, for banners."""
    d = profile_defaults()
    diff = ["%s=%s" % (k, PROFILE[k]) for (k, _, _, _) in PROFILE_SPEC if PROFILE.get(k) != d[k]]
    return ", ".join(diff) if diff else "defaults"


# ===========================================================================
# Terminal style
# ===========================================================================

GREEN = "\033[32m"; RED = "\033[31m"; YELLOW = "\033[33m"
CYAN = "\033[36m"; BLUE = "\033[34m"; MAGENTA = "\033[35m"
DIM = "\033[2m"; RESET = "\033[0m"; BOLD = "\033[1m"
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def strip_ansi(s):
    return ANSI_RE.sub("", s)


FT_BANNER = r"""
      _  _   ____        __ _     _
     | || | |___ \      / _| |_  (_)_ __ ___
     | || |_  __) |    | |_| __| | | '__/ __|
     |__   _|/ __/     |  _| |_  | | | | (__
        |_| |_____|    |_|  \__| |_|_|  \___|   tester
"""


# ===========================================================================
# Result model
# ===========================================================================

PASS = "PASS"; FAIL = "FAIL"; SKIP = "SKIP"; NOTE = "NOTE"; INFO = "INFO"
STATUS_COLOR = {PASS: GREEN, FAIL: RED, SKIP: DIM, NOTE: YELLOW, INFO: CYAN}


class Check(object):
    __slots__ = ("name", "status", "what", "why", "expect", "got", "fix", "dur_ms")

    def __init__(self, name, status, what="", why="", expect="", got="", fix="", dur_ms=0.0):
        self.name = name; self.status = status; self.what = what; self.why = why
        self.expect = expect; self.got = got; self.fix = fix; self.dur_ms = dur_ms


class Scenario(object):
    def __init__(self, key, title, group, blurb):
        self.key = key; self.title = title; self.group = group; self.blurb = blurb
        self.checks = []
        self.events = []            # (t, label, direction, line)
        self.server_lines = []      # (t, text)
        self.t0 = None; self.t1 = None
        self.metrics = []
        self.state = "pending"      # pending / running / done

    def counts(self):
        c = {PASS: 0, FAIL: 0, SKIP: 0, NOTE: 0, INFO: 0}
        for ch in self.checks:
            c[ch.status] = c.get(ch.status, 0) + 1
        return c


class Report(object):
    def __init__(self, args):
        self.args = args
        self.scenarios = []
        self.current = None
        self.server = None
        self.server_cwd = os.getcwd()
        self.t_start = time.time()
        self.lock = threading.Lock()
        self.planned = []           # (key, title, group) for the live scenario list
        self.step = False           # live view: pause after every scenario until told to go on
        self.waiting = False
        self.go = threading.Event()

    def begin(self, key, title, group, blurb):
        sc = Scenario(key, title, group, blurb)
        sc.t0 = time.time(); sc.state = "running"
        with self.lock:
            self.scenarios.append(sc)
            self.current = sc
        PENDING.clear()
        print("\n%s%s[ %s ]%s  %s%s%s" % (BOLD, CYAN, title, RESET, DIM, blurb, RESET))
        return sc

    def end(self):
        sc = self.current
        if sc is None:
            return
        sc.t1 = time.time()
        if self.server is not None:
            time.sleep(0.05)
            sc.server_lines = self.server.slice(sc.t0, time.time())
        sc.state = "done"
        with self.lock:
            self.current = None
        if self.step:               # live view step mode: hold here until the viewer says next / continue
            self.go.clear(); self.waiting = True
            self.go.wait(); self.waiting = False

    def record(self, name, status, what="", why="", expect="", got="", fix="", dur_ms=0.0):
        ch = Check(name, status, what, why, expect, got, fix, dur_ms)
        if self.current is not None:
            self.current.checks.append(ch)
        col = STATUS_COLOR.get(status, "")
        print("  %s[%s]%s %s" % (col, status, RESET, name))
        if status == FAIL:
            for l in strip_ansi(got or "").splitlines()[-4:]:
                print("        %s%s%s" % (YELLOW, l, RESET))
            if fix:
                print("        %s-> %s%s" % (DIM, fix, RESET))
        return ch

    def event(self, t, label, direction, line):
        if self.current is not None:
            self.current.events.append((t, label, direction, line))

    def metric(self, label, value):
        if self.current is not None:
            self.current.metrics.append((label, value))
        print("  %s%s: %s%s" % (DIM, label, value, RESET))

    def totals(self):
        c = {PASS: 0, FAIL: 0, SKIP: 0, NOTE: 0, INFO: 0}
        for sc in self.scenarios:
            for k, v in sc.counts().items():
                c[k] += v
        return c


REPORT = None
RUNNING_SERVERS = []
PENDING = {}
LABELS = {}
T0 = time.time()
TIMEOUT = 4.0
PASSWORD = "pass"


# ===========================================================================
# Wire logging
# ===========================================================================

def wire(s, direction, text):
    label = LABELS.get(s, "fd?")
    for line in text.replace("\r", "").split("\n"):
        if line == "":
            continue
        if REPORT is not None:
            REPORT.event(time.time(), label, direction, line)
        if REPORT is not None and REPORT.args.debug:
            col = CYAN if direction == "<<" else (YELLOW if direction == ">>" else DIM)
            print("    %s%7.3f %-12s %s %s%s" % (DIM, time.time() - T0, label, direction, col + line, RESET))


# ===========================================================================
# Server process on a pty
# ===========================================================================

class ServerProc(object):
    def __init__(self, binary, port, password, logpath, cwd=None):
        self.binary = binary; self.port = port; self.password = password
        self.logpath = logpath; self.cwd = cwd
        self.proc = None; self.buffer = []; self.lock = threading.Lock()
        self.master = None
        self.logf = open(logpath, "w")
        RUNNING_SERVERS.append(self)
        self.msgs_in = collections.deque(maxlen=120)   # per-second counts for live view
        self.msgs_out = collections.deque(maxlen=120)
        self._cin = 0; self._cout = 0; self._last_tick = time.time()
        self._start()

    def _start(self):
        argv = [self.binary, str(self.port), self.password]
        if HAVE_PTY:
            self.master, slave = pty.openpty()
            self.proc = subprocess.Popen(argv, stdout=slave, stderr=slave, cwd=self.cwd, close_fds=True)
            os.close(slave)
            threading.Thread(target=self._pump_pty, daemon=True).start()
        else:
            self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=self.cwd)
            threading.Thread(target=self._pump_pipe, daemon=True).start()

    def _append(self, raw_line):
        t = time.time()
        clean = strip_ansi(raw_line.rstrip("\r\n"))
        low = clean.lower()
        with self.lock:
            self.buffer.append((t, clean))
            if "from fd" in low:
                self._cin += 1
            elif "sendto fd" in low:
                self._cout += 1
            if t - self._last_tick >= 1.0:
                self.msgs_in.append(self._cin); self.msgs_out.append(self._cout)
                self._cin = 0; self._cout = 0; self._last_tick = t
        self.logf.write(raw_line if raw_line.endswith("\n") else raw_line + "\n")
        self.logf.flush()

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
                self._append(line.decode(errors="ignore"))
        if buf:
            self._append(buf.decode(errors="ignore"))

    def _pump_pipe(self):
        for line in iter(self.proc.stdout.readline, b""):
            self._append(line.decode(errors="ignore"))

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def slice(self, t0, t1):
        with self.lock:
            return [(t, txt) for (t, txt) in self.buffer if t0 <= t <= t1]

    def tail(self, n=40):
        with self.lock:
            return [txt for (_, txt) in self.buffer[-n:]]

    def stop(self):
        if self in RUNNING_SERVERS:
            RUNNING_SERVERS.remove(self)
        if self.proc is None:
            return
        try:
            self.proc.terminate(); self.proc.wait(timeout=5)
        except Exception:
            try:
                self.proc.kill(); self.proc.wait(timeout=3)
            except Exception:
                pass
        try:
            if self.master is not None:
                os.close(self.master)
        except OSError:
            pass
        try:
            self.logf.flush(); self.logf.close()
        except Exception:
            pass


# ===========================================================================
# Socket helpers
# ===========================================================================

def can_connect(host, port):
    try:
        socket.create_connection((host, port), timeout=1.5).close()
        return True
    except OSError:
        return False


def find_free_port(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind((host, 0)); return s.getsockname()[1]
    finally:
        s.close()


def connect(rcvbuf=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if rcvbuf:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    s.settimeout(3)
    s.connect((REPORT.args.host, REPORT.args.port))
    s.setblocking(False)
    return s


def send(s, line):
    wire(s, ">>", line)
    s.sendall((line.rstrip("\r\n") + "\r\n").encode())


def send_raw(s, data):
    shown = data.decode(errors="ignore").replace("\r", "\\r").replace("\n", "\\n")
    wire(s, ">>", shown)
    s.sendall(data)


def _read_more(s, wait):
    r, _, _ = select.select([s], [], [], wait)
    if s not in r:
        return True
    try:
        chunk = s.recv(4096)
    except socket.error:
        return False
    if not chunk:
        return False
    text = chunk.decode(errors="ignore")
    wire(s, "<<", text)
    PENDING[s] = PENDING.get(s, "") + text
    return True


def _take_through(s, needle):
    buf = PENDING.get(s, "")
    pos = buf.find(needle)
    if pos < 0:
        return None
    end = buf.find("\n", pos)
    end = len(buf) if end < 0 else end + 1
    PENDING[s] = buf[end:]
    return buf[:end]


def recv_until(s, needle, timeout=None):
    timeout = TIMEOUT if timeout is None else timeout
    end = time.time() + timeout
    while True:
        got = _take_through(s, needle)
        if got is not None:
            return True, got
        left = end - time.time()
        if left <= 0:
            break
        if not _read_more(s, min(left, 0.2)):
            break
    return False, PENDING.get(s, "")


def recv_any(s, needles, timeout=None):
    timeout = TIMEOUT if timeout is None else timeout
    end = time.time() + timeout
    while True:
        for needle in needles:
            got = _take_through(s, needle)
            if got is not None:
                return True, got
        left = end - time.time()
        if left <= 0:
            break
        if not _read_more(s, min(left, 0.2)):
            break
    return False, PENDING.get(s, "")


def drain(s, timeout=0.4):
    end = time.time() + timeout
    while time.time() < end:
        before = len(PENDING.get(s, ""))
        if not _read_more(s, max(0.01, end - time.time())):
            break
        if len(PENDING.get(s, "")) == before:
            break
    return PENDING.pop(s, "")


def closed(s, wait=0.4):
    end = time.time() + wait
    while time.time() < end:
        r, _, _ = select.select([s], [], [], max(0.01, end - time.time()))
        if s in r:
            try:
                if s.recv(1) == b"":
                    return True
            except (BlockingIOError, socket.error):
                return False
    return False


def register(s, nick, realname=None):
    realname = realname or nick
    LABELS[s] = nick
    send(s, "PASS %s" % PASSWORD); send(s, "NICK %s" % nick); send(s, "USER %s 0 * :%s" % (nick, realname))
    return recv_until(s, "001 %s" % nick, timeout=3)


def safe_close(*sockets):
    for s in sockets:
        PENDING.pop(s, None); LABELS.pop(s, None)
        try:
            s.close()
        except OSError:
            pass


def server_rss_mb():
    srv = REPORT.server
    if srv is None or getattr(srv, "proc", None) is None:
        return -1
    return ps_stats(srv.proc.pid)[0]



def _kill_servers():
    """atexit / signal safety net: never leave a launched ircserv behind."""
    for srv in list(RUNNING_SERVERS):
        try:
            srv.stop()
        except Exception:
            pass


def _on_signal(signum, frame):
    # SIGTERM/SIGHUP behave like Ctrl-C: unwind through the finally blocks, which stop the
    # server, restore the terminal and still write the report for what has run.
    raise KeyboardInterrupt()


def ask(prompt, default=""):
    """input() that treats Ctrl-D (EOF) as 'use the default' instead of crashing."""
    try:
        return input(prompt)
    except EOFError:
        print()
        return default


# ===========================================================================
# Check helpers
# ===========================================================================

def ok_fail(name, ok, what, why, expect, got, fix):
    REPORT.record(name, PASS if ok else FAIL, what=what, why=why, expect=expect,
                  got=strip_ansi(got or "").strip(), fix=fix if not ok else "")


def note(name, what, why, expect, got, note_text):
    """A design choice or optional extra: never a failure."""
    REPORT.record(name, NOTE, what=what, why=why, expect=expect,
                  got=strip_ansi(got or "").strip(), fix=note_text)


def subset(name, strict_ok, subset_ok, what, why, expect, got, note_text):
    if strict_ok:
        REPORT.record(name, PASS, what=what, why=why, expect=expect, got=strip_ansi(got or "").strip())
    elif subset_ok:
        note(name, what, why, expect, got, note_text)
    else:
        REPORT.record(name, FAIL, what=what, why=why, expect=expect, got=strip_ansi(got or "").strip(),
                      fix="Neither the required nor an acceptable alternative response was seen.")


def skip(name, reason):
    REPORT.record(name, SKIP, what=reason)



# ===========================================================================
# DESIGN: one poll() for everything, non-blocking fds, no fork
# ===========================================================================

def _strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: " " * m.group(0).count("\n") * 0 + "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _source_files(root):
    dirs = [d for d in (os.path.join(root, "srcs"), os.path.join(root, "src"), os.path.join(root, "includes"),
                        os.path.join(root, "inc"), os.path.join(root, "include")) if os.path.isdir(d)] or [root]
    out = []
    for d in dirs:
        for base, _, names in os.walk(d):
            if any(x in base for x in ("/tests", "/test", "/build", "/.git")):
                continue
            for n in names:
                if n.endswith((".cpp", ".hpp", ".cc", ".h", ".c", ".tpp")):
                    out.append(os.path.join(base, n))
    return sorted(out)


def _find_calls(files, pattern, root, include_bonus=True):
    """Call sites matching pattern as 'file:line'. Tracks #ifdef BONUS / #if defined(BONUS)
    regions so the mandatory build can be judged without bonus-only code."""
    rx = re.compile(pattern)
    hits = []; bonus_hits = []
    for f in files:
        try:
            text = _strip_c_comments(open(f, errors="ignore").read())
        except OSError:
            continue
        stack = []          # True for a BONUS region, False for any other #if
        for i, line in enumerate(text.split("\n"), 1):
            st = line.strip()
            if st.startswith("#if"):
                stack.append(bool(re.search(r"\bBONUS\b", st)))
            elif st.startswith("#endif"):
                if stack:
                    stack.pop()
            elif st.startswith("#else") and stack:
                stack[-1] = not stack[-1] if stack[-1] else stack[-1]
            if rx.search(line):
                loc = "%s:%d" % (os.path.relpath(f, root), i)
                if any(stack):
                    bonus_hits.append(loc)
                else:
                    hits.append(loc)
    return (hits + bonus_hits) if include_bonus else hits


def _threads_and_children(pid):
    threads = -1; children = -1
    try:
        for line in open("/proc/%d/status" % pid):
            if line.startswith("Threads:"):
                threads = int(line.split()[1]); break
    except OSError:
        try:
            out = subprocess.check_output(["ps", "-M", "-p", str(pid)], stderr=subprocess.DEVNULL).decode().splitlines()
            threads = max(0, len(out) - 1)
        except Exception:
            pass
    try:
        out = subprocess.run(["pgrep", "-P", str(pid)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode().split()
        children = len(out)
    except Exception:
        pass
    return threads, children


def sc_single_poll():
    REPORT.begin("poll", "One poll() for everything, non-blocking, no fork (subject core rule)", "Mandatory",
                 "The subject: a single poll() (or equivalent) handles every read and write, fds are "
                 "non-blocking, no forking, and errno must not drive decisions after read/write. "
                 "Checked in the sources next to the binary and on the running process.")
    root = os.path.dirname(os.path.abspath(REPORT.args.binary))
    files = _source_files(root)
    if not files:
        skip("Source scan", "no .cpp/.hpp files found under %s (only the running-process checks apply)" % root)
    else:
        REPORT.metric("Source files scanned", "%d under %s" % (len(files), root))
        polls_all = _find_calls(files, r"(?<![\w_])poll\s*\(", root)
        polls = _find_calls(files, r"(?<![\w_])poll\s*\(", root, include_bonus=False)
        bonus_polls = [x for x in polls_all if x not in polls]
        equiv = _find_calls(files, r"(?<![\w_])(select|epoll_wait|epoll_create1?|kqueue|kevent)\s*\(", root)
        forks = _find_calls(files, r"(?<![\w_])fork\s*\(", root)
        sleeps = _find_calls(files, r"(?<![\w_])(usleep|sleep|nanosleep)\s*\(", root)
        nonblk = _find_calls(files, r"O_NONBLOCK", root)
        errnos = _find_calls(files, r"(?<![\w_])errno(?![\w_])", root)
        ok_fail("Exactly one poll() call site in the mandatory build", len(polls) == 1,
                "Count poll( call sites (comments stripped, code under #ifdef BONUS excluded) in srcs/ and includes/.",
                "The subject allows only one poll() (or equivalent) for all I/O.",
                "1 call site.", "poll() at: %s%s" % (", ".join(polls) or "none found",
                                                    ("   [bonus-only, not in this build: %s]" % ", ".join(bonus_polls)) if bonus_polls else ""),
                "Route every read and write through the one poll loop; remove any extra poll()/select().")
        if bonus_polls:
            REPORT.record("poll() sites that exist only under #ifdef BONUS (not in this binary)", INFO,
                          what="Listed for completeness; they are compiled out of the mandatory build.",
                          why="The one-poll rule also applies to the bonus build; the bonus suite checks that build.",
                          expect="", got=", ".join(bonus_polls))
        ok_fail("No select()/epoll/kqueue used alongside it", not equiv,
                "Scan for other multiplexers.", "Mixing mechanisms breaks the single-poll rule.",
                "none.", ", ".join(equiv) or "none", "Use only the one poll().")
        ok_fail("No fork()", not forks, "Scan for fork(.", "Forking is forbidden by the subject.", "none.", ", ".join(forks) or "none",
                "Remove fork(); the server must be a single process.")
        subset("Sockets set non-blocking with fcntl(O_NONBLOCK)", bool(nonblk), True,
               "Scan for O_NONBLOCK.", "Non-blocking fds are required (macOS: fcntl(fd, F_SETFL, O_NONBLOCK)).",
               "at least one O_NONBLOCK site.", ", ".join(nonblk) or "not found",
               "Not found by name: if you set it another way, fine; otherwise a blocking recv/send can stall the loop.")
        if sleeps:
            note("sleep()/usleep() in the sources", "Scan for sleep calls.", "A sleeping server is not serving; also not in most allowed-function lists.",
                 "none", ", ".join(sleeps), "Remove sleeps; rely on poll()'s timeout instead.")
        if not errnos:
            REPORT.record("No errno anywhere in the sources (subject: never decide on errno after read/recv/write/send)", PASS,
                          what="Scan for the identifier errno.", why="The subject forbids using errno after a read/write; not using it at all satisfies that trivially.",
                          expect="no errno", got="none found")
        else:
            # contextual: an errno within 6 lines after a recv/send/read/write call in the same file
            bad = []
            for f in files:
                try:
                    lines = _strip_c_comments(open(f, errors="ignore").read()).split("\n")
                except OSError:
                    continue
                for i, line in enumerate(lines):
                    if re.search(r"(?<![\w_])errno(?![\w_])", line):
                        window = "\n".join(lines[max(0, i - 6): i + 1])
                        if re.search(r"(?<![\w_])(recv|send|read|write)\s*\(", window):
                            bad.append("%s:%d" % (os.path.relpath(f, root), i + 1))
            ok_fail("errno is never consulted after recv()/send()/read()/write()", not bad,
                    "Every errno reference is checked for a recv/send/read/write call within the 6 lines before it.",
                    "The subject explicitly bans deciding on errno after a read/write.",
                    "no errno right after a read/write (errno after bind/accept/poll is fine).",
                    "errno at: %s%s" % (", ".join(errnos), ("   -- after a read/write at: " + ", ".join(bad)) if bad else ""),
                    "Use the return value of recv/send (<= 0 -> stop or close) instead of errno.")
    # running-process evidence
    srv = REPORT.server
    if srv is not None and srv.proc is not None:
        a = connect(); register(a, "pl_a"); b = connect(); register(b, "pl_b")
        send(a, "JOIN #pl"); send(b, "JOIN #pl"); time.sleep(0.2)
        for i in range(30):
            send(a, "PRIVMSG #pl :tick %d" % i)
        drain(b, 0.3)
        thr, kids = _threads_and_children(srv.proc.pid)
        ok_fail("Runs as a single thread with no child processes (while serving 2 clients)",
                (thr in (1, -1)) and (kids in (0, -1)) and not (thr == -1 and kids == -1),
                "Count threads and children of the server pid under traffic.",
                "One process, one thread, one poll loop is the design the subject asks for.",
                "threads=1, children=0.", "threads=%s children=%s" % (thr if thr >= 0 else "?", kids if kids >= 0 else "?"),
                "Extra threads or children mean concurrency is not coming from the single poll().")
        # optional: syscall census on Linux with strace
        if sys.platform.startswith("linux") and not REPORT.args.no_start:
            try:
                st = subprocess.Popen(["strace", "-c", "-f", "-p", str(srv.proc.pid)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                time.sleep(0.3)
                for i in range(40):
                    send(a, "PRIVMSG #pl :census %d" % i)
                send(b, "PING :x"); recv_until(b, "PONG", timeout=2); drain(b, 0.3)
                time.sleep(0.5); st.send_signal(2)
                out = st.communicate(timeout=5)[1].decode(errors="ignore")
                counts = {}
                for line in out.splitlines():
                    f = line.split()
                    if len(f) >= 5 and f[-1].isidentifier():
                        try:
                            counts[f[-1]] = int(f[3])
                        except ValueError:
                            pass
                if counts:
                    polln = counts.get("poll", 0) + counts.get("ppoll", 0)
                    others = sum(counts.get(k, 0) for k in ("select", "pselect6", "epoll_wait", "epoll_pwait", "kevent"))
                    io = sum(counts.get(k, 0) for k in ("recvfrom", "recv", "read", "sendto", "send", "write"))
                    ok_fail("Syscall census: poll() is the only multiplexer used (strace)", polln > 0 and others == 0,
                            "strace -c on the running server during traffic.", "Direct evidence of the mechanism, not just the source.",
                            "poll > 0, no select/epoll.", "poll=%d others=%d recv/send=%d" % (polln, others, io), "Only poll() should appear.")
                    REPORT.metric("strace census", "poll=%d, recv/send=%d, other multiplexers=%d" % (polln, io, others))
                else:
                    skip("Syscall census (strace)", "strace produced no counts (ptrace not permitted here?)")
            except (OSError, subprocess.TimeoutExpired):
                skip("Syscall census (strace)", "strace not available")
        else:
            skip("Syscall census (strace)", "Linux + launched server only (macOS has no strace; use the source scan + thread check)")
        safe_close(a, b)
    REPORT.end()

# ===========================================================================
# MANDATORY scenarios
# ===========================================================================

def sc_authentication():
    REPORT.begin("auth", "Authentication (PASS/NICK/USER)", "Mandatory",
                 "The subject requires a password-gated connection: wrong/missing passwords are "
                 "refused and a correct handshake yields 001.")
    c = connect(); LABELS[c] = "wrongpass"
    send(c, "PASS wrongpass"); send(c, "NICK w1"); send(c, "USER w1 0 * :w")
    ok, buf = recv_until(c, "464", timeout=2)
    was_closed = closed(c, 1.0)
    ok_fail("Wrong password -> 464, then the link is closed", ok and was_closed,
            "Client registers with the wrong server password.",
            "The subject makes the password mandatory; RFC 2812 answers with 464 ERR_PASSWDMISMATCH before closing.",
            "464 ERR_PASSWDMISMATCH, then EOF.", buf or ("<nothing, then EOF>" if was_closed else "<nothing>"),
            "Send 464 and make sure it is flushed to the socket before the fd is closed (a close right after "
            "queueing it drops the line).")
    safe_close(c)

    c = connect(); LABELS[c] = "nopass"
    send(c, "NICK n1"); send(c, "USER n1 0 * :n")
    ok, buf = recv_until(c, "464", timeout=2)
    was_closed = closed(c, 1.0)
    ok_fail("Missing PASS -> 464, then the link is closed", ok and was_closed,
            "Client sends NICK/USER but never PASS.", "No password must be treated like a wrong password.",
            "464, then EOF.", buf or ("<nothing, then EOF>" if was_closed else "<nothing>"),
            "Never complete registration without an accepted PASS; send 464 and flush it before closing.")
    safe_close(c)

    c = connect()
    ok, buf = register(c, "authgood")
    ok_fail("Correct PASS/NICK/USER -> 001 welcome", ok, "A correct handshake.",
            "The happy path must produce the welcome.", "001 RPL_WELCOME.", buf,
            "Send 001 once PASS, NICK and USER are all satisfied.")
    got = buf + drain(c, 0.4)
    if PROFILE["welcome_burst"] == "yes":
        for code, label in (("002", "RPL_YOURHOST"), ("003", "RPL_CREATED"), ("004", "RPL_MYINFO"), ("005", "RPL_ISUPPORT")):
            ok_fail("Welcome burst includes %s %s" % (code, label), code in got,
                    "Optional welcome numerics after 001.", "Your profile says you send them (welcome_burst=yes).",
                    "%s present." % code, got, "Send %s after 001, or set welcome_burst=no." % code)
    else:
        note("Welcome burst 002-005", "Optional numerics after 001.",
             "The subject doesn't require them; irssi and friends like them.",
             "002-005 after 001 (optional)", got,
             "Your profile says welcome_burst=no. Alternative: send 002 003 004 005 for client compatibility.")
    safe_close(c)

    c = connect(); LABELS[c] = "capclient"
    send(c, "CAP LS 302")
    ok, buf = recv_until(c, "CAP", timeout=2)
    subset("CAP LS 302 before registration -> a CAP * LS reply (IRCv3 handshake irssi/HexChat send first)",
           ok and " LS" in buf, not buf.strip(),
           "The first thing modern clients send.", "If CAP is unknown and answered with 421 or ignored, most clients still carry on; answering CAP LS keeps them happy.",
           ":server CAP * LS :<caps or empty>", buf,
           "Silence is tolerated by most clients; answering CAP LS with an (empty) list is the clean choice. 421 here can confuse some clients.")
    send(c, "PASS %s" % PASSWORD); send(c, "NICK capu"); send(c, "USER c 0 * :c"); send(c, "CAP END")
    ok, buf = recv_until(c, "001 capu", timeout=3)
    ok_fail("Registration completes with CAP LS ... CAP END around it", ok, "The full irssi-style handshake.",
            "CAP negotiation must not block or break registration.", "001 capu.", buf, "Treat CAP END as a no-op and register normally.")
    safe_close(c)

    c = connect(); register(c, "reauth")
    send(c, "PASS %s" % PASSWORD)
    ok, buf = recv_until(c, "462", timeout=2)
    subset("PASS after registration -> 462 ERR_ALREADYREGISTERED", ok, ("462" in buf) or (not buf.strip()),
           "Re-sending PASS on a registered connection.", "RFC 2812: registration commands are refused once registered.",
           "462.", buf, "Silently ignoring it is acceptable; 462 is the RFC reply.")
    send(c, "USER x 0 * :x")
    ok, buf = recv_until(c, "462", timeout=2)
    subset("USER after registration -> 462 ERR_ALREADYREGISTERED", ok, ("462" in buf) or (not buf.strip()),
           "Re-sending USER after registration.", "Same rule as PASS.", "462.", buf,
           "Silently ignoring it is acceptable; 462 is the RFC reply.")
    safe_close(c)
    REPORT.end()


def sc_nick():
    REPORT.begin("nick", "NICK", "Mandatory",
                 "Nickname assignment, change broadcast, duplicate and malformed rejection, and the "
                 "order of NICK/USER.")
    c = connect(); ok, buf = register(c, "nick_a")
    ok_fail("Registration with a valid nick", ok, "Register nick_a.", "Baseline.", "001.", buf, "See Authentication.")
    send(c, "NICK nick_a2")
    ok, buf = recv_until(c, "NICK", timeout=2)
    ok_fail("NICK change is echoed/broadcast", ok, "Change nick_a -> nick_a2.",
            "A live nick change must be announced so peers stay consistent.",
            ":nick_a!... NICK nick_a2", buf, "Broadcast the NICK to the user and every channel they share.")

    c2 = connect(); register(c2, "dup_taken")
    c3 = connect(); LABELS[c3] = "dup_try"
    send(c3, "PASS %s" % PASSWORD); send(c3, "NICK dup_taken"); send(c3, "USER d 0 * :d")
    ok, buf = recv_until(c3, "433", timeout=2)
    ok_fail("Duplicate nick -> 433 ERR_NICKNAMEINUSE", ok, "Second client claims an in-use nick.",
            "Nicks must be unique.", "433.", buf, "Reject the NICK; don't register a clash.")

    c4 = connect(); LABELS[c4] = "badnick"
    send(c4, "PASS %s" % PASSWORD); send(c4, "NICK 1nvalid!"); send(c4, "USER b 0 * :b")
    ok, buf = recv_until(c4, "432", timeout=2)
    subset("Erroneous nick -> 432 ERR_ERRONEUSNICKNAME", ok, ("432" in buf) or ("001" not in buf),
           "Nick with a leading digit and '!'.", "RFC nick grammar forbids these.",
           "432.", buf, "Not validating is acceptable as long as the bad nick is never registered; 432 is the RFC reply.")

    # USER before NICK: correct behaviour is to WAIT (no reply) until NICK arrives.
    c5 = connect(); LABELS[c5] = "nonick"
    send(c5, "PASS %s" % PASSWORD); send(c5, "USER u 0 * :u")
    got = drain(c5, 0.8)
    ok_fail("USER before NICK -> registration waits (no 001 yet)", "001" not in got,
            "PASS and USER are sent, NICK never is.",
            "Registration needs all three; nothing to reply until NICK arrives. Silence is correct.",
            "no 001.", got, "If 001 appears here the server registers a client with no nickname.")
    send(c5, "NICK late")
    ok, buf = recv_until(c5, "001 late", timeout=2)
    ok_fail("...then NICK arrives -> 001 completes the registration", ok, "NICK after PASS+USER.",
            "NICK/USER order must not matter.", "001 late.", buf, "Complete registration once all three are present, in any order.")

    c6 = connect(); LABELS[c6] = "barenick"
    send(c6, "PASS %s" % PASSWORD); send(c6, "NICK")
    ok, buf = recv_until(c6, "431", timeout=2)
    ok_fail("Bare NICK (no parameter) -> 431 ERR_NONICKNAMEGIVEN", ok, "NICK with no nickname.",
            "This is what 431 is for.", "431.", buf, "Return 431 when NICK has no parameter.")
    safe_close(c, c2, c3, c4, c5, c6)
    REPORT.end()


def sc_join():
    REPORT.begin("join", "JOIN", "Mandatory", "Joining, the names burst, broadcast to members, and rejects.")
    a = connect(); b = connect(); register(a, "join_a"); register(b, "join_b")
    send(a, "JOIN #room")
    ok, buf = recv_until(a, "JOIN #room", timeout=2)
    ok_fail("JOIN -> joiner receives its own JOIN", ok, "Alice joins #room.",
            "The server confirms membership by echoing the JOIN.", ":join_a!... JOIN #room", buf, "Echo the JOIN to the joiner.")
    ok, buf = recv_until(a, "353", timeout=2)
    ok_fail("JOIN -> 353 RPL_NAMREPLY", ok, "The names list.", "Clients build the member list from 353.",
            "353 ... :@join_a", buf, "Send 353 with the member list (operator prefixed @).")
    ok, buf = recv_until(a, "366", timeout=2)
    ok_fail("JOIN -> 366 RPL_ENDOFNAMES", ok, "End of names.", "366 ends the list.", "366.", buf, "Send 366 after 353.")
    send(b, "JOIN #room")
    ok, buf = recv_until(a, "JOIN #room", timeout=2)
    ok_fail("Second JOIN is broadcast to existing members", ok, "Bob joins; Alice should see it.",
            "Members must learn who joins.", ":join_b!... JOIN #room", buf, "Fan out the JOIN to all members.")
    send(a, "JOIN nohash")
    ok, buf = recv_until(a, "403", timeout=2)
    subset("JOIN a name without a valid prefix -> 403", ok, ("403" in buf) or ("476" in buf),
           "JOIN 'nohash' (no #/&).", "Channel names must start with a channel type char.",
           "403 (or 476).", buf, "Either numeric is defensible.")
    send(a, "JOIN #a,#b,#c"); got = drain(a, 0.6)
    subset("Comma-separated JOIN #a,#b,#c joins several", got.count("JOIN #") >= 2, "JOIN #" in got,
           "One JOIN with a comma list.", "RFC allows a target list; optional for the subject.",
           "a JOIN for each of #a,#b,#c.", got, "Only the first joined -- acceptable; splitting on ',' is the RFC behaviour.")
    send(a, "JOIN")
    ok, buf = recv_until(a, "461", timeout=2)
    ok_fail("JOIN with no params -> 461 ERR_NEEDMOREPARAMS", ok, "Bare JOIN.", "Missing parameter.", "461.", buf, "Return 461.")
    safe_close(a, b)
    REPORT.end()


def sc_part():
    REPORT.begin("part", "PART", "Mandatory", "Leaving a channel, broadcast, and the not-a-member reject.")
    a = connect(); b = connect(); register(a, "part_a"); register(b, "part_b")
    send(a, "JOIN #p"); send(b, "JOIN #p"); time.sleep(0.2)
    send(a, "PART #p :bye")
    ok, buf = recv_until(b, "PART", timeout=2)
    ok_fail("PART broadcast to remaining members", ok, "Alice parts #p.", "Members must see departures.",
            ":part_a!... PART #p :bye", buf, "Fan out PART before removing the member.")
    send(a, "PART #p")
    ok, buf = recv_until(a, "442", timeout=2)
    ok_fail("PART a channel you're not in -> 442 ERR_NOTONCHANNEL", ok, "Alice parts again.",
            "Can't leave what you're not in.", "442.", buf, "Return 442 when the sender is not a member.")
    send(a, "PART #ghost")
    ok, buf = recv_until(a, "403", timeout=2)
    subset("PART a non-existent channel -> 403", ok, ("403" in buf) or ("442" in buf),
           "PART #ghost.", "Unknown channel.", "403 (or 442).", buf, "Both seen in the wild.")
    safe_close(a, b)
    REPORT.end()


def sc_topic():
    REPORT.begin("topic", "TOPIC", "Mandatory", "Set/query topic, +t restriction, and that a rejected change does not stick.")
    a = connect(); b = connect(); register(a, "topic_a"); register(b, "topic_b")
    send(a, "JOIN #t"); send(b, "JOIN #t"); time.sleep(0.2)
    send(a, "TOPIC #t :hello world")
    ok, buf = recv_until(b, "TOPIC #t :hello world", timeout=2)
    ok_fail("TOPIC set -> broadcast to members", ok, "Alice sets the topic.", "A new topic must reach everyone.",
            ":topic_a!... TOPIC #t :hello world", buf, "Broadcast the TOPIC to all members.")
    send(b, "TOPIC #t")
    ok, buf = recv_until(b, "332", timeout=2)
    ok_fail("TOPIC query -> 332 RPL_TOPIC", ok, "Bob queries the topic.", "Query returns the current topic.",
            "332 #t :hello world", buf, "Answer a paramless TOPIC with 332 (331 if unset).")
    send(a, "MODE #t +t"); time.sleep(0.2)
    send(b, "TOPIC #t :hijack")
    ok, buf = recv_until(b, "482", timeout=2)
    ok_fail("+t: non-op TOPIC change -> 482", ok, "Bob (non-op) sets topic under +t.", "+t restricts TOPIC to operators.",
            "482.", buf, "Gate TOPIC on operator status when +t is set.")
    send(b, "TOPIC #t")
    ok, buf = recv_until(b, "332", timeout=2)
    ok_fail("Rejected TOPIC did not change the topic", ok and "hello world" in buf and "hijack" not in buf,
            "Re-query after the rejected change.", "A refused change must not mutate state.",
            "332 still 'hello world'.", buf, "Only apply the topic after the op check passes.")
    safe_close(a, b)
    REPORT.end()


def sc_privmsg():
    REPORT.begin("privmsg", "PRIVMSG / NOTICE", "Mandatory", "Channel + private delivery, no self-echo, and the error numerics.")
    a = connect(); b = connect(); register(a, "pm_a"); register(b, "pm_b")
    send(a, "JOIN #m"); send(b, "JOIN #m"); time.sleep(0.2)
    send(a, "PRIVMSG #m :hi all")
    ok, buf = recv_until(b, "PRIVMSG #m :hi all", timeout=2)
    ok_fail("PRIVMSG to channel reaches other members", ok, "Alice messages #m.", "Core messaging.",
            ":pm_a!... PRIVMSG #m :hi all", buf, "Fan out to members except the sender.")
    ok_fail("Sender does not receive its own channel message", "hi all" not in drain(a, 0.3),
            "Alice's own stream.", "IRC never echoes your own PRIVMSG.", "nothing", "", "Exclude the sender when fanning out.")
    send(a, "PRIVMSG pm_b :just you")
    ok, buf = recv_until(b, "PRIVMSG pm_b :just you", timeout=2)
    ok_fail("PRIVMSG to a nick is delivered privately", ok, "Alice DMs Bob.", "Private messaging.",
            ":pm_a!... PRIVMSG pm_b :just you", buf, "Route nick targets to that client.")
    send(a, "NOTICE pm_b :note")
    ok, buf = recv_until(b, "NOTICE", timeout=2)
    ok_fail("NOTICE to a nick is delivered", ok, "Alice NOTICEs Bob.", "NOTICE = PRIVMSG without error replies.",
            ":pm_a!... NOTICE pm_b :note", buf, "Deliver NOTICE like PRIVMSG; never answer it with an error.")
    send(a, "PRIVMSG ghost :hi")
    ok, buf = recv_until(a, "401", timeout=2)
    ok_fail("PRIVMSG unknown nick -> 401 ERR_NOSUCHNICK", ok, "Message a missing nick.", "Undeliverable target must be reported.",
            "401.", buf, "Return 401 when the nick isn't found.")
    send(a, "PRIVMSG #m")
    ok, buf = recv_until(a, "412", timeout=2)
    subset("PRIVMSG with no text -> 412 ERR_NOTEXTTOSEND", ok, ("412" in buf) or ("461" in buf),
           "PRIVMSG target, no message.", "No body to deliver.", "412 (or 461).", buf, "412 is the RFC reply; 461 acceptable.")
    send(a, "PRIVMSG")
    ok, buf = recv_until(a, "411", timeout=2)
    subset("PRIVMSG with no recipient -> 411 ERR_NORECIPIENT", ok, ("411" in buf) or ("461" in buf),
           "Bare PRIVMSG.", "No target.", "411 (or 461).", buf, "411 is the RFC reply; 461 acceptable.")
    send(a, "NOTICE ghost :x"); got = drain(a, 0.6)
    ok_fail("NOTICE to unknown nick stays silent (no 401)", "401" not in got, "NOTICE a missing nick.",
            "NOTICE must never generate an error reply.", "silence", got, "Suppress error numerics on the NOTICE path.")
    # non-member send
    o = connect(); register(o, "outsider"); drain(b, 0.2)
    send(o, "PRIVMSG #m :from outside"); got_o = drain(o, 0.6); got_b = drain(b, 0.6)
    if PROFILE["nonmember_send"] == "reject":
        ok_fail("PRIVMSG to a channel you're not in -> rejected (404), not delivered",
                ("404" in got_o or not ("from outside" in got_b)) and "from outside" not in got_b,
                "An outsider messages #m.", "Your profile: nonmember_send=reject (the common +n behaviour).",
                "404 ERR_CANNOTSENDTOCHAN to the sender; nothing at the members.", got_o + got_b,
                "Reject non-member sends, or set nonmember_send=deliver if you allow them.")
    else:
        ok_fail("PRIVMSG to a channel you're not in -> delivered (your profile allows it)", "from outside" in got_b,
                "An outsider messages #m.", "Your profile: nonmember_send=deliver.", "message at the members.",
                got_b, "Deliver, or set nonmember_send=reject.")
    safe_close(a, b, o)
    REPORT.end()


def sc_kick():
    REPORT.begin("kick", "KICK", "Mandatory", "Operator kicks, broadcast, and the non-op / bad-target rejects.")
    a = connect(); b = connect(); c = connect(); register(a, "kick_a"); register(b, "kick_b"); register(c, "kick_c")
    send(a, "JOIN #k"); send(b, "JOIN #k"); send(c, "JOIN #k"); time.sleep(0.3)
    send(a, "KICK #k kick_b :bye")
    ok, buf = recv_until(b, "KICK #k kick_b", timeout=2)
    ok_fail("KICK by op -> target receives KICK", ok, "Alice kicks Bob.", "Operators remove members.",
            ":kick_a!... KICK #k kick_b :bye", buf, "Send the KICK to the target and the channel.")
    ok, buf = recv_until(c, "KICK", timeout=2)
    ok_fail("KICK is broadcast to the channel", ok, "Carol should see it.", "Everyone learns of the removal.",
            "KICK in Carol's stream.", buf, "Fan out the KICK to all members.")
    send(c, "KICK #k kick_a :nope")
    ok, buf = recv_until(c, "482", timeout=2)
    ok_fail("KICK by non-op -> 482", ok, "Carol (non-op) kicks.", "Only ops may kick.", "482.", buf, "Gate KICK on operator status.")
    send(a, "KICK #ghost kick_b :x")
    ok, buf = recv_until(a, "403", timeout=2)
    ok_fail("KICK on a missing channel -> 403", ok, "Kick in a channel that doesn't exist.", "Unknown channel.", "403.", buf, "Return 403 first.")
    d = connect(); register(d, "kick_d")
    send(a, "KICK #k kick_d :outsider")
    ok, buf = recv_until(a, "441", timeout=2)
    ok_fail("KICK a non-member -> 441 ERR_USERNOTINCHANNEL", ok, "Kick someone not in the channel.",
            "Target must be a member.", "441.", buf, "Return 441 when the target isn't in the channel.")
    send(a, "KICK")
    ok, buf = recv_until(a, "461", timeout=2)
    ok_fail("KICK with no params -> 461", ok, "Bare KICK.", "Missing params.", "461.", buf, "Return 461.")
    safe_close(a, b, c, d)
    REPORT.end()


def sc_invite():
    REPORT.begin("invite", "INVITE", "Mandatory", "Invitations, the 341 reply, and +i enforcement.")
    a = connect(); b = connect(); c = connect(); register(a, "inv_a"); register(b, "inv_b"); register(c, "inv_c")
    send(a, "JOIN #i"); time.sleep(0.2)
    send(a, "INVITE inv_b #i")
    ok, buf = recv_until(b, "INVITE", timeout=2)
    ok_fail("INVITE -> target receives it", ok, "Alice invites Bob.", "Invited user is notified.",
            ":inv_a!... INVITE inv_b #i", buf, "Send the INVITE to the target.")
    ok, buf = recv_until(a, "341", timeout=2)
    ok_fail("INVITE -> inviter gets 341 RPL_INVITING", ok, "Confirmation to Alice.", "The inviter is told it worked.",
            "341 inv_a #i inv_b", buf, "Send 341 back.")
    send(a, "MODE #i +i"); time.sleep(0.2)
    send(c, "JOIN #i")
    ok, buf = recv_until(c, "473", timeout=2)
    ok_fail("+i: uninvited JOIN -> 473 ERR_INVITEONLYCHAN", ok, "Carol tries to join +i.", "Invite-only must block the uninvited.",
            "473.", buf, "Reject non-invited joins under +i.")
    send(a, "INVITE inv_c #i"); recv_until(c, "INVITE", timeout=2)
    send(c, "JOIN #i")
    ok, buf = recv_until(c, "JOIN #i", timeout=2)
    ok_fail("+i: invited user can JOIN", ok, "Carol joins after invite.", "An invite must actually admit.",
            ":inv_c!... JOIN #i", buf, "Record the invite and let that user through once.")
    safe_close(a, b, c)
    REPORT.end()


def sc_mode():
    REPORT.begin("mode", "MODE (i t k o l)", "Mandatory", "Every required channel mode, the query form, and the error numerics.")
    a = connect(); b = connect(); c = connect(); register(a, "mode_a"); register(b, "mode_b"); register(c, "mode_c")
    send(a, "JOIN #x"); send(b, "JOIN #x"); send(c, "JOIN #x"); time.sleep(0.3)
    send(a, "MODE #x +o mode_b")
    ok, buf = recv_until(b, "+o mode_b", timeout=2)
    ok_fail("+o grants operator", ok, "Alice ops Bob.", "Operator delegation.", ":mode_a!... MODE #x +o mode_b", buf, "Apply +o and broadcast.")
    send(a, "MODE #x -o mode_b")
    ok, buf = recv_until(b, "MODE", timeout=2)
    ok_fail("-o removes operator", ok, "Alice de-ops Bob.", "Operator removal.", ":mode_a!... MODE #x -o mode_b", buf, "Apply -o and broadcast.")

    send(a, "JOIN #keyc"); time.sleep(0.2); drain(a, 0.2)
    send(a, "MODE #keyc +k secret"); time.sleep(0.2)
    d = connect(); register(d, "mode_d"); send(d, "JOIN #keyc")
    ok, buf = recv_until(d, "475", timeout=2)
    ok_fail("+k: JOIN without key -> 475 ERR_BADCHANNELKEY", ok, "Dave joins a keyed channel with no key.",
            "+k gates entry on a key.", "475.", buf, "Check the key before admitting.")
    send(d, "JOIN #keyc secret")
    ok, buf = recv_until(d, "JOIN #keyc", timeout=2)
    ok_fail("+k: JOIN with the right key works", ok, "Dave joins with the key.", "Correct key admits.", "JOIN #keyc", buf, "Admit on match.")

    send(a, "JOIN #limc"); time.sleep(0.2); drain(a, 0.2)
    send(a, "MODE #limc +l 1"); time.sleep(0.2)
    e = connect(); register(e, "mode_e"); send(e, "JOIN #limc")
    ok, buf = recv_until(e, "471", timeout=2)
    ok_fail("+l: JOIN over the limit -> 471 ERR_CHANNELISFULL", ok, "Channel limited to 1; a second client joins.",
            "+l caps membership.", "471.", buf, "Reject when member count would exceed the limit.")
    send(a, "MODE #limc -l"); time.sleep(0.2)
    send(e, "JOIN #limc")
    ok, buf = recv_until(e, "JOIN #limc", timeout=2)
    ok_fail("-l: JOIN works after the limit is cleared", ok, "Same client retries after -l.", "Removing +l lifts the cap.",
            "JOIN #limc", buf, "Clear the stored limit on -l.")

    send(a, "MODE #x")
    ok, buf = recv_until(a, "324", timeout=2)
    subset("MODE query -> 324 RPL_CHANNELMODEIS", ok, "324" in buf, "Alice queries current modes.",
           "Clients read modes via 324.", "324 #x +...", buf, "Answer a paramless MODE with 324.")
    send(a, "MODE #x +t"); recv_until(a, "+t", 1); drain(a, 0.2)
    send(c, "MODE #x +i")
    ok, buf = recv_until(c, "482", timeout=2)
    ok_fail("MODE by non-op -> 482", ok, "Carol (non-op) sets a mode.", "Only ops change modes.", "482.", buf, "Gate MODE changes on op status.")
    send(a, "MODE #x +Z")
    ok, buf = recv_until(a, "472", timeout=2)
    subset("Unknown mode flag -> 472 ERR_UNKNOWNMODE", ok, ("472" in buf) or ("501" in buf) or (not buf.strip()),
           "An undefined mode letter.", "Unknown modes should be reported.", "472.", buf, "Ignoring it is tolerable; 472 is the RFC reply.")

    send(a, "JOIN #argc"); time.sleep(0.2); drain(a, 0.3)
    pol = PROFILE["missing_mode_arg"]
    for cmd, lbl in (("MODE #argc +k", "+k without key"), ("MODE #argc +o", "+o without nick"), ("MODE #argc +l", "+l without number")):
        send(a, cmd)
        ok, buf = recv_until(a, "461", timeout=2)
        specific = any(x in buf for x in ("467", "696", "472"))
        if pol == "461":
            ok_fail("%s -> 461 ERR_NEEDMOREPARAMS" % lbl, ok, "A mode that needs an argument, given none.",
                    "Your profile: missing_mode_arg=461.", "461.", buf, "Return 461, or set missing_mode_arg=specific/any.")
        elif pol == "specific":
            ok_fail("%s -> a specific numeric (467/696)" % lbl, specific or ok, "A mode that needs an argument, given none.",
                    "Your profile: missing_mode_arg=specific.", "467 ERR_KEYSET / 696 ERR_INVALIDMODEPARAM (461 also accepted).", buf,
                    "Return a defined numeric, or set missing_mode_arg=any.")
        else:
            subset("%s -> which numeric? (461 is the RFC one; 467/696 are more specific alternatives)" % lbl, ok, specific or (not buf.strip()),
                   "Send the mode letter with its argument missing (e.g. MODE #chan +k with no key).",
                   "Not judged by the subject. RFC 2812 answers 461 ERR_NEEDMOREPARAMS; some servers answer 467 ERR_KEYSET or 696 ERR_INVALIDMODEPARAM, which is more precise.",
                   "461 (RFC) - or 467/696.", buf,
                   "PASS = 461. NOTE = your server answered %s. Both are defined replies; set missing_mode_arg=specific to make this a PASS for your choice." % ("467/696" if specific else "nothing"))

    drain(a, 0.3)
    send(a, "MODE mode_a +i")
    ok, buf = recv_until(a, "502", timeout=1)
    if PROFILE["user_modes"] == "supported":
        ok_fail("MODE on a nickname (user mode) -> handled (221/501/502)", ("221" in buf) or ("502" in buf) or ("501" in buf),
                "Setting a user mode.", "Your profile: user_modes=supported.", "221/501/502.", buf, "Handle user modes, or set user_modes=none.")
    else:
        note("MODE on a nickname (a USER mode, e.g. MODE alice +i) - not a channel mode, not required by the subject",
             "Send MODE <own nick> +i.", "The subject only requires channel modes (i t k o l on #channels); user modes are outside its scope.",
             "any consistent handling (403 / 501 / ignore).", buf,
             "Your profile: user_modes=none, so this is informational. Alternative: implement 221/501 user modes.")
    safe_close(a, b, c, d, e)
    REPORT.end()


def sc_quit():
    REPORT.begin("quit", "QUIT & abrupt disconnect", "Mandatory", "Graceful QUIT broadcast, and that a hard drop still tells the peers.")
    a = connect(); b = connect(); register(a, "quit_a"); register(b, "quit_b")
    send(a, "JOIN #q"); send(b, "JOIN #q"); time.sleep(0.2)
    send(a, "QUIT :see ya")
    ok, buf = recv_until(b, "QUIT", timeout=2)
    ok_fail("QUIT is broadcast to channel members", ok, "Alice quits with a message.", "Members must learn of a departure.",
            ":quit_a!... QUIT :... see ya", buf, "Broadcast QUIT to every shared channel, once.")
    ok, abuf = recv_until(a, "ERROR", timeout=2)
    subset("The quitting client gets ERROR :Closing link before EOF", ok, True,
           "Read alice's own socket after her QUIT.",
           "RFC 2812 3.1.7: the server acknowledges QUIT with an ERROR message; irssi shows it as the disconnect reason.",
           "ERROR :Closing link ..., then EOF", abuf or "<nothing, then EOF>",
           "The ERROR line is queued and the fd closed in the same loop iteration, before poll() reports POLLOUT: "
           "wait for the pending output to be sent, then close.")
    safe_close(a)
    a2 = connect(); register(a2, "drop_a"); send(a2, "JOIN #q"); time.sleep(0.2); drain(b, 0.3)
    a2.close()
    ok, buf = recv_until(b, "QUIT", timeout=3)
    subset("Abrupt disconnect -> peers still get a QUIT", ok, True,
           "Alice's socket is closed with no QUIT.", "A dropped client must not linger in the member list.",
           ":drop_a!... QUIT :<reason>", buf,
           "Not broadcast: the member list goes stale for peers. Alternative: on EOF, broadcast a QUIT to the client's channels.")
    safe_close(b)
    REPORT.end()


def sc_ping_errors():
    REPORT.begin("ping", "PING/PONG & unknown commands", "Mandatory", "Liveness handshake and the catch-all error numerics.")
    c = connect(); register(c, "ping_c")
    send(c, "PING :token")
    ok, buf = recv_until(c, "PONG", timeout=2)
    ok_fail("PING -> PONG with the token", ok and "token" in buf, "Alice pings.", "Keepalive.", ":server PONG server :token", buf, "Answer PING with PONG echoing the token.")
    send(c, "PING")
    ok, buf = recv_until(c, "409", timeout=1)
    subset("PING with no token -> 409 ERR_NOORIGIN", ok, ("409" in buf) or (not buf.strip()), "Bare PING.", "No origin to echo.", "409.", buf, "Ignoring is tolerable; 409 is the RFC reply.")
    send(c, "NOTACOMMAND")
    ok, buf = recv_until(c, "421", timeout=2)
    ok_fail("Unknown command -> 421 ERR_UNKNOWNCOMMAND", ok, "Garbage command.", "Unknown verbs must be reported, not crash.", "421.", buf, "Return 421.")
    safe_close(c)
    REPORT.end()


def sc_operator_and_isolation():
    REPORT.begin("ops", "Operator model & channel isolation", "Mandatory", "Creator gets op, granted ops work, and channels don't leak into each other.")
    a = connect(); b = connect(); register(a, "op_a"); register(b, "op_b")
    send(a, "JOIN #o"); send(b, "JOIN #o"); time.sleep(0.2)
    send(b, "KICK #o op_a :haha")
    ok, buf = recv_until(b, "482", timeout=2)
    ok_fail("Channel creator holds op (non-op kick -> 482)", ok, "Bob tries to kick the creator.", "First joiner is operator.", "482.", buf, "Grant op to the channel creator.")
    send(a, "MODE #o +o op_b"); time.sleep(0.2)
    cc = connect(); register(cc, "op_c"); send(cc, "JOIN #o"); time.sleep(0.2)
    send(b, "KICK #o op_c :now i can")
    ok, buf = recv_until(cc, "KICK #o op_c", timeout=2)
    ok_fail("A newly-granted op can KICK", ok, "Bob (now op) kicks Carol.", "Delegated op must be real.", "KICK reaches Carol.", buf, "Honor granted ops.")
    a2 = connect(); b2 = connect(); register(a2, "iso_a"); register(b2, "iso_b")
    send(a2, "JOIN #one"); send(a2, "JOIN #two"); send(b2, "JOIN #one"); time.sleep(0.3)
    send(a2, "PRIVMSG #two :only two"); got = drain(b2, 0.6)
    ok_fail("Message in a channel you're not in never arrives", "only two" not in got, "Alice posts to #two; Bob is only in #one.",
            "Channels are isolated.", "nothing in Bob's stream", got, "Fan out strictly to the target channel's members.")
    safe_close(a, b, cc, a2, b2)
    REPORT.end()


def sc_casemapping():
    REPORT.begin("case", "Case-insensitivity", "Mandatory", "Nicks and channel names compare case-insensitively (rfc1459 / ascii).")
    a = connect(); register(a, "Case_A")
    b = connect(); LABELS[b] = "case_try"
    send(b, "PASS %s" % PASSWORD); send(b, "NICK case_a"); send(b, "USER c 0 * :c")
    ok, buf = recv_until(b, "433", timeout=2)
    a2 = connect(); register(a2, "chan_a"); b2 = connect(); register(b2, "chan_b")
    send(a2, "JOIN #Room"); time.sleep(0.2); drain(a2, 0.2)
    send(b2, "JOIN #room"); time.sleep(0.2)
    send(a2, "PRIVMSG #Room :same channel")
    ok2, buf2 = recv_until(b2, "same channel", timeout=2)
    if PROFILE["casemapping"] == "none":
        note("Nick/channel case folding", "Register 'Case_A' then 'case_a'; join #Room and #room.",
             "Your profile: casemapping=none (case-sensitive).", "433 and one shared channel if folding",
             buf + buf2, "Case-sensitive names are unusual on IRC. Alternative: fold case (rfc1459 or ascii) when comparing nicks and channels.")
    else:
        ok_fail("Nick differing only in case is a duplicate -> 433", ok or ("001" not in (buf + drain(b, 0.3))),
                "Register 'Case_A' then 'case_a'.", "IRC nicks are case-insensitive (your profile: %s)." % PROFILE["casemapping"],
                "433.", buf, "Fold case when comparing nicks.")
        ok_fail("#Room and #room are the same channel", ok2, "Two clients join with different case.",
                "Channel names are case-insensitive.", "Bob receives Alice's message.", buf2, "Fold case on channel keys.")
    safe_close(a, b, a2, b2)
    REPORT.end()


# ===========================================================================
# ROBUSTNESS scenarios
# ===========================================================================

def sc_framing():
    REPORT.begin("framing", "Line framing: fragmented, batched, split-in-time", "Robustness",
                 "The subject's core poll requirement: a command may arrive in pieces or several at once. "
                 "The parser must buffer correctly and never block.")
    c = connect(); LABELS[c] = "frag"
    send_raw(c, ("PASS %s\r\nN" % PASSWORD).encode()); time.sleep(0.15)
    send_raw(c, b"ICK frag\r\nUSER frag 0 * :f\r\n")
    ok, buf = recv_until(c, "001 frag", timeout=3)
    ok_fail("Command split across two packets still parses", ok, "PASS + 'N', then 'ICK frag...'.",
            "recv() boundaries must not split commands.", "001 frag.", buf, "Buffer per fd until you see \\n; never parse partial lines.")
    safe_close(c)
    c = connect(); LABELS[c] = "batch"
    send_raw(c, ("PASS %s\r\nNICK batch\r\nUSER batch 0 * :b\r\nJOIN #b\r\nPRIVMSG #b :hi\r\n" % PASSWORD).encode())
    ok, buf = recv_until(c, "JOIN #b", timeout=3)
    ok_fail("Several commands in one packet all run", ok and "001 batch" in buf, "Five commands in one write.",
            "One recv() can carry many lines.", "001 ... JOIN #b.", buf, "Loop over every complete line in the buffer.")
    safe_close(c)
    c = connect(); LABELS[c] = "split_time"
    send_raw(c, ("PASS %s\r\nNICK spli" % PASSWORD).encode()); time.sleep(1.2)
    send_raw(c, b"t\r\nUSER split 0 * :s\r\n")
    ok, buf = recv_until(c, "001 split", timeout=3)
    ok_fail("Half a command, a pause, then the rest (nc + Ctrl-D case)", ok, "'NICK spli' ... 1.2s ... 't'.",
            "The subject explicitly tests partial sends.", "001 split.", buf, "Keep the partial line across poll cycles; never busy-wait.")
    safe_close(c)
    # the exact evaluator recipe: (printf 'com'; sleep 2; printf 'man'; sleep 2; printf 'd\r\n') | nc
    c = connect(); register(c, "pieces"); drain(c, 0.2)
    LABELS[c] = "pieces"
    send_raw(c, b"PRI"); time.sleep(2.0)
    send_raw(c, b"VMSG pieces :arrived in th"); time.sleep(2.0)
    send_raw(c, b"ree pieces\r\n")
    ok, buf = recv_until(c, "arrived in three pieces", timeout=3)
    ok_fail("One command sent in 3 pieces, 2 s apart, executes once complete", ok,
            "'PRI' ... 2s ... 'VMSG pieces :arrived in th' ... 2s ... 'ree pieces\\r\\n' -- the printf/sleep/nc recipe.",
            "Nothing may run until the line is complete, and the server must stay responsive meanwhile.",
            "the echoed PRIVMSG only after the final piece.", buf,
            "Append to the per-fd buffer on every recv(); parse only complete lines.")
    safe_close(c)
    REPORT.end()


def sc_line_endings():
    REPORT.begin("eol", "Line endings: CRLF, bare LF, CR-CR-LF", "Robustness",
                 "RFC uses CRLF, but nc and many evaluators send a bare LF. Your profile says which you accept.")
    c = connect(); LABELS[c] = "lf"
    send_raw(c, ("PASS %s\nNICK lfuser\nUSER lfuser 0 * :l\n" % PASSWORD).encode())
    ok, buf = recv_until(c, "001 lfuser", timeout=2)
    if PROFILE["line_endings"] == "lf_ok":
        ok_fail("Bare LF line endings accepted", ok, "Register using only \\n.", "Your profile: line_endings=lf_ok.",
                "001 lfuser.", buf, "Accept \\n as well as \\r\\n, or set line_endings=crlf_only.")
    else:
        note("Bare LF line endings", "Register using only \\n.", "Your profile: line_endings=crlf_only (strict RFC).",
             "with crlf_only nothing happens until \\r\\n arrives", buf,
             "Strict CRLF is RFC-correct; plain nc on Linux sends \\n, so with crlf_only an evaluator typing into nc sees no reply. Alternative: accept both.")
    safe_close(c)
    c = connect(); LABELS[c] = "crlf"
    send_raw(c, ("PASS %s\r\nNICK crlfuser\r\nUSER crlfuser 0 * :c\r\n" % PASSWORD).encode())
    ok, buf = recv_until(c, "001 crlfuser", timeout=2)
    ok_fail("CRLF line endings accepted", ok, "Register using \\r\\n.", "CRLF is the RFC standard and must always work.", "001 crlfuser.", buf, "If this fails, line framing is broken.")
    safe_close(c)
    c = connect(); LABELS[c] = "crcrlf"
    send_raw(c, ("PASS %s\r\nNICK crcr\r\r\nUSER crcr 0 * :x\r\n" % PASSWORD).encode())
    got = recv_until(c, "001", timeout=2)[1] + drain(c, 0.3)
    subset("CR-CR-LF handled sanely (no crash, defined result)", "001 crcr" in got, ("432" in got) or ("001" in got) or (not got.strip()),
           "A stray extra \\r before \\n.", "printf '...\\r\\n' | nc -c can emit \\r\\r\\n.",
           "either the extra CR is stripped (001) or the nick is rejected (432) -- but no crash.", got,
           "Your server rejects the nick with the stray CR (defined, no crash). Alternative: strip trailing CRs before parsing.")
    safe_close(c)
    REPORT.end()


def sc_line_limit():
    mx = PROFILE["max_line"]; pol = PROFILE["overlong"]; strikes = PROFILE["overlong_strikes"]
    REPORT.begin("limit", "Line length limit (%d bytes)" % mx, "Robustness",
                 "RFC caps a message at 512 bytes incl. CRLF (510 of content). Your profile: max_line=%d, "
                 "overlong=%s, close after %d in a row." % (mx, pol, strikes))
    c = connect(); register(c, "lim")
    body_ok = "PRIVMSG lim :" + ("a" * (mx - len("PRIVMSG lim :")))
    send(c, body_ok)
    ok, buf = recv_until(c, "PRIVMSG lim :aaa", timeout=2)
    ok_fail("%d-byte line is accepted (echoed back)" % mx, ok, "A message exactly max_line bytes of content.",
            "The maximum legal length must work.", "echoed back to the sender.", buf,
            "If this is rejected, the limit is off by a byte or two (check whether you count CRLF).")
    drain(c, 0.3)
    body_over = "PRIVMSG lim :" + ("b" * (mx + 1 - len("PRIVMSG lim :")))
    send(c, body_over)
    ok417, buf = recv_until(c, "417", timeout=2)
    more = drain(c, 0.3); allgot = buf + more
    if pol == "reject":
        ok_fail("%d-byte line -> rejected (417 or close)" % (mx + 1), ok417 or closed(c, 0.2) or "Closing link" in allgot,
                "One byte over the limit.", "Your profile: overlong=reject.", "417 ERR_INPUTTOOLONG (or the link closed).", allgot,
                "Reject over-long input with 417, or set overlong=truncate/ignore.")
    elif pol == "truncate":
        ok_fail("%d-byte line -> truncated echo" % (mx + 1), "PRIVMSG lim :bbb" in allgot and "417" not in allgot,
                "One byte over the limit.", "Your profile: overlong=truncate.", "a truncated echo, no 417.", allgot,
                "Truncate to max_line, or set overlong=reject.")
    else:
        ok_fail("%d-byte line -> ignored (no crash)" % (mx + 1), "417" not in allgot and "bbb" not in allgot,
                "One byte over the limit.", "Your profile: overlong=ignore.", "nothing.", allgot, "Ignore silently, or set overlong=reject/truncate.")
    if strikes > 0:
        # fresh client so the earlier over-long line doesn't count; send each line and WAIT
        # for its reply so the lines arrive in separate recv() calls (coalescing would hide the rule)
        safe_close(c); c = connect(); register(c, "lim2"); drain(c, 0.3)
        per_line = []; dropped = False; saw_error_text = False
        for i in range(strikes):
            send(c, body_over)
            ok_r, r = recv_any(c, ["417", "Closing link"], timeout=1.5)
            r = r + drain(c, 0.3)
            if "Closing link" in r:
                saw_error_text = True
            if closed(c, 0.3):
                dropped = True
                per_line.append("line %d -> %s%s" % (i + 1, "417 then " if "417" in r else "", "link closed" + (" (ERROR text received)" if saw_error_text else " (bare EOF, no ERROR text)")))
                break
            per_line.append("line %d -> %s" % (i + 1, "417" if "417" in r else (r.strip()[:40] or "no reply")))
        if not dropped:
            time.sleep(1.0)
            dropped = closed(c, 0.5)
            if dropped:
                per_line.append("closed after a further 1 s")
        evidence = "; ".join(per_line)
        ok_fail("%d over-long lines in a row -> link closed" % strikes, dropped,
                "Send the over-long line %d times, waiting for each reply." % strikes,
                "Your profile: overlong_strikes=%d." % strikes, "the link closed after line %d." % strikes, evidence,
                "Count consecutive over-long lines per client and close past the limit -- or set overlong_strikes=0 if you don't do this.")
        if dropped and not saw_error_text:
            note("Close reason not delivered before the close", "The link was closed after the over-long lines.",
                 "Clients like irssi show the ERROR :Closing link text as the reason they were disconnected.",
                 "ERROR :Closing link ... then EOF", evidence,
                 "The socket closed with a bare EOF: the ERROR line was queued but not flushed before close. "
                 "Alternative: flush the write buffer for that fd right before closing it.")
    else:
        note("Over-long lines never close the link", "Repeat the over-long line.", "Your profile: overlong_strikes=0.",
             "kept open", "", "Alternative: drop a client after N consecutive over-long lines (cheap flood hardening).")
    safe_close(c)
    REPORT.end()


def sc_empty_and_weird():
    REPORT.begin("weird", "Empty lines, whitespace, odd input", "Robustness", "Blank lines and whitespace must be ignored, not answered with errors or crashes.")
    c = connect(); register(c, "weird"); drain(c, 0.2)
    for label, payload in (("empty line (CRLF)", b"\r\n"), ("empty line (bare LF)", b"\n"),
                           ("spaces only (CRLF)", b"   \r\n"), ("spaces only (bare LF)", b"   \n")):
        send_raw(c, payload); got = drain(c, 0.5)
        ok_fail("%s is ignored (no reply)" % label, "421" not in got and not got.strip(),
                "Send %r." % payload.decode(), "A line with no command must be skipped silently.",
                "silence.", got, "Skip lines that tokenise to nothing (after trimming spaces).")
    send_raw(c, b"\t\r\n"); got = drain(c, 0.5)
    subset("tab-only line: silence (tab trimmed) or 421 (tab is not a separator)", not got.strip(), "421" in got,
           "Send a line containing only a tab.", "RFC 2812 only knows the space as separator, so a tab is a (bogus) command name.",
           "silence or 421.", got, "Your server answers 421 ERR_UNKNOWNCOMMAND, like ergo does. Alternative design: trim tabs like spaces and stay silent.")
    send(c, "   PING :spaced"); got = drain(c, 0.6)
    subset("Leading spaces before a command handled sanely", "PONG" in got, ("421" in got) or (not got.strip()) or ("PONG" in got),
           "'   PING :spaced'.", "Some parsers choke on leading spaces.", "a PONG or a clean 421 -- not a crash.", got,
           "RFC lets you skip leading spaces; erroring is tolerable, crashing is not.")
    safe_close(c)
    REPORT.end()


def sc_recovery():
    REPORT.begin("recovery", "Slow reader: brief stall then recover", "Robustness",
                 "A client that stalls briefly then reads again must NOT be dropped and must get its buffered lines in order.")
    a = connect(); register(a, "rec_a")
    b = connect(rcvbuf=4096); LABELS[b] = "rec_b(slow)"; register(b, "rec_b")
    send(a, "JOIN #r"); send(b, "JOIN #r"); time.sleep(0.3); drain(a, 0.2); drain(b, 0.2)
    for i in range(200):
        send(a, "PRIVMSG #r :line%03d" % i)
    time.sleep(0.5)
    got = b""; end = time.time() + 3
    while time.time() < end:
        r, _, _ = select.select([b], [], [], 0.2)
        if b in r:
            try:
                chunk = b.recv(65536)
            except socket.error:
                break
            if not chunk:
                break
            got += chunk
    text = got.decode(errors="ignore")
    lines = [l for l in text.split("\r\n") if "PRIVMSG #r :line" in l]
    ordered = all("line%03d" % i in lines[i] for i in range(min(len(lines), 50)))
    ok_fail("Short stall then read: buffered lines arrive, in order, no drop", len(lines) >= 50 and ordered and not closed(a, 0.2),
            "Bob stops reading during a 200-line burst, then drains.", "The kernel absorbs a modest backlog; a brief stall is normal.",
            ">=50 complete lines, in order; Alice still connected.", "got %d lines, ordered=%s" % (len(lines), ordered),
            "If Bob is dropped here, slow-client handling is firing on a client that was fine.")
    REPORT.metric("Backlog lines delivered after recovery", str(len(lines)))
    safe_close(a, b)
    REPORT.end()


def sc_flood_drop():
    pol = PROFILE["slow_client"]; mb = PROFILE["flood_mb"]
    REPORT.begin("flood", "Stalled reader: responsiveness & slow-client policy", "Robustness",
                 "One member never reads while its channel is flooded. Required: the server keeps serving "
                 "everyone else. Your profile says what happens to the stalled client itself: slow_client=%s." % pol)
    a = connect(); register(a, "fl_a"); ctrl = connect(); register(ctrl, "fl_ctrl")
    b = connect(rcvbuf=PROFILE["rcvbuf"]); LABELS[b] = "fl_b(stalled)"; register(b, "fl_b")
    send(a, "JOIN #f"); send(b, "JOIN #f"); send(ctrl, "JOIN #f"); time.sleep(0.3); drain(a, 0.2); drain(ctrl, 0.2)
    base_rss = server_rss_mb()
    line = "PRIVMSG #f :" + ("y" * 400)
    target = int(mb * 1e6 / (len(line) + 2))
    dropped = False; worst_ping = 0.0; sent = 0; t_start = time.time()
    while time.time() - t_start < 25 and sent < target:
        for _ in range(50):
            send(a, line)
        sent += 50
        t1 = time.time(); send(ctrl, "PING :p%d" % sent); recv_until(ctrl, "PONG", timeout=2)
        worst_ping = max(worst_ping, (time.time() - t1) * 1000)
        got = drain(a, 0.02)
        if "QUIT" in got or closed(b, 0.01):
            dropped = True; break
    end_rss = server_rss_mb()
    queued_mb = sent * (len(line) + 2) / 1e6
    ok_fail("Server stays responsive to other clients while one is stalled", worst_ping < 500,
            "A control client is PINGed throughout while fl_b never reads.",
            "The subject requires the server to keep handling everyone; one stalled client must never stall the poll loop.",
            "control PING round-trip well under 500 ms.", "worst PING %.1f ms" % worst_ping,
            "A big spike means send() is blocking or the loop is stuck on the stalled fd.")
    rss_note = ("server RSS %d -> %d MB after ~%.1f MB queued" % (base_rss, end_rss, queued_mb)) if base_rss >= 0 else ("~%.1f MB queued" % queued_mb)
    if pol == "drop":
        ok_fail("Stalled client is dropped (slow_client=drop)", dropped, "fl_b never reads while #f is flooded.",
                "Your profile: drop a client whose sends keep failing.", "fl_b dropped (QUIT to the channel).", rss_note,
                "Count consecutive failed sends and close the socket past a limit, or change slow_client.")
    else:
        REPORT.record("Stalled client kept (slow_client=keep): %s" % (("never dropped; " + rss_note) if not dropped else "dropped"), INFO,
                      what="fl_b never reads while #f is flooded with ~%.1f MB." % queued_mb,
                      why="Design choice recorded for information: the server keeps a client that stops reading; its queue grows in server memory.",
                      expect="kept (keep policy)")
    REPORT.metric("Worst control-client PING during the flood", "%.1f ms" % worst_ping)
    REPORT.metric("Backlog queued to the stalled client", "~%.1f MB" % queued_mb)
    if base_rss >= 0:
        REPORT.metric("Server RSS (before -> after)", "%d -> %d MB" % (base_rss, end_rss))
    safe_close(a, ctrl, b)
    REPORT.end()


def human_bytes(n):
    return ("%dk" % (n // 1024)) if n < 1048576 else ("%.0f MB" % (n / 1048576.0))


def sc_nick_reuse():
    REPORT.begin("reuse", "Nick freed after disconnect; rapid reconnect", "Robustness",
                 "A dropped client's nick must be reusable at once, and the server must survive many quick connect/disconnect cycles.")
    a = connect(); register(a, "taken_nick"); safe_close(a); time.sleep(0.3)
    b = connect(); ok, buf = register(b, "taken_nick")
    ok_fail("A disconnected client's nick is free again", ok, "Register a nick, drop, re-register it.",
            "State from a gone client must be cleaned up.", "001.", buf, "Release the nick when the client is reaped.")
    safe_close(b)
    survived = True
    for i in range(40):
        try:
            s = connect(); send(s, "PASS %s" % PASSWORD); send(s, "NICK churn%d" % i); send(s, "USER c 0 * :c"); send(s, "JOIN #churn")
            time.sleep(0.01); s.close()
        except OSError:
            survived = False; break
    time.sleep(0.5)
    probe = connect(); ok, buf = register(probe, "after_churn")
    ok_fail("Server still healthy after 40 rapid connect/disconnect cycles", survived and ok, "40 clients connect, join, and drop fast.",
            "Reaping must not leak fds or corrupt state.", "a fresh client still registers.", buf, "Look for fd leaks or use-after-free in the reap path.")
    safe_close(probe)
    REPORT.end()


def sc_load():
    n = PROFILE["clients"]
    REPORT.begin("load", "Many clients in one channel (%d)" % n, "Robustness",
                 "N clients register, join one channel, and a broadcast reaches the last one; broadcast latency measured.")
    socks = []
    try:
        registered = 0
        for i in range(n):
            s = connect(); ok, _ = register(s, "load%02d" % i)
            if ok:
                registered += 1; socks.append(s)
            else:
                s.close()
        ok_fail("All %d clients register" % n, registered == n, "Open N connections and register each.", "Many fds at once.",
                "%d/%d." % (n, n), "%d/%d" % (registered, n), "If some fail: fd limit (ulimit -n) or an accept() cap.")
        if len(socks) < 2:
            skip("Broadcast with many clients", "not enough clients registered"); REPORT.end(); return
        for s in socks:
            send(s, "JOIN #load")
        time.sleep(0.5)
        for s in socks:
            drain(s, 0.05)
        t1 = time.time(); send(socks[0], "PRIVMSG #load :broadcast ping")
        ok, buf = recv_until(socks[-1], "broadcast ping", timeout=3); dt = (time.time() - t1) * 1000
        ok_fail("Broadcast reaches the last of %d members" % len(socks), ok, "First posts; last should receive.", "Fan-out at scale.",
                "the message in the last client's stream.", buf, "Check the member iteration on send.")
        REPORT.metric("Broadcast latency to %d members" % len(socks), "%.1f ms" % dt)
    finally:
        safe_close(*socks)
    REPORT.end()


# ===========================================================================
# LOAD: slow-reader load test (N clients reading 1 byte at a time)
# ===========================================================================

def sc_slow_reader_load():
    n = PROFILE["slow_clients"]; pol = PROFILE["slow_client"]
    REPORT.begin("slowload", "Slow-reader load: %d clients reading 1 byte at a time" % n, "Load",
                 "All clients join one channel and keep talking while reading only 1 byte every 10 ms "
                 "(slow-loris). Shows how the server copes: responsiveness to a normal client, memory, "
                 "what it does with the slow ones (your profile: slow_client=%s), and data integrity." % pol)
    ctrl = connect(); register(ctrl, "sl_ctrl"); send(ctrl, "JOIN #sl"); time.sleep(0.2); drain(ctrl, 0.2)
    clients = []
    for i in range(n):
        try:
            s = connect(rcvbuf=PROFILE["rcvbuf"])
            LABELS[s] = "sl%02d" % i
            send(s, "PASS %s" % PASSWORD); send(s, "NICK sl%02d" % i); send(s, "USER s 0 * :s"); send(s, "JOIN #sl")
            clients.append([s, b"", False])   # sock, received bytes, dropped
        except OSError:
            break
    time.sleep(0.6)
    # drain the setup burst normally first
    for c in clients:
        try:
            while True:
                r, _, _ = select.select([c[0]], [], [], 0.05)
                if not r:
                    break
                d = c[0].recv(65536)
                if not d:
                    c[2] = True; break
        except OSError:
            pass
    base_rss = server_rss_mb()
    text = "x" * 256
    first_drop = None; rounds = 0; worst_ping = 0.0; t0 = time.time()
    while rounds < 40 and time.time() - t0 < 25:
        rounds += 1
        for i, c in enumerate(clients):
            if c[2]:
                continue
            try:
                c[0].sendall(("PRIVMSG #sl :r%02d c%02d %s\r\n" % (rounds, i, text)).encode())
            except OSError:
                c[2] = True
        # everyone reads ONE byte
        t_round = time.time()
        while time.time() - t_round < 0.2:
            for c in clients:
                if c[2]:
                    continue
                try:
                    r, _, _ = select.select([c[0]], [], [], 0)
                    if r:
                        d = c[0].recv(1)
                        if not d:
                            c[2] = True
                            if first_drop is None:
                                first_drop = rounds
                        else:
                            c[1] += d
                except (BlockingIOError, socket.error):
                    pass
            time.sleep(0.01)
        t1 = time.time(); send(ctrl, "PING :sl%d" % rounds); okp, _ = recv_until(ctrl, "PONG", timeout=3)
        worst_ping = max(worst_ping, (time.time() - t1) * 1000)
        alive = sum(1 for c in clients if not c[2])
        if first_drop is not None and rounds >= first_drop + 5:
            break
        if alive == 0:
            break
    end_rss = server_rss_mb()
    dropped = sum(1 for c in clients if c[2])
    # integrity of what the slow clients did receive (1 byte at a time)
    total_lines = 0; malformed = 0
    for c in clients:
        parts = c[1].split(b"\r\n")
        for p in parts[:-1]:
            if p:
                total_lines += 1
                if not (p.startswith(b":") and (b" PRIVMSG " in p or b" JOIN " in p or b" QUIT " in p or b" 3" in p[:12] or b" 4" in p[:12])):
                    malformed += 1
    ok_fail("Server stays responsive to a normal client under slow-reader load", worst_ping < 500,
            "A control client is PINGed every round.", "The subject requires everyone else to keep being served.",
            "PING round-trip well under 500 ms.", "worst %.1f ms over %d rounds" % (worst_ping, rounds),
            "A spike means the loop is blocked on slow fds.")
    ok_fail("Data delivered to slow readers is intact (complete lines, no corruption)", malformed == 0,
            "Reassemble each slow client's byte stream.", "Partial writes must never interleave or corrupt lines.",
            "0 malformed lines.", "%d lines, %d malformed" % (total_lines, malformed), "Check partial-send handling: keep the unsent tail and resume from it.")
    if pol == "keep":
        REPORT.record("Slow readers kept (slow_client=keep): %d/%d still connected after %d rounds; first drop: %s; RSS %d -> %d MB"
                      % (n - dropped, n, rounds, first_drop or "none", base_rss, end_rss), INFO,
                      what="%d clients read 1 byte per 10 ms while the channel is busy." % n,
                      why="Design choice recorded for information: slow clients are kept, their backlog accumulates server-side.",
                      expect="kept (keep policy)")
    else:
        ok_fail("Slow readers get dropped by your policy (slow_client=%s)" % pol, dropped > 0,
                "%d clients read 1 byte per 10 ms while the channel is busy." % n, "Your profile drops slow clients when sends fail.",
                "at least some slow clients dropped.", "dropped %d/%d, first at round %s" % (dropped, n, first_drop),
                "If none are dropped, the drop rule isn't firing for a never-reading client.")
    REPORT.metric("Rounds / first drop", "%d / %s" % (rounds, first_drop or "none"))
    REPORT.metric("Slow clients dropped", "%d / %d" % (dropped, n))
    REPORT.metric("Worst control PING", "%.1f ms" % worst_ping)
    if base_rss >= 0:
        REPORT.metric("Server RSS (before -> after)", "%d -> %d MB" % (base_rss, end_rss))
    safe_close(ctrl, *[c[0] for c in clients])
    REPORT.end()


# ===========================================================================
# BONUS scenarios
# ===========================================================================

def sc_bot_commands():
    bot = PROFILE["bot_nick"]
    REPORT.begin("bot", "Bonus 1 - The bot answers commands (no file involved)", "Bonus",
                 "A client sends PRIVMSG <bot> :!version / !whoami / !help / a bogus command / CTCP VERSION / CTCP PING "
                 "and the bot must reply. Nothing about files here. Skipped if no bot answers (mandatory binary).")
    c = connect(); register(c, "bot_u"); drain(c, 0.3)
    send(c, "PRIVMSG %s :!version" % bot)
    ok, buf = recv_any(c, ["I am", bot], timeout=3)
    if not ok and ("401" in buf or not buf.strip()):
        skip("Bot present", "No bot answered PRIVMSG %s :!version (mandatory binary, or bot_nick differs -- set bot_nick=...)." % bot)
        safe_close(c); REPORT.end(); return
    ok_fail("!version -> bot identifies itself", ok, "Ask the bot its version.", "The bot must respond to commands.", "a PRIVMSG back naming the bot.", buf, "Route PRIVMSG to the bot nick into the bot handler.")
    send(c, "PRIVMSG %s :!whoami" % bot)
    ok, buf = recv_until(c, "You are bot_u", timeout=3)
    ok_fail("!whoami -> bot echoes the caller", ok, "Ask who am I.", "Bot reads the caller's identity.", "You are bot_u.", buf, "Return the caller nick.")
    send(c, "PRIVMSG %s :!help" % bot)
    ok, buf = recv_any(c, ["!send", "help"], timeout=3)
    ok_fail("!help -> bot lists commands", ok, "Ask for help.", "Discoverability.", "a help line.", buf, "Return the help text.")
    send(c, "PRIVMSG %s :!bogus" % bot)
    ok, buf = recv_any(c, ["Unknown", "unknown"], timeout=3)
    ok_fail("Unknown bot command -> 'Unknown command'", ok, "A made-up command.", "Graceful handling of nonsense.", "an 'Unknown command' reply.", buf, "Add the fallback branch.")
    send(c, "PRIVMSG %s :\x01VERSION\x01" % bot)
    ok, buf = recv_any(c, ["\x01VERSION", bot], timeout=3)
    subset("CTCP VERSION -> bot replies", ok, True, "Send CTCP VERSION.", "Real clients probe with CTCP.", "a CTCP VERSION reply.", buf.replace("\x01", "<SOH>"), "Optional polish.")
    send(c, "PRIVMSG %s :\x01PING 12345\x01" % bot)
    ok, buf = recv_any(c, ["PING 12345"], timeout=3)
    subset("CTCP PING -> bot echoes the token", ok, True, "Send CTCP PING 12345.", "CTCP ping round-trip.", "PING 12345 echoed.", buf.replace("\x01", "<SOH>"), "Optional polish.")
    safe_close(c)
    REPORT.end()


def sc_bot_dcc_offer():
    bot = PROFILE["bot_nick"]
    REPORT.begin("dcc_offer", "Bonus 2 - File OFFER through the bot (!send / DCC SEND / !reject / !accept) - no bytes moved yet", "Bonus",
                 "An 'offer' is the bot telling the receiver that a file is available: after alice sends "
                 "PRIVMSG <bot> :!send bob <file>, the bot answers alice 'offer sent' and sends bob a DCC SEND <file> <ip> <port> <size> "
                 "line. The file is NOT transferred here; bob only replies !reject (frees the slot) or !accept (the bot opens "
                 "its listener and waits). This scenario checks that dialogue, and that a second offer while one is active is refused.")
    a = connect(); b = connect(); register(a, "dcc_a"); register(b, "dcc_b"); drain(a, 0.2); drain(b, 0.2)
    fname = ensure_sample_file()
    if fname is None:
        skip("DCC offer", "could not create files/<sample> next to the server binary"); safe_close(a, b); REPORT.end(); return
    send(a, "PRIVMSG %s :!send dcc_b %s" % (bot, fname))
    oka, bufa = recv_any(a, ["offer sent", "busy", "not found", "401"], timeout=3)
    if not oka or "401" in bufa:
        skip("DCC offer", "bot did not accept !send (mandatory binary or bot off)."); safe_close(a, b); REPORT.end(); return
    ok_fail("!send -> sender told the offer was sent", "offer sent" in bufa, "Alice offers a file to Bob.", "The offer path must acknowledge.", "'offer sent to dcc_b'.", bufa, "Acknowledge a successful offer.")
    ok, bufb = recv_any(b, ["DCC SEND"], timeout=3)
    ok_fail("Target receives a DCC SEND offer line", ok, "Bob should get the DCC SEND.", "The offer must reach the target with ip/port/size.", "DCC SEND %s <ip> <port> <size>" % fname, bufb.replace("\x01", "<SOH>"), "Deliver the DCC SEND CTCP to the target.")
    send(b, "PRIVMSG %s :!reject" % bot)
    ok, buf = recv_any(b, ["rejected", "reject"], timeout=3)
    ok_fail("!reject frees the slot", ok, "Bob rejects.", "A rejected offer must release resources.", "'transfer rejected'.", buf, "Close the offer on reject.")
    send(a, "PRIVMSG %s :!send dcc_b %s" % (bot, fname)); recv_any(a, ["offer sent"], timeout=3); recv_any(b, ["DCC SEND"], timeout=3)
    send(b, "PRIVMSG %s :!accept" % bot)
    ok, buf = recv_any(b, ["accepted", "accept"], timeout=3)
    ok_fail("!accept on a fresh offer works", ok, "Bob accepts the re-sent offer.", "Accept must move the transfer to active.", "'transfer accepted'.", buf, "Mark the offer accepted.")
    send(a, "PRIVMSG %s :!send dcc_b %s" % (bot, fname))
    ok, buf = recv_any(a, ["busy", "offer sent"], timeout=3)
    subset("A second offer while one is active -> 'bot busy'", "busy" in buf, True, "Alice offers again mid-transfer.", "A single-slot bot must refuse concurrent offers.", "'bot busy'.", buf, "A multi-slot bot is an equally valid design.")
    safe_close(a, b)
    REPORT.end()


def sc_dcc_transfer():
    bot = PROFILE["bot_nick"]
    REPORT.begin("dcc_xfer", "Bonus 3 - The actual FILE TRANSFER: receiver connects to the DCC port and gets the bytes", "Bonus",
                 "Same offer as Bonus 2, then the receiver does what a real client does after !accept: it connects to the "
                 "ip:port from the DCC SEND line and reads until EOF. Passes only if the byte count equals the file size. "
                 "This is the step that depends on the server driving the transfer socket from its poll loop.")
    fname = ensure_sample_file()
    if fname is None:
        skip("DCC transfer", "could not create the sample file"); REPORT.end(); return
    root = REPORT.server_cwd or os.getcwd()
    try:
        size_expected = os.path.getsize(os.path.join(root, "files", fname))
    except OSError:
        size_expected = 0
    a = connect(); b = connect(); register(a, "xfer_a"); register(b, "xfer_b"); drain(a, 0.3); drain(b, 0.3)
    send(a, "PRIVMSG %s :!send xfer_b %s" % (bot, fname))
    oka, ba = recv_any(a, ["offer sent", "busy", "not found", "401"], timeout=4)
    if ("busy" in ba) or ("not found" in ba) or ("401" in ba) or not oka:
        subset("Real DCC file transfer completes", False, True, "Offer a file to a second client, accept, receive.",
               "Needs a free transfer slot and a working listener.", "the file's bytes over TCP.", ba.replace("\x01", "<SOH>"),
               "Slot busy / offer refused in this run -- the offer-lifecycle scenario already exercised the bot.")
        safe_close(a, b); REPORT.end(); return
    okb, bb = recv_any(b, ["DCC SEND"], timeout=4)
    if not okb:
        REPORT.record("Receiver gets the DCC SEND offer", FAIL, what="The target should receive a DCC SEND line.", why="Needed to open the data connection.",
                      expect="DCC SEND <file> <ip> <port> <size>", got=bb.replace("\x01", "<SOH>"), fix="Deliver the offer to the target's fd.")
        safe_close(a, b); REPORT.end(); return
    m = re.search(r"DCC SEND \S+ (\S+) (\d+) (\d+)", bb)
    if not m:
        REPORT.record("Parse DCC ip/port/size from the offer", FAIL, what="Read ip/port/size out of the DCC SEND line.", why="The receiver needs them.",
                      expect="DCC SEND <file> <ip> <port> <size>", got=bb.replace("\x01", "<SOH>"), fix="Check the DCC SEND field layout.")
        safe_close(a, b); REPORT.end(); return
    ip, port, size = m.group(1), int(m.group(2)), int(m.group(3))
    if not re.match(r"^\d+\.\d+\.\d+\.\d+$", ip):
        try:
            n = int(ip); ip = "%d.%d.%d.%d" % ((n >> 24) & 255, (n >> 16) & 255, (n >> 8) & 255, n & 255)
        except ValueError:
            ip = REPORT.args.host
    REPORT.record("Parse DCC ip/port/size from the offer", PASS, what="Read ip/port/size from the DCC SEND line.", why="Needed to open the data connection.",
                  expect="ip, port, size", got="ip=%s port=%d size=%d" % (ip, port, size))
    send(b, "PRIVMSG %s :!accept" % bot); time.sleep(0.3)
    data = b""; err = None
    try:
        ds = socket.create_connection((ip, port), timeout=5); ds.setblocking(False)
        end = time.time() + 8
        while time.time() < end:
            try:
                # a normal keepalive from the sender (irssi sends "PING LAG<ms>"); it also wakes a poll
                # loop whose transfer socket is polled only once per iteration
                send(a, "PING LAG%d" % int(time.time() * 1000))
            except OSError:
                pass
            r, _, _ = select.select([ds], [], [], 0.25)
            if ds in r:
                try:
                    chunk = ds.recv(8192)
                except socket.error:
                    break
                if not chunk:
                    break
                data += chunk
                if size and len(data) >= size:
                    break
        ds.close()
    except OSError as e:
        err = e
    exp = size or size_expected
    if err is not None:
        subset("Real DCC file transfer completes", False, True, "Connect to the listener and read to EOF.", "This is the actual file transfer.",
               "%d bytes" % exp, "connection error: %s" % err, "Could not open the DCC data socket in this environment; the offer path is verified above.")
    else:
        ok_fail("Real DCC file transfer completes (byte count matches)", exp > 0 and len(data) == exp, "Connect to the listener, read to EOF.",
                "The transfer must deliver the whole file.", "%d bytes" % exp, "received %d bytes" % len(data),
                "A short read means the server never accepted on its DCC listener or never sent the chunks: the transfer "
                "socket is not being serviced by the poll loop (if the listener/data fds are only polled once per loop iteration, "
                "the loop must actually iterate; if they are polled with a separate poll() that was removed, nothing drives them).")
        REPORT.metric("DCC bytes received", "%d / %d" % (len(data), exp))
    safe_close(a, b)
    REPORT.end()


def sc_dcc_relay():
    REPORT.begin("dcc_relay", "Bonus 4 - DCC lines between two ordinary clients pass through the server untouched (no bot)", "Bonus",
                 "Two normal users exchange a PRIVMSG whose text is a CTCP DCC SEND/CHAT line (what irssi sends for /dcc). "
                 "The server only has to relay it byte-for-byte; no bot, no file, no listener involved.")
    a = connect(); b = connect(); register(a, "rel_a"); register(b, "rel_b")
    send(a, "PRIVMSG rel_b :\x01DCC SEND hello.txt 2130706433 9000 12\x01")
    ok, buf = recv_until(b, "DCC SEND hello.txt", timeout=2)
    ok_fail("DCC SEND relayed verbatim through PRIVMSG", ok, "Alice sends a DCC SEND CTCP to Bob.", "The server must not mangle CTCP payloads.",
            "the exact DCC SEND line at Bob.", buf.replace("\x01", "<SOH>"), "Relay the trailing parameter byte-for-byte.")
    send(a, "PRIVMSG rel_b :\x01DCC CHAT chat 2130706433 9001\x01")
    ok, buf = recv_until(b, "DCC CHAT chat", timeout=2)
    ok_fail("DCC CHAT relayed verbatim through PRIVMSG", ok, "Alice sends a DCC CHAT CTCP.", "Same passthrough requirement.",
            "the exact DCC CHAT line at Bob.", buf.replace("\x01", "<SOH>"), "Same relay path as DCC SEND.")
    safe_close(a, b)
    REPORT.end()



def sc_bonus_poll():
    REPORT.begin("bonus_poll", "Bonus 0 - One poll() in the bonus build", "Bonus",
                 "The one-poll() rule is a subject rule for the whole program; the bonus is only graded if the "
                 "mandatory part is perfect, and evaluators read the code. Counts poll( sites INCLUDING #ifdef BONUS code.")
    root = os.path.dirname(os.path.abspath(REPORT.args.bonus_binary))
    files = _source_files(root)
    if not files:
        skip("Bonus source scan", "no sources under %s" % root); REPORT.end(); return
    polls = _find_calls(files, r"(?<![\w_])poll\s*\(", root)
    ok_fail("Exactly one poll() call site in the bonus build", len(polls) == 1,
            "Count poll( call sites with bonus code included.",
            "\"Only 1 poll() (or equivalent) can be used for handling all these operations\" -- a separate poll() for a "
            "file-transfer socket is a second one, and an evaluator grepping for poll( will see it.",
            "1 call site.", "poll() at: %s" % ", ".join(polls),
            "Register the transfer listener/data fds in the main pollfd set (POLLIN on the listener, POLLOUT on the data fd) "
            "and handle them in the same loop, instead of a second poll() call.")
    REPORT.end()

_SAMPLE_NAME = "tester_sample.txt"
_SAMPLE_CREATED = None


def ensure_sample_file():
    global _SAMPLE_CREATED
    root = REPORT.server_cwd or os.getcwd()
    d = os.path.join(root, "files")
    try:
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, _SAMPLE_NAME)
        if not os.path.exists(p):
            with open(p, "w") as f:
                f.write("ft_irc DCC transfer test payload.\n" * 32)
            _SAMPLE_CREATED = p
        return _SAMPLE_NAME
    except OSError:
        return None


def cleanup_sample_file():
    if _SAMPLE_CREATED and os.path.exists(_SAMPLE_CREATED):
        try:
            os.remove(_SAMPLE_CREATED)
        except OSError:
            pass


def capture_bot_dialog(tag):
    lines = []; bot = PROFILE["bot_nick"]
    c = connect(); LABELS[c] = "probe"
    for s in ("PASS %s" % PASSWORD, "NICK probe", "USER probe 0 * :p"):
        send(c, s)
    recv_until(c, "001 probe", timeout=3)
    for cmd, needles in (("!version", ["I am", "401", "No such"]), ("!help", ["!send", "help", "401", "No such"])):
        lines.append(">> PRIVMSG %s :%s" % (bot, cmd)); send(c, "PRIVMSG %s :%s" % (bot, cmd))
        _, b1 = recv_any(c, needles, timeout=3)
        for l in strip_ansi(b1).replace("\r", "").split("\n"):
            if l:
                lines.append("<< " + l)
    safe_close(c)
    return "=== %s ===\n(target nick: %s)\n" % (tag, bot) + "\n".join(lines) + "\n"


# ===========================================================================
# Suites
# ===========================================================================

MANDATORY = [sc_single_poll, sc_authentication, sc_nick, sc_join, sc_part, sc_topic, sc_privmsg, sc_kick, sc_invite,
             sc_mode, sc_quit, sc_ping_errors, sc_operator_and_isolation, sc_casemapping]
ROBUSTNESS = [sc_framing, sc_line_endings, sc_line_limit, sc_empty_and_weird, sc_recovery, sc_flood_drop,
              sc_nick_reuse, sc_load]
LOAD = [sc_slow_reader_load]
BONUS_MAIN = [sc_bonus_poll, sc_bot_commands, sc_bot_dcc_offer, sc_dcc_relay]
BONUS_XFER = [sc_dcc_transfer]

TITLES = {
    "sc_single_poll": ("One poll()", "Mandatory"), "sc_authentication": ("Authentication", "Mandatory"), "sc_nick": ("NICK", "Mandatory"), "sc_join": ("JOIN", "Mandatory"),
    "sc_part": ("PART", "Mandatory"), "sc_topic": ("TOPIC", "Mandatory"), "sc_privmsg": ("PRIVMSG / NOTICE", "Mandatory"),
    "sc_kick": ("KICK", "Mandatory"), "sc_invite": ("INVITE", "Mandatory"), "sc_mode": ("MODE", "Mandatory"),
    "sc_quit": ("QUIT", "Mandatory"), "sc_ping_errors": ("PING / errors", "Mandatory"),
    "sc_operator_and_isolation": ("Operators & isolation", "Mandatory"), "sc_casemapping": ("Case-insensitivity", "Mandatory"),
    "sc_framing": ("Line framing", "Robustness"), "sc_line_endings": ("Line endings", "Robustness"),
    "sc_line_limit": ("Line length limit", "Robustness"), "sc_empty_and_weird": ("Odd input", "Robustness"),
    "sc_recovery": ("Slow reader recovers", "Robustness"), "sc_flood_drop": ("Stalled reader", "Robustness"),
    "sc_nick_reuse": ("Nick reuse / churn", "Robustness"), "sc_load": ("Many clients", "Robustness"),
    "sc_slow_reader_load": ("Slow-reader load", "Load"),
    "sc_bonus_poll": ("Bonus 0 one poll()", "Bonus"), "sc_bot_commands": ("Bonus 1 bot commands", "Bonus"), "sc_bot_dcc_offer": ("Bonus 2 file offer", "Bonus"),
    "sc_dcc_relay": ("Bonus 4 DCC relay", "Bonus"), "sc_dcc_transfer": ("Bonus 3 file transfer", "Bonus"),
}


# ===========================================================================
# Server management per phase
# ===========================================================================

def start_phase_server(binary):
    args = REPORT.args
    if args.no_start:
        if not can_connect(args.host, args.port):
            print("%s[ERROR]%s cannot reach %s:%d and --no-start was given." % (RED, RESET, args.host, args.port))
            raise SystemExit(1)
        REPORT.server = None; REPORT.server_cwd = os.getcwd()
        print("%sUsing external server at %s:%d%s" % (GREEN, args.host, args.port, RESET))
        return None
    if not os.path.isfile(binary):
        print("%s[skip phase]%s binary %r not found." % (YELLOW, RESET, binary))
        return None
    cwd = os.path.dirname(os.path.abspath(binary)) or "."
    logpath = os.path.join(args.outdir, "server_%s.log" % os.path.basename(binary))
    # Always the configured port (6667 unless --port): the server is restarted
    # on it between phases; SO_REUSEADDR makes the rebind immediate. If someone
    # else holds the port we say who and stop instead of silently moving.
    if can_connect(args.host, args.port):
        print("%s[ERROR]%s port %d is already in use by another program:" % (RED, RESET, args.port))
        print("   " + (port_owner(args.port) or "(lsof/ss not available to name it)"))
        print("   stop it, or run with --port <other> (make PORT=<other>)")
        raise SystemExit(1)
    deadline = time.time() + 8; tail = []
    while time.time() < deadline:
        srv = ServerProc(os.path.abspath(binary), args.port, args.password, logpath, cwd=cwd)
        ready = False; t_end = time.time() + 6
        while time.time() < t_end:
            if not srv.alive():
                break
            if can_connect(args.host, args.port):
                ready = True; break
            time.sleep(0.1)
        if ready:
            REPORT.server = srv; REPORT.server_cwd = cwd
            print("%sStarted %s (pid %d) on port %d%s" % (GREEN, os.path.basename(binary), srv.proc.pid, args.port, RESET))
            return srv
        tail = srv.tail(6); srv.stop(); time.sleep(0.5)     # bind not possible yet: retry
    print("%s[ERROR]%s %s did not come up on port %d:" % (RED, RESET, binary, args.port))
    for l in tail:
        print("   " + l)
    raise SystemExit(1)


def port_owner(port):
    """'pid 123 ircserv' for whatever listens on the port, via lsof (mac/linux) or ss."""
    try:
        out = subprocess.check_output(["lsof", "-nP", "-iTCP:%d" % port, "-sTCP:LISTEN"], stderr=subprocess.DEVNULL).decode().splitlines()
        if len(out) > 1:
            f = out[1].split()
            return "pid %s %s   (kill %s)" % (f[1], f[0], f[1])
    except Exception:
        pass
    try:
        out = subprocess.check_output(["ss", "-ltnp"], stderr=subprocess.DEVNULL).decode()
        for line in out.splitlines():
            if ":%d " % port in line:
                return line.strip()
    except Exception:
        pass
    return ""


def stop_phase_server(srv):
    if srv is not None:
        srv.stop(); time.sleep(0.4)
    REPORT.server = None


def run_suite(scenarios):
    for sc in scenarios:
        try:
            sc()
        except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError, OSError) as e:
            if REPORT.current is None:
                REPORT.begin(sc.__name__, sc.__name__, "?", "")
            REPORT.record(sc.__name__, FAIL, what="Scenario crashed.", got="%s: %s" % (type(e).__name__, e),
                          fix="The server may have died; check its log slice for this scenario.")
            REPORT.end()


def run_everything(mode, extra_files):
    """The whole plan, as the terminal mode runs it; also used by --live in a worker thread."""
    args = REPORT.args
    if mode in ("mandatory", "robustness", "load", "all"):
        srv = start_phase_server(args.binary)
        if mode == "mandatory":
            run_suite(MANDATORY)
        elif mode == "robustness":
            run_suite(ROBUSTNESS)
        elif mode == "load":
            run_suite(LOAD)
        else:
            run_suite(MANDATORY); run_suite(ROBUSTNESS); run_suite(LOAD)
        stop_phase_server(srv)
    if mode in ("bonus", "all"):
        srv = start_phase_server(args.bonus_binary)
        if REPORT.server is not None or args.no_start:
            run_suite(BONUS_MAIN)
        stop_phase_server(srv)
        if args.no_start:
            run_suite(BONUS_XFER)
        else:
            srv = start_phase_server(args.bonus_binary)
            if REPORT.server is not None:
                run_suite(BONUS_XFER)
            stop_phase_server(srv)
    if mode in ("compare", "all"):
        with_txt = no_txt = None
        srv = start_phase_server(args.bonus_binary)
        if REPORT.server is not None or args.no_start:
            with_txt = capture_bot_dialog("WITH BOT (bonus binary)")
        stop_phase_server(srv)
        srv = start_phase_server(args.binary)
        if REPORT.server is not None or args.no_start:
            no_txt = capture_bot_dialog("NO BOT (mandatory binary)")
        stop_phase_server(srv)
        if with_txt is not None:
            open(os.path.join(args.outdir, "bot_with.log"), "w").write(with_txt)
            extra_files.append(("artifacts/bot_with.log", "bot dialogue on the bonus binary"))
        if no_txt is not None:
            open(os.path.join(args.outdir, "bot_without.log"), "w").write(no_txt)
            extra_files.append(("artifacts/bot_without.log", "same dialogue on the mandatory binary (bot absent)"))
        REPORT.begin("compare", "With-bot vs no-bot capture", "Compare",
                     "The same bot dialogue against both binaries, saved to files so the difference is on paper.")
        if with_txt and no_txt:
            ok_fail("Bonus binary: the bot answers !version/!help", "I am" in with_txt, "Run the dialogue on the bonus binary.",
                    "Proves the bot is wired in.", "an 'I am <bot>' style reply.", with_txt, "Route PRIVMSG to the bot nick into the bot.")
            ok_fail("Mandatory binary: no bot (nick just 401s / silent)", ("401" in no_txt) or ("I am" not in no_txt),
                    "Same dialogue on the mandatory binary.", "The bot is a bonus-only feature.", "401 / no bot reply.", no_txt, "")
            REPORT.record("Captured both transcripts to files", INFO, what="bot_with.log and bot_without.log written for a side-by-side diff.")
        else:
            skip("With/no-bot capture", "one of the binaries was unavailable.")
        REPORT.end()


# ===========================================================================
# HTML report
# ===========================================================================

def esc(s):
    return html.escape(s or "")


def colorize_client_line(direction, line):
    cls = {"<<": "rx", ">>": "tx", "--": "note"}.get(direction, "note")
    arrow = {"<<": "&larr;", ">>": "&rarr;", "--": "&middot;"}.get(direction, "&middot;")
    return '<div class="ln %s"><span class="arw">%s</span>%s</div>' % (cls, arrow, esc(line.replace("\x01", "<SOH>")))


def colorize_server_line(text):
    low = text.lower(); cls = "srv"
    if "recvmsg" in low or "from fd" in low:
        cls = "srv rx"
    elif "sendmsg" in low or "sendto fd" in low:
        cls = "srv tx"
    elif "error" in low or "closing link" in low or "except" in low:
        cls = "srv err"
    elif "info" in low or "connect" in low:
        cls = "srv info"
    return '<div class="ln %s">%s</div>' % (cls, esc(text))


def scenario_panes_html(sc):
    order = []; by_label = {}
    for (t, label, d, line) in sc.events:
        if label not in by_label:
            by_label[label] = []; order.append(label)
        by_label[label].append((d, line))
    cnt = sc.counts()
    mark = "bad" if cnt.get(FAIL) else ("warn" if cnt.get(NOTE) else "")
    panes = []; cap = 500
    for label in order:
        rows = by_label[label]; trunc = ""
        if len(rows) > cap:
            trunc = '<div class="ln note">... (%d more lines)</div>' % (len(rows) - cap); rows = rows[:cap]
        body = "".join(colorize_client_line(d, l) for (d, l) in rows) + trunc
        panes.append('<div class="pane client %s"><div class="pane-h">%s</div><div class="pane-b">%s</div></div>'
                     % (mark, esc(label), body or '<div class="ln note">(no traffic)</div>'))
    srv_rows = sc.server_lines; trunc = ""
    if len(srv_rows) > cap:
        trunc = '<div class="ln note">... (%d more lines)</div>' % (len(srv_rows) - cap); srv_rows = srv_rows[:cap]
    srv_body = "".join(colorize_server_line(txt) for (_, txt) in srv_rows) + trunc
    panes.append('<div class="pane server %s"><div class="pane-h">SERVER log</div><div class="pane-b">%s</div></div>'
                 % (mark, srv_body or '<div class="ln note">(no server output captured for this window)</div>'))
    return '<div class="panes">%s</div>' % "".join(panes)


def scenario_checks_html(sc):
    rows = []
    for ch in sc.checks:
        pill = '<span class="pill %s">%s</span>' % (ch.status.lower(), ch.status)
        detail = []
        if ch.what:
            detail.append('<div class="d"><b>What:</b> %s</div>' % esc(ch.what))
        if ch.why:
            detail.append('<div class="d"><b>Why it matters:</b> %s</div>' % esc(ch.why))
        if ch.expect:
            ecls = "d exp" if ch.status == FAIL else "d"
            detail.append('<div class="%s"><b>Expected:</b> <code>%s</code></div>' % (ecls, esc(ch.expect)))
        if ch.status in (FAIL, NOTE) and ch.got:
            gcls = "d got bad" if ch.status == FAIL else "d got"
            detail.append('<div class="%s"><b>What actually happened:</b> <pre>%s</pre></div>' % (gcls, esc(ch.got[-600:])))
        if ch.status in (FAIL, NOTE) and ch.fix:
            lead = "How to fix" if ch.status == FAIL else "Your choice, and the alternatives"
            fcls = "d fix bad" if ch.status == FAIL else "d fix"
            detail.append('<div class="%s"><b>%s:</b> %s</div>' % (fcls, lead, esc(ch.fix)))
        rows.append('<div class="check %s"><div class="check-h">%s <span class="cn">%s</span></div>%s</div>'
                    % (ch.status.lower(), pill, esc(ch.name), "".join(detail)))
    return "".join(rows)


def scenario_html(sc, idx):
    c = sc.counts(); dur = (sc.t1 - sc.t0) * 1000 if sc.t1 else 0
    badges = []
    for st in (FAIL, NOTE, PASS, SKIP):
        if c.get(st):
            badges.append('<span class="mini %s">%d %s</span>' % (st.lower(), c[st], st))
    metrics = ""
    if sc.metrics:
        metrics = '<div class="metrics">' + "".join('<span class="metric"><b>%s:</b> %s</span>' % (esc(k), esc(v)) for (k, v) in sc.metrics) + '</div>'
    open_attr = "open" if (c.get(FAIL) or c.get(NOTE)) else ""
    banner = ""
    if c.get(FAIL):
        banner = ('<div class="scbanner bad">This scenario has %d issue(s). The red checks say what was expected vs. what happened; '
                  'the red-bordered windows are the clients and server involved.</div>' % c[FAIL])
    elif c.get(NOTE):
        banner = ('<div class="scbanner warn">%d note(s) here &mdash; optional extras or design choices, NOT failures. '
                  'Each one states your choice and the alternatives.</div>' % c[NOTE])
    return """
    <details class="scenario" %s>
      <summary><span class="grp %s">%s</span><span class="stitle">%s</span><span class="badges">%s</span><span class="dur">%.0f ms</span></summary>
      <div class="blurb">%s</div>
      %s
      %s
      <div class="checks">%s</div>
      <div class="screens-h">Screens &mdash; every client's window and the server log for this scenario (problematic ones bordered)</div>
      %s
    </details>
    """ % (open_attr, sc.group.lower(), esc(sc.group), esc(sc.title), "".join(badges), dur, esc(sc.blurb), banner, metrics,
           scenario_checks_html(sc), scenario_panes_html(sc))


def profile_html():
    d = profile_defaults()
    rows = []
    for (k, _, kind, desc) in PROFILE_SPEC:
        cur = PROFILE.get(k); changed = cur != d[k]
        rows.append('<tr class="%s"><td><code>%s</code></td><td><b>%s</b></td><td>%s</td><td>%s</td></tr>'
                    % ("changed" if changed else "", esc(k), esc(str(cur)), esc(str(d[k])), esc(desc)))
    return ('<details class="profile"><summary>Implementation profile used for this run &mdash; %s</summary>'
            '<table><tr><th>option</th><th>this run</th><th>default</th><th>meaning</th></tr>%s</table>'
            '<div class="d">Set with <code>--set key=value</code>, a <code>tester.conf</code>, or the menu. '
            'These describe design choices the subject leaves open; they change what the tester expects, never whether a subject requirement passes.</div></details>'
            % (esc(profile_summary()), "".join(rows)))


def write_html(path, extra_files):
    t = REPORT.totals(); total = sum(t.values())
    passed = t[PASS]; failed = t[FAIL]; notes = t[NOTE]; skipped = t[SKIP]
    rate = (100.0 * passed / (passed + failed)) if (passed + failed) else 100.0
    when = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    body = []; cur = None
    for i, sc in enumerate(REPORT.scenarios):
        if sc.group != cur:
            cur = sc.group; body.append('<h2 class="ghead %s">%s</h2>' % (sc.group.lower(), esc(sc.group)))
        body.append(scenario_html(sc, i))
    files_html = ""
    if extra_files:
        items = "".join('<li><code>%s</code> &mdash; %s</li>' % (esc(n), esc(desc)) for (n, desc) in extra_files)
        files_html = '<div class="files"><b>Companion files (same folder):</b><ul>%s</ul></div>' % items
    doc = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>ft_irc test report</title>
<style>
:root{--ink:#1b1f24;--muted:#5c646d;--faint:#8b939c;--bg:#f6f7f5;--card:#fff;--line:#e2e5e1;
--pass:#2f855a;--fail:#c0392b;--note:#b7791f;--skip:#8b939c;--info:#0f766e;--bonus:#2b6cb0;--load:#6b46c1;
--tx:#8a5a00;--rx:#0f766e;--accent:#0f766e;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
--sans:-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;}
@media(prefers-color-scheme:dark){:root:not([data-theme=light]){--ink:#e6e9ec;--muted:#a2acb5;--faint:#727b84;--bg:#14171a;--card:#1c2024;--line:#2c3238;--tx:#e0a860;--rx:#4fd1c5;}}
*{box-sizing:border-box}html,body{margin:0}body{font-family:var(--sans);background:var(--bg);color:var(--ink);font-size:14px;line-height:1.5}
.wrap{max-width:1100px;margin:0 auto;padding-block:22px;padding-left:16px;padding-right:16px}
header.top{border-bottom:2px solid var(--ink);padding-bottom:14px;margin-bottom:8px}
h1{font-size:22px;margin:0 0 2px}.sub{color:var(--muted);font-size:13px}
.kpis{display:flex;flex-wrap:wrap;gap:10px;margin:16px 0}
.kpi{background:var(--card);border:1px solid var(--line);border-radius:9px;padding:10px 14px;min-width:96px}
.kpi .n{font-family:var(--mono);font-size:20px;font-weight:700;line-height:1}
.kpi .l{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em;margin-top:4px}
.n.pass{color:var(--pass)}.n.fail{color:var(--fail)}.n.note{color:var(--note)}.n.skip{color:var(--skip)}.n.rate{color:var(--accent)}
.legend{font-size:12px;color:var(--muted);margin:6px 0 12px}.legend code{font-family:var(--mono)}
.profile{background:var(--card);border:1px solid var(--line);border-radius:9px;padding:8px 14px;margin:0 0 16px;font-size:13px}
.profile summary{cursor:pointer;font-weight:600}.profile table{border-collapse:collapse;width:100%%;margin-top:8px;font-size:12.5px}
.profile th,.profile td{text-align:left;padding:4px 8px;border-bottom:1px solid var(--line);vertical-align:top}
.profile tr.changed td{background:rgba(183,121,31,.10)}.profile code{font-family:var(--mono)}
h2.ghead{font-size:13px;text-transform:uppercase;letter-spacing:.06em;margin:26px 0 8px;padding-bottom:4px;border-bottom:1px solid var(--line)}
h2.mandatory{color:var(--pass)}h2.robustness{color:var(--accent)}h2.load{color:var(--load)}h2.bonus{color:var(--bonus)}h2.compare{color:var(--note)}
.scenario{background:var(--card);border:1px solid var(--line);border-radius:10px;margin:8px 0;overflow:hidden}
summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:10px;padding:11px 14px}
summary::-webkit-details-marker{display:none}
.grp{font-size:10px;text-transform:uppercase;letter-spacing:.05em;padding:2px 7px;border-radius:20px;color:#fff;font-weight:700}
.grp.mandatory{background:var(--pass)}.grp.robustness{background:var(--accent)}.grp.load{background:var(--load)}.grp.bonus{background:var(--bonus)}.grp.compare{background:var(--note)}
.stitle{font-weight:700;flex:1}.badges{display:flex;gap:5px}.mini{font-size:10px;padding:2px 6px;border-radius:5px;font-weight:700}
.mini.pass{background:rgba(47,133,90,.15);color:var(--pass)}.mini.fail{background:rgba(192,57,43,.15);color:var(--fail)}
.mini.note{background:rgba(183,121,31,.15);color:var(--note)}.mini.skip{background:rgba(139,147,156,.2);color:var(--skip)}
.dur{font-family:var(--mono);font-size:11px;color:var(--faint)}
.blurb{padding:0 14px 8px;color:var(--muted);font-size:13px}
.metrics{padding:0 14px 8px;display:flex;flex-wrap:wrap;gap:12px}.metric{font-size:12px;background:rgba(15,118,110,.08);padding:3px 8px;border-radius:5px}
.checks{padding:2px 14px 10px}
.check{border-left:3px solid var(--line);padding:6px 10px;margin:6px 0;background:rgba(0,0,0,.015);border-radius:0 6px 6px 0}
.check.fail{border-color:var(--fail)}.check.pass{border-color:var(--pass)}.check.note{border-color:var(--note)}.check.skip{border-color:var(--skip)}
.check-h{display:flex;align-items:center;gap:8px}.cn{font-weight:600}
.pill{font-size:10px;font-weight:700;padding:1px 6px;border-radius:5px;color:#fff}
.pill.pass{background:var(--pass)}.pill.fail{background:var(--fail)}.pill.note{background:var(--note)}.pill.skip{background:var(--skip)}.pill.info{background:var(--info)}
.d{font-size:12.5px;color:var(--muted);margin:3px 0 0}.d code{font-family:var(--mono);font-size:12px}
.d.fix{color:var(--ink)}.d.got pre{font-family:var(--mono);font-size:11.5px;background:var(--bg);border:1px solid var(--line);border-radius:5px;padding:6px;margin:3px 0 0;white-space:pre-wrap;word-break:break-word}
.d.exp{color:var(--ink);font-weight:600}.d.got.bad pre{border-color:var(--fail);background:rgba(192,57,43,.06);color:var(--ink)}.d.fix.bad{color:var(--fail);font-weight:600}
.scbanner{margin:0 14px 8px;padding:7px 10px;border-radius:6px;font-size:12.5px;font-weight:600}
.scbanner.bad{background:rgba(192,57,43,.10);border:1px solid var(--fail);color:var(--fail)}
.scbanner.warn{background:rgba(183,121,31,.10);border:1px solid var(--note);color:var(--note)}
.screens-h{padding:8px 14px 4px;font-size:11px;text-transform:uppercase;letter-spacing:.05em;color:var(--faint)}
.panes{display:flex;gap:8px;overflow-x:auto;padding:0 14px 14px}
.pane{flex:0 0 300px;border:1px solid var(--line);border-radius:8px;overflow:hidden;background:var(--bg)}.pane.server{flex:0 0 380px}
.pane.bad{border:2px solid var(--fail)}.pane.bad .pane-h{background:rgba(192,57,43,.10);color:var(--fail)}
.pane.warn{border:2px solid var(--note)}.pane.warn .pane-h{background:rgba(183,121,31,.10);color:var(--note)}
.pane-h{background:var(--card);border-bottom:1px solid var(--line);padding:5px 9px;font-weight:700;font-size:11px;font-family:var(--mono)}
.pane.server .pane-h{color:var(--accent)}.pane-b{max-height:280px;overflow:auto;padding:6px 8px}
.ln{font-family:var(--mono);font-size:11px;line-height:1.45;white-space:pre-wrap;word-break:break-word}
.ln .arw{display:inline-block;width:14px;color:var(--faint)}.ln.tx{color:var(--tx)}.ln.rx{color:var(--rx)}.ln.note{color:var(--faint)}
.ln.srv{color:var(--muted)}.ln.srv.rx{color:var(--rx)}.ln.srv.tx{color:var(--tx)}.ln.srv.err{color:var(--fail)}.ln.srv.info{color:var(--accent)}
.files{background:var(--card);border:1px solid var(--line);border-radius:9px;padding:10px 14px;margin:18px 0}.files ul{margin:6px 0 0;padding-left:18px}.files code{font-family:var(--mono)}
.sum{border-collapse:collapse;margin:8px 0;font-size:12.5px}.sum th,.sum td{text-align:left;padding:3px 10px;border-bottom:1px solid var(--line)}.sum td.n{font-family:var(--mono);font-weight:700}.sum td.mono{font-family:var(--mono)}
.files h3{font-size:12px;text-transform:uppercase;letter-spacing:.05em;margin:12px 0 4px;color:var(--muted)}
footer{color:var(--faint);font-size:12px;margin-top:22px;border-top:1px solid var(--line);padding-top:10px}
</style></head><body><div class="wrap">
<header class="top"><h1>ft_irc test report</h1>
<div class="sub">%s &middot; server %s &middot; mandatory <code>%s</code>, bonus <code>%s</code></div></header>
<div class="kpis">
<div class="kpi"><div class="n rate">%.0f%%</div><div class="l">Pass rate</div></div>
<div class="kpi"><div class="n pass">%d</div><div class="l">Passed</div></div>
<div class="kpi"><div class="n fail">%d</div><div class="l">Failed</div></div>
<div class="kpi"><div class="n note">%d</div><div class="l">Notes (optional)</div></div>
<div class="kpi"><div class="n skip">%d</div><div class="l">Skipped</div></div>
<div class="kpi"><div class="n">%d</div><div class="l">Total checks</div></div>
</div>
<div class="legend"><b style="color:var(--pass)">PASS</b> required behaviour works &middot; <b style="color:var(--fail)">FAIL</b> a real problem &middot; <b style="color:var(--note)">NOTE</b> (amber) = not a failure: an optional RFC extra or a design choice the subject leaves open, shown with your choice and the alternatives &middot; each check expands to what it tests, why, expected vs. what happened, and how to fix. Panes show every line each client sent (<code>&rarr;</code>) and received (<code>&larr;</code>) plus the server log.</div>
%s
%s
%s
%s
<footer>Generated by tests/tester/test_irc.py &middot; outputs under tests/tester/artifacts/</footer>
</div></body></html>
""" % (when, esc(REPORT.args.host), esc(REPORT.args.binary), esc(REPORT.args.bonus_binary),
       rate, passed, failed, notes, skipped, total, profile_html(), files_html, "".join(body), summary_html())
    with open(path, "w") as f:
        f.write(doc)


def summary_html():
    """End-of-report summary: per-group counts, every FAIL and NOTE in one place, all metrics."""
    groups = collections.OrderedDict()
    fails = []; notes_ = []; metrics = []
    for sc in REPORT.scenarios:
        c = sc.counts(); g = groups.setdefault(sc.group, {PASS: 0, FAIL: 0, NOTE: 0, SKIP: 0})
        for k in (PASS, FAIL, NOTE, SKIP):
            g[k] += c.get(k, 0)
        for ch in sc.checks:
            if ch.status == FAIL:
                fails.append((sc.title, ch.name, ch.fix))
            elif ch.status == NOTE:
                notes_.append((sc.title, ch.name))
        for (k, v) in sc.metrics:
            metrics.append((sc.title, k, v))
    rows = "".join('<tr><td>%s</td><td class="n pass">%d</td><td class="n fail">%d</td><td class="n note">%d</td><td class="n skip">%d</td></tr>'
                   % (esc(g), c[PASS], c[FAIL], c[NOTE], c[SKIP]) for (g, c) in groups.items())
    fl = "".join('<li><b>%s</b> &mdash; %s<div class="d fix bad">%s</div></li>' % (esc(a), esc(b), esc(f)) for (a, b, f) in fails) or "<li>none</li>"
    nl = "".join('<li><b>%s</b> &mdash; %s</li>' % (esc(a), esc(b)) for (a, b) in notes_) or "<li>none</li>"
    ml = "".join('<tr><td>%s</td><td>%s</td><td class="mono">%s</td></tr>' % (esc(a), esc(k), esc(v)) for (a, k, v) in metrics) or "<tr><td colspan=3>none</td></tr>"
    verdict = ("No failures: every subject requirement checked here passed." if not fails else
               "%d failure(s) to fix, listed below with the suggested fix." % len(fails))
    return """
<h2 class="ghead">Summary</h2>
<div class="files"><b>%s</b> %d note(s) are design choices or optional extras, not failures.
<table class="sum"><tr><th>group</th><th>pass</th><th>fail</th><th>note</th><th>skip</th></tr>%s</table>
<h3>Failures</h3><ul>%s</ul>
<h3>Notes (design choices / optional)</h3><ul>%s</ul>
<h3>Measurements</h3><table class="sum"><tr><th>scenario</th><th>metric</th><th>value</th></tr>%s</table>
<div class="d">Profile used: %s</div></div>
""" % (esc(verdict), len(notes_), rows, fl, nl, ml, esc(profile_summary()))


def write_transcripts(outdir):
    tdir = os.path.join(outdir, "transcripts"); os.makedirs(tdir, exist_ok=True)
    for sc in REPORT.scenarios:
        lines = ["# %s  [%s]" % (sc.title, sc.group), "# %s" % sc.blurb, "", "## checks"]
        for ch in sc.checks:
            lines.append("[%s] %s" % (ch.status, ch.name))
            if ch.status in (FAIL, NOTE) and ch.got:
                for l in ch.got.splitlines()[-4:]:
                    lines.append("      got: %s" % l)
            if ch.status in (FAIL, NOTE) and ch.fix:
                lines.append("      %s: %s" % ("fix" if ch.status == FAIL else "note", ch.fix))
        lines += ["", "## client screens"]
        order = []; by = {}
        for (t, label, d, line) in sc.events:
            by.setdefault(label, [])
            if label not in order:
                order.append(label)
            by[label].append("%s %s" % (d, line.replace("\x01", "<SOH>")))
        for label in order:
            lines.append("--- %s ---" % label); lines.extend(by[label]); lines.append("")
        lines.append("## server log (this scenario)")
        for (_, txt) in sc.server_lines:
            lines.append(txt)
        with open(os.path.join(tdir, "%s.txt" % sc.key), "w") as f:
            f.write("\n".join(lines) + "\n")


# ===========================================================================
# LIVE mode: curses view of the run (scenario list + client windows + server log)
# ===========================================================================

class LiveUI(object):
    """Fixed-region live view of a test run. Nothing moves when the log grows.

        row 0      header: mode, port, pid, RSS, CPU, running totals
        row 1      in / out messages-per-second sparklines, profile
        row 2      poll set: sockets = listeners + clients, fds open, stalled badge
        rows 3..   left: SCENARIOS (fixed 30 cols)  |  right: SERVER CLIENTS (irctop table: nick, in/out, SendQ, channels)
        rows ..    CLIENT WINDOWS of the current scenario (what each test client sent -> / received <-)
        rows ..    SERVER LOG (own scroll)
        row h-1    keys
    """

    def __init__(self, mode, extra_files):
        self.mode = mode; self.extra_files = extra_files
        self.done = False; self.error = None
        self.log_scroll = 0
        self.mon = MonState() if MonState else None
        self.fed = 0; self.last_sample = 0
        self.socks = -1; self.fds = -1

    def worker(self):
        try:
            run_everything(self.mode, self.extra_files)
        except SystemExit:
            self.error = "a server failed to start (see artifacts/server_*.log)"
        except Exception as e:
            self.error = "%s: %s" % (type(e).__name__, e)
        finally:
            self.done = True

    def _feed_monitor(self, srv):
        """Push new server-log lines into the irctop State (nicks, channels, per-client counters)."""
        if self.mon is None or srv is None:
            return
        with srv.lock:
            new = srv.buffer[self.fed:]
            self.fed = len(srv.buffer)
        for (_, txt) in new:
            self.mon.feed(txt)

    def run(self, stdscr):
        curses.curs_set(0); stdscr.nodelay(True); stdscr.timeout(200); stdscr.keypad(True)
        curses.start_color(); curses.use_default_colors()
        many = curses.COLORS >= 256
        pal = [(1, 114 if many else curses.COLOR_GREEN), (2, 214 if many else curses.COLOR_YELLOW),
               (3, 203 if many else curses.COLOR_RED), (4, 67 if many else curses.COLOR_BLUE),
               (5, 176 if many else curses.COLOR_MAGENTA), (6, 81 if many else curses.COLOR_CYAN),
               (7, 223 if many else curses.COLOR_WHITE), (8, 245 if many else curses.COLOR_WHITE)]
        for i, col in pal:
            curses.init_pair(i, col, -1)
        C_G, C_Y, C_R, C_FRAME, C_M, C_FOCUS, C_TITLE, C_DIM = [curses.color_pair(i) for i in range(1, 9)]
        GRAD = [curses.color_pair(i) for i in (4, 6, 5, 1, 2)]
        BOLD = curses.A_BOLD
        G = glyphs()
        threading.Thread(target=self.worker, daemon=True).start()
        last_srv = None
        # panes: 0 scenarios, 1 server clients, 2.. client windows (up to 5), then server log
        focus = 0
        scroll = {}           # pane -> (vertical, horizontal)

        def sc_of(pane):
            return scroll.get(pane, (0, 0))

        def put(y, x, s, attr=0):
            h, w = stdscr.getmaxyx()
            if 0 <= y < h and 0 <= x < w:
                lim = w - x - (1 if y == h - 1 else 0)
                try:
                    stdscr.addstr(y, x, s[: max(0, lim)], attr)
                except curses.error:
                    pass

        def box(y, x, hh, ww, title, attr, focused):
            if hh < 2 or ww < 4:
                return (y + 1, x + 1, max(0, hh - 2), max(0, ww - 2))
            a = (C_FOCUS | BOLD) if focused else attr
            put(y, x, G["tl"] + G["h"] * (ww - 2) + G["tr"], a)
            for i in range(1, hh - 1):
                put(y + i, x, G["v"], a); put(y + i, x + ww - 1, G["v"], a)
            put(y + hh - 1, x, G["bl"] + G["h"] * (ww - 2) + G["br"], a)
            put(y, x + 2, (" %s " % title)[: ww - 4], (curses.A_REVERSE | BOLD) if focused else (a | BOLD))
            return (y + 1, x + 1, hh - 2, ww - 2)

        def rows_of(lines, pane, ih, iw):
            """Slice for a pane: `v` lines up from the bottom, `hs` columns to the right."""
            v, hs = sc_of(pane)
            v = max(0, min(v, max(0, len(lines) - ih))); scroll[pane] = (v, hs)
            end = len(lines) - v
            return [(l[0], l[1][hs: hs + iw].ljust(iw)) for l in lines[max(0, end - ih): max(0, end)]]

        while True:
            h, w = stdscr.getmaxyx(); stdscr.erase()
            srv = REPORT.server; t = REPORT.totals(); cur = REPORT.current
            if srv is not last_srv:               # new phase / new server: reset the monitor
                last_srv = srv; self.fed = 0
                if MonState:
                    self.mon = MonState()
            self._feed_monitor(srv)
            now = time.time()
            if now - self.last_sample >= 1.0 and srv and srv.proc:
                self.last_sample = now
                rss, cpu = ps_stats(srv.proc.pid); self.fds, self.socks = fd_counts(srv.proc.pid)
                if self.mon:
                    self.mon.rss_mb, self.mon.cpu = rss, cpu; self.mon.pid = srv.proc.pid; self.mon.port = REPORT.args.port
                    q = kernel_sendq(REPORT.args.port) if REPORT.args.port else {}
                    with self.mon.lock:
                        for c in self.mon.clients.values():
                            if not c.closed:
                                c.sendq = q.get(c.peer, 0)
                    self.mon.tick()
            mon = self.mon
            clients = mon.live_clients() if mon else []

            # ---- header + traffic line ----
            hdr = " ft_irc tester  mode=%s  port %s  pid %s  RSS %d MB  CPU %.1f%%   %d pass  %d fail  %d note  %d skip%s" % (
                self.mode, REPORT.args.port, (srv.proc.pid if (srv and srv.proc) else "-"),
                mon.rss_mb if mon else 0, mon.cpu if mon else 0.0, t[PASS], t[FAIL], t[NOTE], t[SKIP],
                "   DONE - press q" if self.done else ("   PAUSED - Enter: next scenario, c: continue" if REPORT.waiting else ("   step mode: pausing after this scenario" if REPORT.step else "")))
            put(0, 0, hdr.ljust(w - 1), curses.A_REVERSE | BOLD)
            listeners = max(0, self.socks - len(clients)) if self.socks >= 0 else -1
            stalled = sum(1 for c in clients if c.sendq >= 32768)
            if srv:
                sw = max(8, (w - 70) // 3)
                put(1, 1, "in  ", C_DIM); put(1, 5, _spark(srv.msgs_in, sw), C_G); put(1, 6 + sw, "%4d/s" % ((list(srv.msgs_in) or [0])[-1]), C_G)
                x2 = 14 + sw; put(1, x2, "out ", C_DIM); put(1, x2 + 4, _spark(srv.msgs_out, sw), C_Y); put(1, x2 + 5 + sw, "%4d/s" % ((list(srv.msgs_out) or [0])[-1]), C_Y)
                put(1, x2 + 13 + sw, ("poll set: %s sockets = %s listeners + %d clients   fds %s   profile: %s" % (
                    self.socks if self.socks >= 0 else "?", listeners if listeners >= 0 else "?", len(clients),
                    self.fds if self.fds >= 0 else "?", profile_summary()))[: w - x2 - 14 - sw], C_DIM)
            if stalled:
                put(1, w - 22, " %d STALLED " % stalled, C_R | BOLD | curses.A_REVERSE)

            # ---- scenarios | server clients ----
            top = 2; lw = 34; T = max(6, min(12, (h - 12) // 3))
            with REPORT.lock:
                scs = list(REPORT.scenarios)
            iy, ix, ih, iw = box(top, 0, T + 2, lw, "SCENARIOS %d" % len(scs), C_FRAME, focus == 0)
            sc_lines = []
            for sc in scs:
                c = sc.counts()
                if sc.state == "running":
                    mark, attr = ">", C_FOCUS | BOLD
                elif c.get(FAIL):
                    mark, attr = "x", C_R | BOLD
                elif c.get(NOTE):
                    mark, attr = "o", C_Y
                else:
                    mark, attr = "v", C_G
                sc_lines.append((attr, "%s %-24s %s" % (mark, sc.title[:24], "%d/%d" % (c[PASS], sum(c.values())))))
            for i, (attr, line) in enumerate(rows_of(sc_lines, 0, ih, iw)):
                put(iy + i, ix, line, attr)
            rx = lw; rw = w - rx
            iy, ix, ih, iw = box(top, rx, T + 2, rw, "SERVER CLIENTS (from its log)   unread-by-it = kernel bytes the client has not read", C_FRAME, focus == 1)
            put(iy, ix, "%-4s %-12s %-11s %6s %6s %12s %6s %s" % ("fd", "nick", "state", "in", "out", "unread-by-it", "idle", "channels"), C_TITLE | BOLD)
            cl_lines = []
            for c in clients:
                attr = C_R | BOLD if c.sendq >= 32768 else (C_Y if c.sendq > 0 else 0)
                cl_lines.append((attr, "%-4s %-12s %-11s %6d %6d %12s %5ds %s" % (
                    c.fd, (c.nick or "-")[:12], "stalled" if c.sendq >= 32768 else ("registered" if c.registered else "connecting"),
                    c.msgs_in, c.msgs_out, human_bytes(c.sendq), int(now - c.last), ",".join(sorted(c.chans)))))
            for i, (attr, line) in enumerate(rows_of(cl_lines, 1, ih - 1, iw)):
                put(iy + 1 + i, ix, line, attr)

            # ---- client windows of the current scenario ----
            wy = top + T + 2
            sc = cur or (scs[-1] if scs else None)
            log_rows = max(6, min(14, (h - wy) // 3))
            win_h = h - wy - log_rows - 3
            order = []; by = {}
            if sc:
                for (_, label, d, line) in sc.events:
                    if label not in by:
                        by[label] = []; order.append(label)
                    by[label].append((d, line))
            ports = {}
            for sock, lab in list(LABELS.items()):
                try:
                    ports[lab] = "%s:%d" % sock.getsockname()
                except Exception:
                    pass
            q = kernel_sendq(REPORT.args.port) if (REPORT.args.port and order) else {}
            shown_labels = order[-5:]
            ncols = max(1, len(shown_labels)); colw = w // ncols
            if win_h > 2:
                if not shown_labels:
                    box(wy, 0, win_h, w, "CLIENT WINDOWS  %s" % (sc.title if sc else ""), C_FRAME, focus == 2)
                for ci, label in enumerate(shown_labels):
                    x = ci * colw; ww = w - x if ci == ncols - 1 else colw
                    sq = q.get(ports.get(label, ""), 0)
                    frame = C_R if sq >= 32768 else GRAD[ci % len(GRAD)]
                    iy, ix, ih, iw = box(wy, x, win_h, ww, "%s   unread-by-it %s" % (label, human_bytes(sq)), frame, focus == 2 + ci)
                    lines = [(C_Y if d == ">>" else C_G, "%s %s" % (G["out"] if d == ">>" else G["inn"], printable(line))) for (d, line) in by[label]]
                    for li, (attr, line) in enumerate(rows_of(lines, 2 + ci, ih, iw)):
                        put(iy + li, ix, line, attr)
            npanes = 2 + len(shown_labels)

            # ---- server log ----
            ly = wy + win_h
            iy, ix, ih, iw = box(ly, 0, h - ly - 1, w, "SERVER LOG", C_FRAME, focus == npanes)
            if srv:
                with srv.lock:
                    entries = list(srv.buffer)
                log_lines = []
                for (tt, txt) in entries:
                    low = txt.lower()
                    attr = C_G if "from fd" in low else (C_Y if "sendto fd" in low else (C_R if ("error" in low or "closing" in low) else (C_M if "connect" in low else C_DIM)))
                    log_lines.append((attr, printable("%s %s" % (time.strftime("%H:%M:%S", time.localtime(tt)), re.sub(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \S+\s+", "", txt)))))
                for i, (attr, line) in enumerate(rows_of(log_lines, npanes, ih, iw)):
                    put(iy + i, ix, line, attr)
            put(h - 1, 0, (" p pause after each scenario   Enter/space next   c continue   q quit (after DONE)   Tab pick a pane   arrows/PgUp/PgDn scroll   red frame = stalled   %s"
                           % (self.error or "")).ljust(w - 1), curses.A_REVERSE | (C_R if self.error else 0))
            stdscr.refresh()
            try:
                k = stdscr.getch()
            except curses.error:
                k = -1
            if k in (ord("q"), ord("Q")) and self.done:
                break
            elif k in (ord("p"), ord("P")):              # pause after every scenario from now on
                REPORT.step = True
            elif k in (10, 13, curses.KEY_ENTER, ord(" ")):   # next scenario (stays in step mode)
                if not REPORT.step:
                    REPORT.step = True
                REPORT.go.set()
            elif k in (ord("c"), ord("C")):              # continue without pausing
                REPORT.step = False; REPORT.go.set()
            elif k == 9:
                focus = (focus + 1) % (npanes + 1)
            elif k == curses.KEY_BTAB:
                focus = (focus - 1) % (npanes + 1)
            elif k in (curses.KEY_UP, ord("k"), curses.KEY_DOWN, ord("j"), curses.KEY_PPAGE, curses.KEY_NPAGE, curses.KEY_LEFT, curses.KEY_RIGHT):
                v, hs = sc_of(focus)
                v += {curses.KEY_UP: 1, ord("k"): 1, curses.KEY_DOWN: -1, ord("j"): -1, curses.KEY_PPAGE: 10, curses.KEY_NPAGE: -10}.get(k, 0)
                hs += {curses.KEY_LEFT: -8, curses.KEY_RIGHT: 8}.get(k, 0)
                scroll[focus] = (max(0, v), max(0, hs))


# ===========================================================================
# LAB: interactive multi-client console (server log + up to 5 client windows)
# ===========================================================================

LAB_HELP = [
    ("Keys", None),
    ("Tab / Shift-Tab",   "next / previous pane: clients, server log, help, menu bar; a client pane is where typed lines go"),
    ("menu bar",          "Left/Right pick an option, Enter or Up/Down change it, b back: what windows show, order, per row, log rows, log filter, help, eol, walkthrough, new client, quit"),
    ("Up/Down  Left/Right", "scroll the focused pane (vertical / horizontal)"),
    ("PgUp / PgDn",       "scroll the focused pane by a page"),
    ("Ctrl-L",            "layout: help+log / windows only / help only"),
    ("Enter",             "send the input line to the active client (empty Enter = run the walkthrough step shown)"),
    ("", None),
    ("Lab commands", None),
    ("/new [nick|N|a,b]", "1 client, N clients, or these nicks (auto PASS/NICK/USER)"),
    ("/use N",            "make client N active"),
    ("/msg <target> <text>", "= PRIVMSG <target> :<text>      /join #chan = JOIN #chan"),
    ("/eol crlf|lf|none", "line ending used when sending"),
    ("/long N [target]",  "PRIVMSG padded to exactly N chars"),
    ("/noeol <text>",     "send text with NO line ending (next send is glued on)"),
    ("/raw <text>",       "send exactly this (+ current eol)"),
    ("/pause | /resume",  "stop / restart reading (the ^Z test of the eval sheet)"),
    ("/close",            "close the active client abruptly (like kill -9 on nc)"),
    ("/walk",             "walkthrough: the next step is shown first, Enter runs it; type your own line to try something else; /skip /back /stop"),
    ("/help /cols N /log N /helpw N", "hide help / windows per row / log rows / help width"),
    ("/quit",             "leave the lab (or Ctrl-D)"),
    ("", None),
    ("File transfer", None),
    ("/send <nick> <path>", "WITHOUT the bot: this client offers <path> to <nick> with a DCC SEND line and serves it"),
    ("/dcc",              "receive the pending offer in the active window (saves to artifacts/)"),
    ("PRIVMSG DCCBot :!send <nick> <file>", "WITH the bot: offer files/<file>; receiver: PRIVMSG DCCBot :!accept, then /dcc"),
    ("", None),
    ("IRC commands (typed as-is, sent to the active client)", None),
    ("PASS <pw>  NICK <nick>  USER <u> 0 * :<real>", "registration (done for you on /new)"),
    ("JOIN #chan [key]  /  PART #chan [:msg]", "first joiner becomes @op"),
    ("PRIVMSG <#chan|nick> :txt  /  NOTICE ...", "message / message without error replies"),
    ("TOPIC #chan [:txt]", "read / set topic"),
    ("MODE #chan +i|+t|+k key|+o nick|+l n", "channel modes (-x removes); MODE #chan = query"),
    ("KICK #chan nick [:why]  /  INVITE nick #chan", "operator commands"),
    ("PING :x  /  QUIT :bye  /  CAP LS 302", "liveness / leave / IRCv3 handshake"),
    ("PRIVMSG DCCBot :!help|!version|!whoami", "bonus bot commands"),
]

# (who, line-or-action, what to say)
DEMO_STEPS = [
    ("alice", "JOIN #demo", "alice creates #demo and becomes operator (@alice in the 353 names list)"),
    ("bob", "JOIN #demo", "bob joins: alice sees bob's JOIN, bob gets the names list"),
    ("carol", "JOIN #demo", "carol joins too"),
    ("alice", "PRIVMSG #demo :hello everyone", "channel message: bob and carol receive it, alice does not get an echo"),
    ("bob", "PRIVMSG alice :just you", "private message to one nick"),
    ("bob", "TOPIC #demo :first topic", "anyone can set the topic while +t is off"),
    ("alice", "MODE #demo +t", "operator sets +t: topic now restricted to operators"),
    ("bob", "TOPIC #demo :hijack", "bob is not op -> 482 ERR_CHANOPRIVSNEEDED"),
    ("alice", "MODE #demo +o bob", "alice gives bob operator"),
    ("bob", "KICK #demo carol :see you", "bob (now op) kicks carol; everyone sees the KICK"),
    ("carol", "JOIN #demo", "carol comes back (no +i yet)"),
    ("alice", "MODE #demo +i", "invite-only"),
    ("carol", "PART #demo :brb", "carol leaves..."),
    ("carol", "JOIN #demo", "...and is refused: 473 ERR_INVITEONLYCHAN"),
    ("alice", "INVITE carol #demo", "alice invites carol (341 to alice, INVITE to carol)"),
    ("carol", "JOIN #demo", "invited: carol gets in"),
    ("alice", "MODE #demo +k secret", "channel key"),
    ("alice", "MODE #demo +l 3", "user limit"),
    ("alice", "MODE #demo", "query: 324 shows +itkl secret 3"),
    ("alice", "MODE #demo -i-k-l", "clear the modes"),
    ("bob", "PRIVMSG DCCBOT :!version", "BONUS: the bot answers (401 No such nick on the mandatory binary)"),
    ("alice", "PRIVMSG DCCBOT :!send bob tester_sample.txt", "BONUS: alice offers files/tester_sample.txt to bob"),
    ("bob", "PRIVMSG DCCBOT :!accept", "BONUS: bob accepts the pending offer"),
    ("bob", "@dcc", "BONUS: bob's window connects to the DCC port and receives the bytes"),
    ("carol", "QUIT :bye", "carol quits: alice and bob see the QUIT"),
]


def glyphs():
    """Box/arrow characters. Python's curses on macOS is often built against the old non-wide
    ncurses, which corrupts multi-byte characters on redraw: use ASCII there."""
    wide = HAVE_CURSES and hasattr(curses, "unget_wch")
    if wide:
        return dict(tl="╭", tr="╮", bl="╰", br="╯", h="─", v="│", out="→", inn="←", dot="·", up="↑")
    return dict(tl="+", tr="+", bl="+", br="+", h="-", v="|", out="->", inn="<-", dot="*", up="^")


def printable(text):
    """Control characters as ^A style so they take one cell and cannot break the panes."""
    return "".join(ch if ch >= " " else "^" + chr(ord(ch) + 64) for ch in text)


class LabClient(object):
    def __init__(self, idx, nick):
        self.idx = idx; self.nick = nick; self.sock = None
        self.lines = collections.deque(maxlen=400)   # (dir, text)
        self.buf = b""; self.last_len = 0; self.sendq = 0; self.closed = False; self.paused = False
        self.dcc_offer = None
        self.scroll = 0; self.hscroll = 0            # pane scrolling (lines from the bottom / columns)
        self.dcc_listener = None; self.dcc_path = None; self.dcc_conn = None; self.dcc_sent = 0

    def note(self, text):
        self.lines.append(("--", printable(text)))


class Lab(object):
    def __init__(self, args, nicks=None):
        self.args = args; self.clients = []; self.active = 0; self.eol = "crlf"
        self.start_nicks = list(nicks or ["alice"])
        self.input = ""; self.msg = ""; self.demo = None; self.demo_i = 0
        self.log_scroll = 0; self.help_scroll = 0; self.quit = False
        self.bot = PROFILE["bot_nick"]
        self.help_on = True; self.help_w = 36; self.log_rows = 8; self.grid_cols = 0   # 0 = automatic

    # ---- client plumbing ----
    def eol_bytes(self):
        return {"crlf": b"\r\n", "lf": b"\n", "none": b""}[self.eol]

    def new_client(self, nick=None, register=True):
        if len(self.clients) >= 5:
            self.msg = "max 5 client windows"; return None
        nick = nick or ("user%d" % (len(self.clients) + 1))
        c = LabClient(len(self.clients), nick)
        try:
            c.sock = socket.create_connection((self.args.host, self.args.port), timeout=3); c.sock.setblocking(False)
        except OSError as e:
            self.msg = "connect failed: %s" % e; return None
        self.clients.append(c); self.active = c.idx; self.focus_request = c.idx
        if register:
            for line in ("PASS %s" % self.args.password, "NICK %s" % nick, "USER %s 0 * :%s" % (nick, nick)):
                self.send_line(c, line)
        return c

    def send_bytes(self, c, data, shown=None):
        if c.sock is None or c.closed:
            c.note("(closed)"); return
        try:
            c.sock.sendall(data)
        except OSError as e:
            c.note("send failed: %s" % e); c.closed = True; return
        c.last_len = len(data)
        c.lines.append((">>", printable(shown if shown is not None else data.decode(errors="replace").replace("\r", "\\r").replace("\n", "\\n"))))

    def send_line(self, c, line):
        self.send_bytes(c, line.encode() + self.eol_bytes(), line + {"crlf": "", "lf": "  [LF]", "none": "  [no eol]"}[self.eol])

    def pump(self):
        """Read anything pending on every client socket (non-blocking)."""
        self.dcc_serve()
        for c in self.clients:
            if c.sock is None or c.closed or c.paused:
                continue
            try:
                r, _, _ = select.select([c.sock], [], [], 0)
            except (OSError, ValueError):
                continue
            if not r:
                continue
            try:
                d = c.sock.recv(65536)
            except (BlockingIOError, InterruptedError):
                continue
            except OSError as e:
                c.note("recv error: %s" % e); c.closed = True; continue
            if not d:
                c.note("<< server closed the connection (EOF)"); c.closed = True; continue
            c.buf += d
            while b"\n" in c.buf:
                line, c.buf = c.buf.split(b"\n", 1)
                text = line.decode(errors="replace").replace("\r", "\\r")
                c.lines.append(("<<", printable(text)))
                m = re.search(r"DCC SEND (\S+) (\S+) (\d+) (\d+)", text)
                if m:
                    c.dcc_offer = (m.group(1), m.group(2), int(m.group(3)), int(m.group(4)))
                    c.note("DCC offer noted: %s from %s:%s (%s bytes) -- /dcc to receive" % (m.group(1), m.group(2), m.group(3), m.group(4)))

    def dcc_send(self, c, nick, path):
        """Bot-less DCC SEND: this client opens a listener, tells <nick> where, and serves the file
        when the receiver connects (what irssi's /dcc send does)."""
        if not os.path.isfile(path):
            self.msg = "no such file: %s" % path; return
        ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ls.bind(("127.0.0.1", 0)); ls.listen(1); ls.setblocking(False)
        port = ls.getsockname()[1]; size = os.path.getsize(path)
        c.dcc_listener = ls; c.dcc_path = path; c.dcc_conn = None; c.dcc_sent = 0
        self.send_line(c, "PRIVMSG %s :\x01DCC SEND %s 127.0.0.1 %d %d\x01" % (nick, os.path.basename(path), port, size))
        c.note("DCC SEND offered to %s: %s (%d bytes) on port %d; waiting for it to connect (the receiver types /dcc)" % (nick, os.path.basename(path), size, port))

    def dcc_serve(self):
        """Called from pump(): accept the receiver, stream the file, close."""
        for c in self.clients:
            if c.dcc_listener is None:
                continue
            try:
                if c.dcc_conn is None:
                    r, _, _ = select.select([c.dcc_listener], [], [], 0)
                    if not r:
                        continue
                    c.dcc_conn, _ = c.dcc_listener.accept(); c.dcc_conn.setblocking(True)
                    c.note("DCC: receiver connected, sending %s" % os.path.basename(c.dcc_path))
                with open(c.dcc_path, "rb") as f:
                    f.seek(c.dcc_sent)
                    chunk = f.read(65536)
                if chunk:
                    c.dcc_conn.sendall(chunk); c.dcc_sent += len(chunk)
                else:
                    c.note("DCC: sent %d bytes, done" % c.dcc_sent)
                    c.dcc_conn.close(); c.dcc_listener.close(); c.dcc_conn = c.dcc_listener = None
            except OSError as e:
                c.note("DCC send failed: %s" % e)
                for so in (c.dcc_conn, c.dcc_listener):
                    try:
                        if so: so.close()
                    except OSError:
                        pass
                c.dcc_conn = c.dcc_listener = None

    def dcc_receive(self, c):
        if not c.dcc_offer:
            self.msg = "no DCC offer seen in this window"; return
        fname, ip, port, size = c.dcc_offer
        if not re.match(r"^\d+\.\d+\.\d+\.\d+$", ip):
            ip = self.args.host
        def worker():
            data = b""
            try:
                ds = socket.create_connection((ip, port), timeout=5); ds.setblocking(False)
                end = time.time() + 8
                while time.time() < end and len(data) < size:
                    r, _, _ = select.select([ds], [], [], 0.25)
                    if r:
                        chunk = ds.recv(8192)
                        if not chunk:
                            break
                        data += chunk
                ds.close()
                try:
                    os.makedirs(self.args.outdir, exist_ok=True)
                    dest = os.path.join(self.args.outdir, "dcc_%s_%s" % (c.nick, os.path.basename(fname)))
                    with open(dest, "wb") as f:
                        f.write(data)
                    c.note("DCC: received %d / %d bytes of %s -> %s" % (len(data), size, fname, dest))
                except OSError as e:
                    c.note("DCC: received %d / %d bytes of %s (not saved: %s)" % (len(data), size, fname, e))
            except OSError as e:
                c.note("DCC connect failed: %s" % e)
        threading.Thread(target=worker, daemon=True).start()
        c.note("DCC: connecting to %s:%d ..." % (ip, port))

    # ---- commands ----
    def handle(self, text):
        text = text.rstrip("\n")
        if not text:
            if self.demo is not None:
                self.demo_step()
            return
        c = self.clients[self.active] if self.clients else None
        if text.startswith("/"):
            parts = text.split(" ", 2); cmd = parts[0].lower()
            if cmd == "/quit":
                self.quit = True
            elif cmd == "/new":
                arg = parts[1] if len(parts) > 1 else ""
                if arg.isdigit():                       # /new 3 -> three more clients
                    for _ in range(int(arg)):
                        self.new_client(None)
                elif "," in arg:                         # /new dave,eve
                    for nick in arg.split(","):
                        if nick.strip():
                            self.new_client(nick.strip())
                else:
                    self.new_client(arg or None)
            elif cmd == "/msg" and c is not None and len(parts) > 2:
                self.send_line(c, "PRIVMSG %s :%s" % (parts[1], parts[2]))
            elif cmd == "/join" and c is not None and len(parts) > 1:
                self.send_line(c, "JOIN " + parts[1])
            elif cmd == "/use" and len(parts) > 1 and parts[1].isdigit():
                n = int(parts[1]) - 1
                if 0 <= n < len(self.clients):
                    self.active = n; self.focus_request = n
            elif cmd == "/eol" and len(parts) > 1 and parts[1] in ("crlf", "lf", "none"):
                self.eol = parts[1]; self.msg = "line ending: %s" % self.eol
            elif cmd == "/long" and c is not None and len(parts) > 1 and parts[1].isdigit():
                n = int(parts[1]); target = parts[2] if len(parts) > 2 else c.nick
                head = "PRIVMSG %s :" % target
                body = head + ("x" * max(0, n - len(head)))
                self.send_line(c, body); c.note("sent %d chars + %d eol bytes = %d" % (len(body), len(self.eol_bytes()), len(body) + len(self.eol_bytes())))
            elif cmd == "/noeol" and c is not None:
                payload = text[len("/noeol "):]
                self.send_bytes(c, payload.encode(), payload + "  [no eol]"); c.note("sent %d bytes, no line ending" % len(payload))
            elif cmd == "/raw" and c is not None:
                self.send_line(c, text[len("/raw "):])
            elif cmd == "/dcc" and c is not None:
                self.dcc_receive(c)
            elif cmd == "/send" and c is not None and len(parts) > 2:
                self.dcc_send(c, parts[1], parts[2].strip())
            elif cmd == "/pause" and c is not None:
                c.paused = True; c.note("PAUSED: this window stops reading (like Ctrl-Z on nc). Flood it from another window, watch its sendq, then /resume")
            elif cmd == "/resume" and c is not None:
                c.paused = False; c.note("resumed: everything the server queued is read now")
            elif cmd == "/close" and c is not None:
                try:
                    c.sock.close()
                except OSError:
                    pass
                c.closed = True; c.note("closed abruptly (no QUIT)")
            elif cmd in ("/walk", "/demo"):
                self.start_demo()
            elif cmd == "/skip" and self.demo is not None:
                self.demo_i = min(len(self.demo), self.demo_i + 1); self.msg = "skipped"
            elif cmd == "/back" and self.demo is not None:
                self.demo_i = max(0, self.demo_i - 1); self.msg = "back one step"
            elif cmd == "/stop" and self.demo is not None:
                self.demo = None; self.msg = "walkthrough stopped"
            elif cmd == "/irssi" and len(parts) > 1 and parts[1].isdigit():
                k = max(1, min(5, int(parts[1])))
                self.msg = open_client_windows("irssi", list(NICKS[:k]), self.args.host, self.args.port, self.args.password, "windows")
            elif cmd == "/help":
                self.help_on = not self.help_on; self.msg = "help pane %s" % ("shown (PgUp/PgDn scroll it)" if self.help_on else "hidden")
            elif cmd == "/log" and len(parts) > 1 and parts[1].isdigit():
                self.log_rows = max(0, min(30, int(parts[1]))); self.msg = "server log: %d rows" % self.log_rows
            elif cmd == "/cols" and len(parts) > 1 and parts[1].isdigit():
                self.grid_cols = max(0, min(5, int(parts[1]))); self.msg = "client windows per row: %s" % (self.grid_cols or "auto")
            elif cmd == "/helpw" and len(parts) > 1 and parts[1].isdigit():
                self.help_w = max(20, min(60, int(parts[1]))); self.msg = "help pane width: %d" % self.help_w
            else:
                self.msg = "unknown lab command: %s" % cmd
            return
        if c is None:
            self.msg = "no client: /new first"; return
        self.send_line(c, text)

    # ---- walkthrough: the next step is shown first; Enter runs it ----
    def start_demo(self):
        want = ["alice", "bob", "carol"]
        have = dict((cl.nick, cl) for cl in self.clients)
        for n in want:
            if n not in have:
                self.new_client(n)
        self.demo = [(w, l.replace("DCCBOT", self.bot), n) for (w, l, n) in DEMO_STEPS]; self.demo_i = 0
        self.msg = "walkthrough: the next step is shown above this line. Enter runs it; type a line to send something else first; /skip /back /stop"

    def walk_bar(self):
        """One line describing the pending step, or '' when no walkthrough is running."""
        if self.demo is None:
            return ""
        if self.demo_i >= len(self.demo):
            return "WALK  done (%d steps).  /stop to close, /back to repeat the last one" % len(self.demo)
        who, line, what = self.demo[self.demo_i]
        return "WALK %d/%d  next:  %s > %s     %s" % (self.demo_i + 1, len(self.demo), who, line, what)

    def demo_step(self):
        if self.demo_i >= len(self.demo):
            self.msg = "walkthrough finished (/stop to close it)"; return
        who, line, what = self.demo[self.demo_i]; self.demo_i += 1
        c = next((cl for cl in self.clients if cl.nick == who), None)
        if c is None:
            self.msg = "walkthrough: no client %s" % who; return
        self.active = c.idx; self.focus_request = c.idx
        if line == "@dcc":
            time.sleep(0.3); self.pump(); self.dcc_receive(c)
        else:
            self.send_line(c, line)
        self.msg = "ran %d/%d  %s > %s" % (self.demo_i, len(self.demo), who, line)

    # ---- UI ----
    def run(self, stdscr):
        curses.curs_set(1); stdscr.nodelay(True); stdscr.timeout(120); stdscr.keypad(True)
        curses.start_color(); curses.use_default_colors()
        many = curses.COLORS >= 256
        # colour pairs: 1 sent, 2 received, 3 error/red, 4 frame, 5 note, 6 focused frame, 7 title, 8 dim
        pal = [(1, 214 if many else curses.COLOR_YELLOW), (2, 114 if many else curses.COLOR_GREEN),
               (3, 203 if many else curses.COLOR_RED), (4, 67 if many else curses.COLOR_BLUE),
               (5, 176 if many else curses.COLOR_MAGENTA), (6, 81 if many else curses.COLOR_CYAN),
               (7, 223 if many else curses.COLOR_WHITE), (8, 245 if many else curses.COLOR_WHITE)]
        for i, col in pal:
            curses.init_pair(i, col, -1)
        C_SENT, C_RECV, C_R, C_FRAME, C_NOTE, C_FOCUS, C_TITLE, C_DIM = [curses.color_pair(i) for i in range(1, 9)]
        GRAD = [curses.color_pair(i) for i in (4, 6, 5, 2, 1)]     # one frame colour per client pane
        BOLD = curses.A_BOLD
        G = glyphs()
        last_q = 0
        if not self.clients:
            for n in self.start_nicks:
                self.new_client(n)
            self.active = 0
            self.msg = ("%d clients registered (%s). Tab moves between panes and picks the client you type to; "
                        "the last Tab stop is the menu bar; /walk for the walkthrough"
                        % (len(self.clients), ", ".join(c.nick for c in self.clients)))
        # focus: 0..n-1 client panes, LOG, HELP, MENU (the last three are constants below n is added)
        LOG, HELP, MENU = "log", "help", "menu"
        focus = 0
        self.hscroll_log = 0; self.help_h = 0
        # ---- view options, edited from the menu bar (same keys as the monitor) ----
        SHOWS = ("all", "sent", "received", "notes")
        SORTS = ("number", "nick", "unread")
        LOGFS = ("all", "in", "out", "errors")
        opts = {"show": 0, "sort": 0, "logf": 0}
        MENU_ITEMS = [  # (key, label, kind)
            ("show", "windows show", "cycle"), ("sort", "order", "cycle"), ("cols", "per row", "int"),
            ("log", "log rows", "int"), ("logf", "log shows", "cycle"), ("help", "help", "toggle"),
            ("eol", "eol", "cycle"), ("walk", "walkthrough", "action"), ("new", "new client", "action"),
            ("quit", "quit", "action"),
        ]
        menu_i = 0

        def menu_value(key):
            if key == "show":
                return SHOWS[opts["show"]]
            if key == "sort":
                return SORTS[opts["sort"]]
            if key == "cols":
                return str(self.grid_cols) if self.grid_cols else "auto"
            if key == "log":
                return str(self.log_rows) if self.log_rows else "off"
            if key == "logf":
                return LOGFS[opts["logf"]]
            if key == "help":
                return "on" if self.help_on else "off"
            if key == "eol":
                return self.eol
            if key == "walk":
                return "next" if self.demo is not None else "start"
            return ""

        def visible_panes():
            v = list(range(len(self.clients)))
            if self.log_rows:
                v.append(LOG)
            if self.help_on:
                v.append(HELP)
            v.append(MENU)
            return v

        def put(y, x, s, attr=0):
            h, w = stdscr.getmaxyx()
            if 0 <= y < h and 0 <= x < w:
                lim = w - x - (1 if y == h - 1 else 0)
                try:
                    stdscr.addstr(y, x, s[: max(0, lim)], attr)
                except curses.error:
                    pass

        def box(y, x, hh, ww, title, attr, focused):
            if hh < 2 or ww < 4:
                return (y + 1, x + 1, max(0, hh - 2), max(0, ww - 2))
            a = (C_FOCUS | BOLD) if focused else attr
            put(y, x, G["tl"] + G["h"] * (ww - 2) + G["tr"], a)
            for i in range(1, hh - 1):
                put(y + i, x, G["v"], a); put(y + i, x + ww - 1, G["v"], a)
            put(y + hh - 1, x, G["bl"] + G["h"] * (ww - 2) + G["br"], a)
            put(y, x + 2, (" %s " % title)[: ww - 4], (curses.A_REVERSE | BOLD) if focused else (a | BOLD))
            return (y + 1, x + 1, hh - 2, ww - 2)

        while not self.quit:
            self.pump()
            if time.time() - last_q > 1.0:
                last_q = time.time()
                q = kernel_sendq(self.args.port)
                for c in self.clients:
                    try:
                        c.sendq = q.get("%s:%d" % c.sock.getsockname(), 0) if c.sock else 0
                    except OSError:
                        pass
            h, w = stdscr.getmaxyx(); stdscr.erase()
            srv = REPORT.server
            rss = ps_stats(srv.proc.pid)[0] if (srv and srv.proc) else 0
            n = len(self.clients)
            fr = getattr(self, "focus_request", None)
            if fr is not None:
                focus = fr; self.focus_request = None
            vis = visible_panes()
            if focus not in vis:
                focus = vis[0]
            if isinstance(focus, int):
                self.active = focus
            act = self.clients[self.active] if self.clients else None
            fname = {LOG: "server log", HELP: "help", MENU: "menu"}.get(focus, self.clients[focus].nick if isinstance(focus, int) and focus < n else "-")
            put(0, 0, (" LAB   server 127.0.0.1:%s  pid %s  RSS %d MB   clients %d   typing goes to: %s   focus: %s" % (
                self.args.port, srv.proc.pid if (srv and srv.proc) else "-", rss, n,
                act.nick if act else "-", fname)).ljust(w - 1), curses.A_REVERSE | BOLD)
            # ---- menu bar (row 1) ----
            x = 0
            put(1, 0, " " * (w - 1), C_DIM)
            for i, (key, label, kind) in enumerate(MENU_ITEMS):
                val = menu_value(key)
                txt = " %s%s " % (label, (": " + val) if val else "")
                sel = (focus == MENU and i == menu_i)
                put(1, x, txt, (curses.A_REVERSE | BOLD) if sel else (C_FOCUS if focus == MENU else C_DIM))
                x += len(txt) + 1
            tail = ("  %s/%s pick   Enter or Up/Down change   b back" % (G["inn"], G["out"])) if focus == MENU else "  Tab: panes, log, help, menu"
            put(1, min(x, max(0, w - len(tail) - 1)), tail, C_SENT if focus == MENU else C_DIM)

            top = 2
            walk = self.walk_bar()
            bottom = 2 + (1 if walk else 0)          # msg + input (+ walk bar)
            # ---- help pane (left) ----
            hw = self.help_w if self.help_on else 0
            log_rows = self.log_rows
            if self.help_on:
                hh_ = h - top - bottom - (log_rows + 2 if log_rows else 0)
                iy, ix, ih, iw = box(top, 0, hh_, hw, "HELP", C_FRAME, focus == HELP)
                self.help_scroll = max(0, min(self.help_scroll, max(0, len(LAB_HELP) - ih)))
                for i, (k, d) in enumerate(LAB_HELP[self.help_scroll: self.help_scroll + ih]):
                    if d is None:
                        put(iy + i, ix, k[hw and self.help_h:][: iw], C_TITLE | BOLD if k else 0)
                    else:
                        line = ("%-22s %s" % (k, d))[self.help_h:]
                        put(iy + i, ix, line[:iw], C_DIM)
                        put(iy + i, ix, line[: min(iw, max(0, 22 - self.help_h))], C_RECV)
            # ---- client panes (grid, right), in the chosen order ----
            gx = hw; gw = w - gx; nn = max(1, n)
            cols = self.grid_cols or (3 if gw >= 105 else (2 if gw >= 70 else 1))
            cols = max(1, min(nn, cols)); rws = (nn + cols - 1) // cols
            area_h = h - top - bottom - (log_rows + 2 if log_rows else 0); win_h = max(4, area_h // rws); win_w = gw // cols
            order = list(self.clients)
            if SORTS[opts["sort"]] == "nick":
                order.sort(key=lambda cl: cl.nick.lower())
            elif SORTS[opts["sort"]] == "unread":
                order.sort(key=lambda cl: -cl.sendq)
            show = SHOWS[opts["show"]]
            for pos, c in enumerate(order):
                r, col = divmod(pos, cols)
                x = gx + col * win_w; y = top + r * win_h
                ww = (w - x) if col == cols - 1 else win_w
                title = "%d %s  sent %dB  unread-by-it %s%s%s%s" % (
                    c.idx + 1, c.nick, c.last_len, human_bytes(c.sendq),
                    "  PAUSED" if c.paused else "", "  CLOSED" if c.closed else "",
                    "  <- typing here" if c.idx == self.active else "")
                frame = C_R if (c.sendq >= 32768 or c.closed) else GRAD[c.idx % len(GRAD)]
                iy, ix, ih, iw = box(y, x, win_h, ww, title, frame, focus == c.idx)
                lines = [(d, t) for (d, t) in c.lines
                         if show == "all" or (show == "sent" and d == ">>") or (show == "received" and d == "<<")
                         or (show == "notes" and d not in (">>", "<<"))]
                rows = [(d, ("%s %s" % ({">>": G["out"], "<<": G["inn"]}.get(d, G["dot"]), t))) for (d, t) in lines]
                c.scroll = max(0, min(c.scroll, max(0, len(rows) - ih)))
                shown = rows[max(0, len(rows) - c.scroll - ih): max(0, len(rows) - c.scroll)]
                for i, (d, text) in enumerate(shown):
                    attr = C_SENT if d == ">>" else (C_RECV if d == "<<" else C_NOTE)
                    put(iy + i, ix, text[c.hscroll: c.hscroll + iw].ljust(iw), attr)
                if c.scroll:
                    put(y + win_h - 1, x + ww - 14, " %s%d more " % (G["up"], c.scroll), C_DIM)
            # ---- server log (bottom) ----
            if log_rows:
                ly = h - log_rows - 2 - bottom
                lf = LOGFS[opts["logf"]]
                iy, ix, ih, iw = box(ly, 0, log_rows + 2, w, "SERVER LOG%s" % ("" if lf == "all" else "  (%s only)" % lf), C_FRAME, focus == LOG)
                if srv:
                    with srv.lock:
                        entries = list(srv.buffer)
                    if lf != "all":
                        entries = [(tt, txt) for (tt, txt) in entries
                                   if (lf == "in" and "from fd" in txt.lower()) or (lf == "out" and "sendto fd" in txt.lower())
                                   or (lf == "errors" and ("error" in txt.lower() or "closing" in txt.lower()))]
                    self.log_scroll = max(0, min(self.log_scroll, max(0, len(entries) - ih)))
                    end = len(entries) - self.log_scroll
                    for i, (tt, txt) in enumerate(entries[max(0, end - ih): max(0, end)]):
                        low = txt.lower()
                        attr = C_RECV if "from fd" in low else (C_SENT if "sendto fd" in low else (C_R if ("error" in low or "closing" in low) else C_DIM))
                        line = printable("%s %s" % (time.strftime("%H:%M:%S", time.localtime(tt)), re.sub(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \S+\s+", "", txt)))
                        put(iy + i, ix, line[self.hscroll_log: self.hscroll_log + iw].ljust(iw), attr)
            # ---- walk bar + message + input ----
            if walk:
                put(h - 3, 0, walk[: w - 1].ljust(w - 1), C_FOCUS | BOLD)
            put(h - 2, 0, (self.msg or "")[: w - 1].ljust(w - 1), C_NOTE | BOLD)
            prompt = "[%s]> " % (act.nick if act else "-")
            counter = " len=%d (+%d eol) " % (len(self.input), len(self.eol_bytes()))
            put(h - 1, 0, (prompt + self.input).ljust(w - 1), curses.A_REVERSE)
            put(h - 1, max(0, w - len(counter) - 1), counter, curses.A_REVERSE | C_SENT)
            try:
                stdscr.move(h - 1, min(w - 2, len(prompt) + len(self.input)))
            except curses.error:
                pass
            stdscr.refresh()
            try:
                k = stdscr.getch()
            except curses.error:
                k = -1
            if k == -1:
                continue
            if k == 27:                                  # raw ESC sequence (terminal not in keypad mode)
                stdscr.nodelay(True)
                seq = ""
                for _ in range(3):
                    try:
                        ch = stdscr.getch()
                    except curses.error:
                        ch = -1
                    if ch == -1:
                        break
                    seq += chr(ch) if 0 <= ch < 256 else ""
                stdscr.timeout(120)
                k = {"[D": curses.KEY_LEFT, "OD": curses.KEY_LEFT, "[C": curses.KEY_RIGHT, "OC": curses.KEY_RIGHT,
                     "[A": curses.KEY_UP, "OA": curses.KEY_UP, "[B": curses.KEY_DOWN, "OB": curses.KEY_DOWN,
                     "[Z": curses.KEY_BTAB, "[5~": curses.KEY_PPAGE, "[6~": curses.KEY_NPAGE}.get(seq, 27 if not seq else -1)
                if k == -1:
                    continue
            if k == 4:                                   # Ctrl-D
                self.quit = True; continue
            vis = visible_panes()
            if k == 9:                                   # Tab: next visible pane (clients, log, help, menu)
                focus = vis[(vis.index(focus) + 1) % len(vis)]; continue
            if k == curses.KEY_BTAB:                     # Shift-Tab: previous
                focus = vis[(vis.index(focus) - 1) % len(vis)]; continue
            if focus == MENU:                            # menu bar: Left/Right pick, Enter/Up/Down change, b/Esc back
                key, label, kind = MENU_ITEMS[menu_i]
                if k == curses.KEY_LEFT:
                    menu_i = (menu_i - 1) % len(MENU_ITEMS)
                elif k == curses.KEY_RIGHT:
                    menu_i = (menu_i + 1) % len(MENU_ITEMS)
                elif k in (ord("b"), ord("B"), 27):
                    focus = vis[0]
                elif k in (10, 13, curses.KEY_ENTER, curses.KEY_UP, curses.KEY_DOWN, ord(" ")):
                    step = -1 if k == curses.KEY_DOWN else 1
                    if key in ("show", "sort", "logf"):
                        size = {"show": len(SHOWS), "sort": len(SORTS), "logf": len(LOGFS)}[key]
                        opts[key] = (opts[key] + step) % size
                    elif key == "cols":
                        self.grid_cols = (self.grid_cols + step) % 6
                    elif key == "log":
                        self.log_rows = max(0, min(20, self.log_rows + 4 * step))
                    elif key == "help":
                        self.help_on = not self.help_on
                    elif key == "eol":
                        e = ("crlf", "lf", "none"); self.eol = e[(e.index(self.eol) + step) % 3]
                    elif key == "walk":
                        if self.demo is None:
                            self.start_demo()
                        else:
                            self.demo_step()
                        self.focus_request = None; focus = MENU
                    elif key == "new":
                        used = set(cl.nick for cl in self.clients)
                        self.new_client(next((nk for nk in NICKS if nk not in used), None)); focus = MENU; self.focus_request = None
                    elif key == "quit":
                        self.quit = True
                continue
            if k in (10, 13, curses.KEY_ENTER):
                text = self.input; self.input = ""; self.msg = ""
                self.handle(text)
            elif k in (curses.KEY_BACKSPACE, 127, 8):
                self.input = self.input[:-1]
            elif k == 12:                                # Ctrl-L: cycle layouts
                if self.help_on and self.log_rows:
                    self.help_on = False; self.log_rows = 0; self.msg = "layout: client windows only (Ctrl-L again to cycle)"
                elif not self.help_on and not self.log_rows:
                    self.help_on = True; self.msg = "layout: help + windows"
                else:
                    self.help_on = True; self.log_rows = 8; self.msg = "layout: help + windows + server log"
            elif k in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE, curses.KEY_LEFT, curses.KEY_RIGHT):
                step = {curses.KEY_UP: 1, curses.KEY_DOWN: -1, curses.KEY_PPAGE: 10, curses.KEY_NPAGE: -10}.get(k, 0)
                hstep = {curses.KEY_LEFT: -8, curses.KEY_RIGHT: 8}.get(k, 0)
                if isinstance(focus, int):
                    c = self.clients[focus]
                    c.scroll = max(0, c.scroll + step); c.hscroll = max(0, c.hscroll + hstep)
                elif focus == LOG:
                    self.log_scroll = max(0, self.log_scroll + step); self.hscroll_log = max(0, self.hscroll_log + hstep)
                else:
                    self.help_scroll = max(0, self.help_scroll - step); self.help_h = max(0, self.help_h + hstep)
            elif k == curses.KEY_RESIZE:
                pass
            elif 32 <= k <= 126:
                self.input += chr(k)
        for c in self.clients:
            try:
                if c.sock:
                    c.sock.close()
            except OSError:
                pass


NICKS = ("alice", "bob", "carol", "dave", "eve")


def nc_crlf_flag():
    """The flag that makes this nc send CRLF line endings: -c on macOS/BSD, -C on Linux (OpenBSD nc)."""
    try:
        h = subprocess.run(["nc", "-h"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout.decode(errors="ignore")
    except OSError:
        return ""
    for line in h.splitlines():
        if "CRLF" in line:
            m = re.search(r"-(c|C)\b", line)
            if m:
                return "-" + m.group(1)
    return "-c" if sys.platform == "darwin" else "-C"


def client_command(kind, nick, host, port, password):
    """Shell command for one client window: irssi already connected, or nc already registered."""
    if kind == "irssi":
        return "irssi -c %s -p %d -w %s -n %s" % (host, port, password, nick)
    flag = nc_crlf_flag()
    reg = "PASS %s\\r\\nNICK %s\\r\\nUSER %s 0 * :%s\\r\\n" % (password, nick, nick, nick)
    # registration is sent first, then whatever you type goes to the server as-is
    return ("printf '== %s: registered as %s, type raw IRC lines (JOIN #x, PRIVMSG bob :hi) ==\\n'; "
            "(printf '%s'; cat) | nc %s %s %d" % (nick, nick, reg, flag, host, port))


def open_client_windows(kind, nicks, host, port, password, layout):
    """Open one client per nick. layout: 'tmux' = panes in one tmux session in this terminal
    (blocks until you detach with Ctrl-b d or close the panes); 'windows' = separate terminal
    windows, tiled on macOS. Returns a status line."""
    cmds = [client_command(kind, n, host, port, password) for n in nicks]
    if layout == "tmux":
        sess = "ftirc"
        subprocess.call(["tmux", "kill-session", "-t", sess], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.check_call(["tmux", "new-session", "-d", "-s", sess, "-n", kind, "bash -c %s" % shlex.quote(cmds[0])])
        for c in cmds[1:]:
            subprocess.check_call(["tmux", "split-window", "-t", sess, "bash -c %s" % shlex.quote(c)])
        subprocess.call(["tmux", "select-layout", "-t", sess, "tiled"])
        subprocess.call(["tmux", "set-option", "-t", sess, "pane-border-status", "top"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for i, n in enumerate(nicks):
            subprocess.call(["tmux", "select-pane", "-t", "%s:0.%d" % (sess, i), "-T", n], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.call(["tmux", "select-pane", "-t", "%s:0.0" % sess])
        print("%s%d %s pane(s) in one tmux window: Ctrl-b + arrow = move between panes, Ctrl-b z = zoom one, Ctrl-b d = detach (comes back here).%s"
              % (DIM, len(nicks), kind, RESET))
        subprocess.call(["tmux", "attach", "-t", sess])
        return "tmux session ended (%s)" % kind
    opened = 0
    for i, c in enumerate(cmds):
        try:
            if sys.platform == "darwin":
                # tile: two columns, rows of 380 px, from the top-left of the main screen
                col, row = i % 2, i // 2
                x1, y1 = 20 + col * 700, 40 + row * 380
                script = ('tell application "Terminal"\n activate\n set w to do script "%s"\n'
                          ' set bounds of front window to {%d, %d, %d, %d}\n end tell' % (c.replace('"', '\\"'), x1, y1, x1 + 680, y1 + 360))
                subprocess.Popen(["osascript", "-e", script], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                opened += 1
            else:
                for term in (["gnome-terminal", "--"], ["konsole", "-e"], ["xfce4-terminal", "-e"], ["x-terminal-emulator", "-e"], ["xterm", "-e"]):
                    try:
                        subprocess.Popen(term + ["bash", "-c", c], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        opened += 1; break
                    except OSError:
                        continue
        except OSError:
            pass
    return "opened %d/%d %s window(s): %s" % (opened, len(cmds), kind, ", ".join(nicks))


def have(prog):
    return any(os.access(os.path.join(d, prog), os.X_OK) for d in os.getenv("PATH", "").split(os.pathsep))


CHEATSHEET = """
Cheat-sheet commands
  irssi (Alt-1 = status window, Alt-2.. = channel/query windows)
    /connect <host> <port> <password> <nick>       or start it: irssi -c <host> -p <port> -w <password> -n <nick>
    /join #chan                 /part #chan [msg]           /topic #chan <text>
    /msg <nick> <text>          /notice <nick> <text>       /me waves
    /mode #chan +i              /mode #chan +t              /mode #chan +k <key>        /mode #chan +l <n>
    /mode #chan +o <nick>       /kick #chan <nick> [why]    /invite <nick> #chan
    /msg DCCBot !help           /msg DCCBot !send <nick> <file>                         /msg DCCBot !accept
    /rawlog open x.log          (writes every wire line)    /quit [msg]
  nc (raw IRC, one command per line; the window is already registered)
    JOIN #chan                  PART #chan :msg             TOPIC #chan :text
    PRIVMSG <nick> :text        PRIVMSG #chan :text         NOTICE <nick> :text
    MODE #chan +i               MODE #chan +t               MODE #chan +k key           MODE #chan +l 3
    MODE #chan +o <nick>        KICK #chan <nick> :why      INVITE <nick> #chan
    PRIVMSG DCCBot :!help       PRIVMSG DCCBot :!send <nick> <file>                     PRIVMSG DCCBot :!accept
    PING :x                     QUIT :bye
"""

# ===========================================================================
# Terminal chrome, menu, main
# ===========================================================================

def intro(args):
    if args.no_intro:
        return
    if sys.stdout.isatty():
        os.system("clear")
    print("%s%s%s%s" % (BOLD, BLUE, FT_BANNER, RESET))
    print("%s%sInspectable ft_irc tester -- mandatory, robustness, load, bonus, lab.%s  %s(inspired by the black-wolf style testers)%s" % (BOLD, CYAN, RESET, DIM, RESET))


def banner(args):
    print("%s%s%s" % (MAGENTA, "-" * 62, RESET)); print("%sft_irc tester%s" % (BOLD, RESET))
    print("%smandatory%s %s" % (DIM, RESET, args.binary)); print("%sbonus    %s %s" % (DIM, RESET, args.bonus_binary))
    print("%shost     %s %s   %spassword%s %s" % (DIM, RESET, args.host, DIM, RESET, args.password))
    print("%sprofile  %s %s   %s(--show-config to see all)%s" % (DIM, RESET, profile_summary(), DIM, RESET))
    print("%soutput   %s %s" % (DIM, RESET, args.outdir)); print("%s%s%s" % (MAGENTA, "-" * 62, RESET))


def print_summary():
    t = REPORT.totals(); tot = sum(t.values()); print()
    print("%s%s%s" % (MAGENTA, "-" * 62, RESET))
    print("  Results: %s%d pass%s / %s%d fail%s / %s%d notes%s / %s%d info%s / %s%d skip%s / %d total"
          % (GREEN, t[PASS], RESET, RED, t[FAIL], RESET, YELLOW, t[NOTE], RESET, DIM, t[INFO], RESET, DIM, t[SKIP], RESET, tot))
    if t[FAIL] == 0:
        print("  %s%sNo failures.%s %sNOTE = behaviour the subject leaves open, INFO = a measurement.%s" % (GREEN, BOLD, RESET, DIM, RESET))
    else:
        print("  %s%s%d real failure(s) -- see the report.%s" % (RED, BOLD, t[FAIL], RESET))
    print("%s%s%s" % (MAGENTA, "-" * 62, RESET))


def raw_terminal():
    print("\n%s%s[ Raw IRC terminal ]%s  type commands; /quit to leave\n" % (BOLD, CYAN, RESET))
    nick = input("Nick [blackwolf]: ").strip() or "blackwolf"
    c = connect(); ok, buf = register(c, nick)
    if buf:
        print("%s%s%s" % (DIM, buf.rstrip(), RESET))
    try:
        while True:
            ready, _, _ = select.select([c, sys.stdin], [], [], 0.2)
            if PENDING.get(c):
                print("%s%s%s" % (CYAN, PENDING.pop(c).rstrip(), RESET))
            if c in ready:
                data = c.recv(4096)
                if not data:
                    print("\n%sServer closed the connection.%s" % (RED, RESET)); break
                print("%s%s%s" % (CYAN, data.decode(errors="ignore").rstrip(), RESET))
            if sys.stdin in ready:
                line = sys.stdin.readline()
                if not line:
                    break
                line = line.rstrip("\n")
                if line == "/quit":
                    break
                send(c, line)
    finally:
        safe_close(c)


def choose_mode():
    print("\n%s%sChoose mode%s" % (BOLD, CYAN, RESET))
    for k, lbl, desc in (("1", "mandatory", "subject command set + numerics"),
                         ("2", "robustness", "framing, limits, slow readers, churn, many clients"),
                         ("3", "load", "slow-reader load: N clients reading 1 byte at a time"),
                         ("4", "bonus", "DCC bot + file transfer (needs the bonus binary)"),
                         ("5", "all", "everything + with/no-bot capture"),
                         ("6", "compare", "with-bot vs no-bot capture only"),
                         ("7", "lab", "interactive: up to 5 client windows + server log + help + guided demo"),
                         ("8", "clients", "N irssi or nc client windows on a launched server (tmux panes or separate windows)"),
                         ("9", "profile", "show the implementation profile, then edit it if you want"),
                         ("0", "quit", "")):
        print("  %s%s%s  %-12s %s" % (GREEN, k, RESET, lbl, desc))
    print("  %sadd 'live' to a mode number to watch it run, e.g. '5 live'%s" % (DIM, RESET))
    ans = ask("\n%sSelect [default: 5 = all]: %s" % (BOLD, RESET), "0").strip().lower() or "5"
    live = ans.endswith("live"); ans = ans.replace("live", "").strip() or "5"
    m = {"1": "mandatory", "2": "robustness", "3": "load", "4": "bonus", "5": "all", "6": "compare",
         "7": "lab", "8": "clients", "9": "profile", "0": "quit"}.get(ans, ans)
    return (m, live)


def parse_args():
    p = argparse.ArgumentParser(description="Inspectable ft_irc tester.", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("--host", default=os.getenv("IRC_TEST_HOST", "127.0.0.1"))
    p.add_argument("--port", type=int, default=int(os.getenv("IRC_TEST_PORT", "6667")), help="the server is always started on this port")
    p.add_argument("--password", default=os.getenv("IRC_TEST_PASSWORD", "pass"))
    p.add_argument("--binary", default=os.getenv("IRC_TEST_BINARY", "../../ircserv"), help="mandatory server binary")
    p.add_argument("--bonus-binary", dest="bonus_binary", default=os.getenv("IRC_TEST_BONUS_BINARY", "../../ircserv_bonus"))
    p.add_argument("--mode", choices=("menu", "mandatory", "robustness", "load", "bonus", "all", "compare", "lab", "irssi", "clients", "nc", "terminal"), default="menu")
    p.add_argument("--live", action="store_true", help="watch the server window and client windows while the suite runs")
    p.add_argument("--step", action="store_true", help="with --live: pause after every scenario (Enter = next, c = continue)")
    p.add_argument("--no-start", action="store_true", help="use an already-running server on --host/--port")
    p.add_argument("--no-intro", action="store_true"); p.add_argument("--fast", action="store_true")
    p.add_argument("--debug", action="store_true", help="print every line sent/received live")
    p.add_argument("--outdir", default="artifacts"); p.add_argument("--report", default=None)
    p.add_argument("--no-html", action="store_true")
    p.add_argument("--config", default="tester.conf", help="profile file (key=value lines)")
    p.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="override a profile option (repeatable)")
    p.add_argument("--show-config", action="store_true", help="print the implementation profile and exit")
    # legacy flags accepted silently
    p.add_argument("--clients", type=int, default=None, help=argparse.SUPPRESS)
    p.add_argument("--bot-nick", dest="bot_nick", default=None, help=argparse.SUPPRESS)
    p.add_argument("--timeout", type=float, default=None, help=argparse.SUPPRESS)
    p.add_argument("--server-log", default=None, help=argparse.SUPPRESS)
    return p.parse_args()


def run_mode(mode, args):
    """Run one mode to completion; returns True if a report was produced."""
    global TIMEOUT
    extra_files = []
    real_stdout = sys.stdout
    if mode == "lab":
        if not (HAVE_CURSES and sys.stdout.isatty()):
            print("the lab needs a terminal (curses)."); return False
        n = ask("How many clients to open (1-5) [3]: ", "3").strip() or "3"
        n = max(1, min(5, int(n))) if n.isdigit() else 3
        names = ask("Nicks, comma separated [%s]: " % ",".join(NICKS[:n]), "").strip()
        nicks = [x.strip() for x in names.split(",") if x.strip()][:n] if names else list(NICKS[:n])
        while len(nicks) < n:
            nicks.append(NICKS[len(nicks)])
        srv = None if args.no_start else start_phase_server(args.binary)
        lab = Lab(args, nicks)
        try:
            curses.wrapper(lab.run)
        finally:
            stop_phase_server(srv)
        return False
    if mode in ("irssi", "nc", "clients"):
        kind = mode if mode != "clients" else ""
        if not kind:
            kind = ask("Client program: 1) irssi  2) nc (raw IRC, already registered)  [1]: ", "1").strip()
            kind = "nc" if kind in ("2", "nc") else "irssi"
        if not have(kind):
            print("%s%s is not installed (brew install %s / apt install %s)%s" % (RED, kind, kind, kind, RESET)); return False
        n = ask("How many %s clients (1-5) [3]: " % kind, "3").strip() or "3"
        n = max(1, min(5, int(n))) if n.isdigit() else 3
        names = ask("Nicks, comma separated [%s]: " % ",".join(NICKS[:n]), "").strip()
        nicks = [x.strip() for x in names.split(",") if x.strip()][:n] if names else list(NICKS[:n])
        while len(nicks) < n:
            nicks.append(NICKS[len(nicks)])
        if have("tmux"):
            lay = ask("Layout: 1) panes in this terminal (tmux)  2) separate windows  [1]: ", "1").strip()
            layout = "windows" if lay == "2" else "tmux"
        else:
            layout = "windows"; print("%s(tmux not installed: opening separate windows; brew install tmux gives panes in one terminal)%s" % (DIM, RESET))
        print(CHEATSHEET)
        srv = None if args.no_start else start_phase_server(args.binary)
        print(open_client_windows(kind, nicks, args.host, args.port, args.password, layout))
        if srv is not None:
            if layout == "windows":
                print("Server running on port %d for the %s windows. Press Enter here to stop it." % (args.port, kind))
                ask("")
            stop_phase_server(srv)
        return False
    try:
        if args.live and HAVE_CURSES and sys.stdout.isatty():
            sys.stdout = io.StringIO()
            ui = LiveUI(mode, extra_files)
            REPORT.step = bool(args.step)
            try:
                curses.wrapper(ui.run)
            finally:
                sys.stdout = real_stdout
            if ui.error:
                print("%s[live] %s%s" % (RED, ui.error, RESET))
        else:
            run_everything(mode, extra_files)
    except KeyboardInterrupt:
        sys.stdout = real_stdout; print("\nInterrupted.")
    finally:
        sys.stdout = real_stdout
        if REPORT.server is not None:
            stop_phase_server(REPORT.server)
        cleanup_sample_file()
    print_summary()
    write_transcripts(args.outdir)
    extra_files.append(("artifacts/transcripts/", "raw per-scenario screens (client + server)"))
    if not args.no_html:
        write_html(args.report, extra_files)
        try:
            import pathlib
            uri = pathlib.Path(os.path.abspath(args.report)).as_uri()
        except Exception:
            uri = os.path.abspath(args.report)
        print("\n%sHTML report:%s %s  %s(Cmd-click / Ctrl-click, or: make open)%s" % (BOLD, RESET, uri, DIM, RESET))
    print("%sTranscripts:%s %s" % (BOLD, RESET, os.path.join(args.outdir, "transcripts")))
    return True


def main():
    global REPORT, TIMEOUT, PASSWORD
    args = parse_args()
    atexit.register(_kill_servers)
    for sig in (signal.SIGTERM, signal.SIGHUP):
        try:
            signal.signal(sig, _on_signal)
        except (OSError, ValueError, AttributeError):
            pass
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (OSError, ValueError, AttributeError):
        pass
    try:
        load_profile(args.config, args.set)
        if args.clients is not None:
            profile_set("clients", args.clients, "--clients")
        if args.bot_nick:
            profile_set("bot_nick", args.bot_nick, "--bot-nick")
        if args.timeout is not None:
            profile_set("timeout", args.timeout, "--timeout")
    except ValueError as e:
        print("profile error: %s" % e); sys.exit(2)
    if args.show_config:
        show_config(); return
    TIMEOUT = PROFILE["timeout"]; PASSWORD = args.password
    args.outdir = os.path.abspath(args.outdir); os.makedirs(args.outdir, exist_ok=True)
    if args.report is None:
        args.report = os.path.join(args.outdir, "report.html")

    intro(args); banner(args)
    if args.mode != "menu":
        REPORT = Report(args)
        try:
            run_mode("lab" if args.mode == "terminal" else args.mode, args)
        except KeyboardInterrupt:
            _kill_servers(); print("\n%sInterrupted; server stopped.%s" % (YELLOW, RESET))
        sys.exit(1 if REPORT.totals()[FAIL] else 0)

    # menu loop: come back here after every mode instead of quitting
    worst = 0
    while True:
        mode, live = choose_mode()
        if mode == "quit":
            break
        if mode == "profile":
            configure_interactive(); TIMEOUT = PROFILE["timeout"]; continue
        if mode not in ("mandatory", "robustness", "load", "bonus", "all", "compare", "lab", "irssi", "nc", "clients"):
            print("unknown choice"); continue
        args.live = live
        REPORT = Report(args)
        try:
            run_mode(mode, args)
        except KeyboardInterrupt:
            _kill_servers(); print("\n%sInterrupted; server stopped.%s" % (YELLOW, RESET))
        worst = max(worst, REPORT.totals()[FAIL])
        print("\n%s(back to the menu -- 0 to quit)%s" % (DIM, RESET))
    sys.exit(1 if worst else 0)


if __name__ == "__main__":
    main()
