*This project has been created as part of the 42 curriculum by blnunez-, kmurugan, rhaas.*

# ft_irc - IRC Server

## Description

### 42 Project | Internet Relay Chat in C++98

### Objective
Build an IRC server in C++98 that handles multiple concurrent clients over TCP, implementing the core IRC protocol (registration, channels, modes, private messaging, and operator commands) over an event-driven sockets layer.

### Overview
**ft_irc** is a 42 project that teaches event-driven I/O, the poll multiplexing model, and the IRC protocol. The server listens on a given port and password, accepts multiple clients via non-blocking sockets, registers them with PASS/NICK/USER, and lets them join channels, exchange private and channel messages, and manage channels with KICK/INVITE/TOPIC/MODE and operator privileges.

---

## Instructions

### Compile & Run

```sh
# mandatory
make && ./ircserv <port> <password>

# bonus
make bonus && ./ircserv_bonus <port> <password>
```

**NOTE:** The server binds an IPv4 and an IPv6 listening socket (`IPV6_V6ONLY`) and multiplexes them with `poll`. Connect with any IRC client (e.g. `irssi`) or a raw `nc` session.

```sh
# connect with netcat and register
nc <host> <port>
PASS <password>
NICK <nick>
USER <user> 0 * :<realname>
JOIN #channel
```

Port must be in the range `1-65535`, a password must be supplied.\
Please note that you may need system admin permissions to use privileged ports (usually 1–1023).

---

## IRC Command & Message Reference

### Registration
- `PASS <password>` — authenticate during registration.
- `NICK <nickname>` — set/change the nick.
- `USER <user> <mode> <unused> <realname>` — complete registration credentials.

### Channels
- `JOIN <#channel> [<key>]` — join (or create) a channel, optional key for `+k`.
- `PART <#channel> [<msg>]` — leave a channel (deleted when empty).
- `KICK <#channel> <nick> [<msg>]` — remove a member (requires operator rank).
- `INVITE <nick> <#channel>` — invite a user (may require operator rank).
- `TOPIC <#channel> [<topic>]` — view/set the topic (may require operator rank).
- `NAMES <#channel>` — list current members.
- `MODE <#channel> [modes]` — view/set channel modes and user modes.

### Messaging
- `PRIVMSG <target> <message>` — send a message. The bonus bot answers PRIVMSG addressed to it.
- `NOTICE <target> <message>` — send a notice (no auto-reply / CTCP handling).

### Channel Modes
| Mode | Argument | Meaning |
|------|----------|---------|
| `i`  | no  | Invite-only channel (`JOIN` requires an invite) |
| `t`  | no  | Only operators may set the topic |
| `k`  | yes | Channel key (password) required to join |
| `o`  | yes | Grant/revoke operator privilege to a member |
| `l`  | yes | User limit — max concurrent members |

### Channel User Modes
`o` Operator · none

Modes act on the target `nick` and are shown as `@` for operator or none for regular users. Privileges are checked on every call to a command that needs elevated privileges (this may also depend on the state of a channel, e.g. whether changing the TOPIC is restricted or not).

### Other
- `PING`/`PONG` — keepalive and latency check.

---

## Bonus: DCC Bot & File Transfer

Built with `make bonus` → `./ircserv_bonus <port> <password>`.

### DCCBot

| Abbr.| Meaning   |
|------|---------------------------|
| CTCP | Client-to-client protocol |
| DDC  | Direct Client-to-Client   |

A bot named `DCCBot` (also reached as `bot` / `BOT` / `#bot`, or by sending a bot command to any channel) that intercepts `PRIVMSG`: bot commands are answered with `PRIVMSG`, CTCP queries with `NOTICE`.

| Command          | Action |
|------------------|--------|
| `!help`          | List the available bot commands |
| `!version`       | Report the bot/server version |
| `!whoami`        | Show the requester's nick |
| `!send <nick> <file>` | Start a DCC SEND offer of `files/<file>` |
| `!accept`        | Accept the pending transfer offer |
| `!reject`        | Reject the pending transfer offer |

CTCP queries (`\x01VERSION\x01`, `\x01PING <token>\x01`) sent to the bot via `PRIVMSG` are answered by the bot with `NOTICE`.

### File transfer (DCC SEND)

`!send` opens a listener on an OS-picked port and hands the target a CTCP `DCC SEND <file> 127.0.0.1 <port> <size>` line. The receiver replies with `!accept`, connects to the advertised address/port and receives the file in 1 KiB chunks. Only one transfer is active at a time (`bot busy` otherwise). Pending offers expire after 60 seconds. Offered files must live in a `files/` directory next to the server; filenames containing `/` or `..` are rejected.

---

## Clang tool commands

```sh
# clang-format all headers and sources
make format

# clang-tidy one file
make tidy FILE=srcs/<filename>

# clang-tidy on multiple files (filenames without spaces)
make tidy FILE="srcs/<a.cpp> srcs/<b.cpp>"

# clang-tidy every source and header
make tidy-all
```

