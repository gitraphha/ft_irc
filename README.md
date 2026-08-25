*This project has been created as part of the 42 curriculum by blnunez-, kmurugan, rhaas.*

# ft_irc - IRC server

## Description

### 42 Project | Internet Relay Chat in C++98

### Objective
Build an IRC server in C++98 that implements...


### Overview
...

---

## Instructions

### Requirements

-

### Compile & Run

```sh
# mandatory
make && ./ircserv <port> <password>

# bonus
make bonus && ./ircserv_bonus <port> <password>
```

---

## Clang tool commands

```sh
# Run clang-format before PR (use clang-format version 21)
make format
```

```sh
# Run clang-tidy on individual file
make tidy FILE=srcs/<filename>

# Run clang-tidy on multiple files (filenames without spaces)
make tidy FILE="srcs/<a.cpp> srcs/<b.cpp>"

# Run clang-tidy on all .cpp/.hpp under srcs and includes
make tidy-all
```

NOTE: Review clang-tidy results and fix only what makes sense for this project.

---

## Key Challenges & Solutions

### Topic

**Challenge:** 

**Solution:** 

---

## Resources

### IRC protocol
[RFC 2810 - IRC: Architecture](https://rfc-editor.org/rfc/rfc2810)
[RFC 2811 - IRC: Channel Management](https://rfc-editor.org/rfc/rfc2811)
[RFC 2812 - IRC: Client Protocol](https://rfc-editor.org/rfc/rfc2812)
[RFC 2813 - IRC: Server Protocol](https://rfc-editor.org/rfc/rfc2813)
[ircgod](https://ircgod.com)
[Model IRC client protocol](https://modern.ircdocs.horse)

### rfc reading tips
[Tips for Reading RFCs](https://www.rfc-editor.org/series/rfc-tips/)

### Reference IRC client and server
[Server: ergo chat (formerly Oragono)](https://github.com/ergochat/ergo)
[Client: irssi](https://github.com/irssi/irssi)

### Sockets
[Sockets & Networking](https://web.mit.edu/6.031/www/sp19/classes/23-sockets-networking/)
[All about sockets](https://docs.oracle.com/javase/tutorial/networking/sockets/index.html)

[CS107 Sockets Programming](https://web.stanford.edu/class/archive/cs/cs107/cs107.1238/lectures/20/)
[Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) - What is a
	socket?, IP Addresses, structs, and Data Munging, Jumping from IPv4 to IPv6,
	System Calls or Bust

### DCC protocol
[Direct Client-to-Client protocol (DCC)](https://modern.ircdocs.horse/dcc.html)

### Use of AI

- 
- 

---
