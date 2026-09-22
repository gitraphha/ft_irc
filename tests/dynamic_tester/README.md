# Dynamic Tester

Starts the server on port 6667, drives it with real TCP clients, checks every reply against the
subject and RFC 2812, and writes one HTML report. Python 3.8 or newer, standard library only.
macOS and Linux.

## Run

From the repo root (the server is built first: `make`, `make bonus`):

```
make -C tests/dynamic_tester                menu; Enter runs everything
make -C tests/dynamic_tester mandatory      PASS NICK USER CAP, JOIN PART TOPIC PRIVMSG NOTICE KICK INVITE MODE, QUIT, PING
make -C tests/dynamic_tester robustness     split and batched commands, line endings, 510-byte limit, what precedes a close, slow and stalled readers, reconnects, 20 clients
make -C tests/dynamic_tester load           30 clients reading one byte at a time
make -C tests/dynamic_tester bonus          bot commands, file offer, the transfer, DCC relayed between clients
make -C tests/dynamic_tester compare        the same bot dialogue against ircserv_bonus and ircserv, saved to two files
make -C tests/dynamic_tester live           everything, with the server and client windows on screen while it runs
make -C tests/dynamic_tester debug          everything, every line sent and received printed
make -C tests/dynamic_tester open           open the report (macOS)
make -C tests/dynamic_tester clean          remove artifacts/
```

`PORT=<port> PASSWORD=<password>` after any target changes the server the tester starts.
`make -C tests/dynamic_tester external PORT=<port> PASSWORD=<password>` tests a server you
started yourself. Ctrl-C stops the run; the report is written for what ran.

## What you get

In the terminal, one line per check:

```
  [JOIN] join, names, topic, second member
  [PASS] 353 lists both members
  [FAIL] 366 ends the names list
        :irc.local 366 alice #x  <- the last lines received
        -> send "366 <nick> <channel> :End of NAMES list"
```

| tag | meaning |
|---|---|
| PASS / FAIL | a subject requirement |
| NOTE | behaviour the subject leaves open; the check says what the server did and what else is common |
| INFO | a measurement (latency, memory, bytes) |
| SKIP | the check could not run (binary missing, port busy) |


At the end: `Results: N pass / N fail / N notes / N info / N skip / N total` and the link to the report.

`artifacts/report.html` is one page: a summary table, then every scenario with its checks
(what was sent, what came back, what was expected and why), the client windows and the server
log for that scenario. Scenarios with a FAIL or NOTE are expanded and their windows are bordered
red or amber; click a scenario title to expand or collapse it. `artifacts/transcripts/<scenario>.txt`
has the raw client and server lines; `artifacts/server_*.log` the server output. All of
`artifacts/` is ignored by git.

## tester.conf

Where the subject leaves a choice (bare `\n` accepted or not, what an over-long line gets, what
happens to a client that never reads, which numeric MODE returns without an argument, ...),
`tester.conf` says what to expect. Every line is commented out and shows the default; uncomment
and edit to match the server. A wrong entry appears as NOTE or FAIL in the affected checks.
`make -C tests/dynamic_tester config` prints the values; `SET="key=value"` overrides one for a run.

## Lab

`make -C tests/dynamic_tester lab` asks how many clients and their nicks, registers them and
shows each as a pane, the help on the left, the server log at the bottom and one input line.

| keys | |
|---|---|
| Tab / Shift-Tab | next / previous pane; typed lines go to the focused client |
| Up/Down, Left/Right, PgUp/PgDn | scroll the focused pane |
| Ctrl-L | layout: help+log / windows only / help only |
| Enter | send the input line (empty Enter: next walkthrough step) |

| command | |
|---|---|
| `/new 2`, `/new dave,eve` | more clients |
| `/use N` | make client N active |
| `/msg <target> <text>`, `/join #chan` | PRIVMSG / JOIN |
| `/eol crlf\|lf\|none`, `/long 510`, `/noeol <text>`, `/raw <text>` | line endings, exact lengths, partial sends |
| `/pause`, `/resume` | the client stops and restarts reading |
| `/close` | close the active client abruptly |
| `/walk` | guided walkthrough, one step per Enter: join, topic, +t, +o, kick, +i and invite, +k, +l, bot, transfer |
| `/send bob <path>` then `/dcc` in bob's window | file transfer without the bot |
| `PRIVMSG DCCBot :!send bob <file>`, `PRIVMSG DCCBot :!accept`, then `/dcc` | file transfer with the bot |
| `/help`, `/cols N`, `/log N`, `/quit` | hide help, windows per row, log rows, leave |

Received files are saved under `artifacts/`.

## Clients

`make -C tests/dynamic_tester clients` (or `irssi` / `nc`) asks the program, the number of
clients and their nicks, then opens them as tmux panes in this terminal (Ctrl-b + arrow to
move, Ctrl-b z to zoom, Ctrl-b d to detach and stop the server) or as separate windows. nc
windows are registered already and take raw IRC lines. A cheat-sheet of commands is printed first.

## Monitor

`make -C tests/dynamic_tester monitor` starts the server and shows it live: CPU and memory
graphs, bytes and lines per second in and out, syscalls per second (strace on Linux, the server
log otherwise), connections and the poll set, probe latency, bot and transfer state, the
clients table (red = stalled), channels, the server log.

`make -C tests/dynamic_tester attach` shows a server that is already on port 6667, and waits for
one if there is none yet. Run it in a second terminal next to the tester: it follows every
server restart between suites. The monitor's own probe client (nick `irctop`) appears in the
clients table.

| keys | |
|---|---|
| Tab | clients -> log -> menu bar |
| Left/Right, Enter or Up/Down, b | in the menu bar: pick an option, change it, back |
| Up/Down, Left/Right | scroll the focused pane |
| q | quit |

Plain ASCII is used when the terminal's curses has no wide-character support.