NOTE: Review clang-tidy results and fix only what makes sense for this project.

---

## Key Challenges & Solutions

### Protocol-Correct Error Replies

**Challenge:** Every command can fail in several ways (wrong channel, no such nick, missing params, insufficient privileges) and each failure must produce the correct RFC 2812 numeric to the client.

**Solution:** A `Reply` module maps each numeric code to its required parameter layout and trailing text. A central `Error` exception carries a pre-built reply. Command processing throws `Error::reply(...)` on the first invalid condition and the server loop catches it once, formats `:server <code> <nick> <params> :<text>\r\n`, and sends it — guaranteeing exactly one well-formed reply per failure and keeping handlers free of repetitive error plumbing.

### Usage of `poll` and buffers

**Challenge:** Apart from setting up sockets correctly for receiving data streams, caching and handling of incomplete or overlong messages needed to be implemented. Additionally, a certain amount of fault tolerance was desired, e.g. for not disconnecting a client on the first too long message.

**Solution:** Before passing on any data to command parsers and processors, the validity of a full line is checked. No incomplete or overlong data is ever forwarded to the internal handlers. Additionally an error counter is installed separately for each client, which limits the amount of consecutive errors and is reset on every valid input.

### Channel Lifetime & Ownership

**Challenge:** Channels are created on join, destroyed when the last member parts, and must never hold a pointer to a freed `Client`.

**Solution:** `ChannelManager` owns `Channel` objects in a case-folded map and cleans up empty channels after PART/KICK/leaveAll. `MemberSet` stores `Client*` + `ModeSet`. `ClientManager::remove` first parts the client from every channel (via `ChannelManager::leaveAll`) before freeing the `Client`, so channel member vectors never dangle.

### Bonus: CTCP & DCC SEND through the Bot

**Challenge:** The CTCP mechanic used by DCC is barely documented anywhere. You can forward a CTCP by stripping the wrapping `\x01` bytes and re-sending what is inside. But to actually run a file transfer the server needs to hand the receiver a `\x01DCC SEND <file> <ip> <port> <size>\x01` line, open a listener the receiver can connect to, and stream the file. Wiring the two bonuses together (bot + transfer manager) with the bot as the middleman got complicated as the design evolved, and real clients silently abstract all of this away so there was no reference.

**Solution:** Make the bot the DCC sender. `!send` asks the `TransferManager` to validate the filename, open a listener on an OS-picked port, and return the offer port/size. The bot then builds the `DCC SEND ... 127.0.0.1 <port> <size>` CTCP and delivers it wrapped in `\x01` to the target over `PRIVMSG`. The receiver just replies `!accept` and connects to the advertised address/port — which means the whole flow works over a plain `nc` session, no IRC client needed. Using the bot to construct and send the CTCP also makes the query path symmetric: the same `PRIVMSG` interception that answers `!help`/`!version` carries the DCC offer, and CTCP responses go back out as `NOTICE`. Transfer state (offer → accept/reject → stream → expire after 60 s) stayed in `TransferManager`, while the bot is a dumb messenger.

---

## Resources

### IRC protocol
- [RFC 2810 - IRC: Architecture](https://rfc-editor.org/rfc/rfc2810)
- [RFC 2811 - IRC: Channel Management](https://rfc-editor.org/rfc/rfc2811)
- [RFC 2812 - IRC: Client Protocol](https://rfc-editor.org/rfc/rfc2812)
- [RFC 2813 - IRC: Server Protocol](https://rfc-editor.org/rfc/rfc2813)
- [ircgod](https://ircgod.com)
- [Model IRC client protocol](https://modern.ircdocs.horse)

### rfc reading tips
- [Tips for Reading RFCs](https://www.rfc-editor.org/series/rfc-tips/)

### Reference IRC client and server
- [Server: ergo chat (formerly Oragono)](https://github.com/ergochat/ergo)
- [Client: irssi](https://github.com/irssi/irssi)

### Sockets
- [Sockets & Networking](https://web.mit.edu/6.031/www/sp19/classes/23-sockets-networking/)
- [All about sockets](https://docs.oracle.com/javase/tutorial/networking/sockets/index.html)
- [CS107 Sockets Programming](https://web.stanford.edu/class/archive/cs/cs107/cs107.1238/lectures/20/)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) - What is a
	socket?, IP Addresses, structs, and Data Munging, Jumping from IPv4 to IPv6,
	System Calls or Bust

### DCC protocol
- [Direct Client-to-Client protocol (DCC)](https://modern.ircdocs.horse/dcc.html)

### Use of AI

- Learning and verification: Clarifying IRC numeric replies, and the RFC 2812 grammar.
- Documentation: Proofreading and refining the README and project descriptions.
- Testing and debugging: Generating protocol-level test scenarios and reviewing edge cases.

---
