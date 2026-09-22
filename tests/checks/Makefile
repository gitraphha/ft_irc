# ft_irc checks: basic, networking, client, bonus. macOS and Linux.
#
#   make -C tests/checks              everything: basic networking client bonus
#   make -C tests/checks basic        Makefile rules, flags, no relink, allowed functions only, one poll(),
#                                     no select/epoll/kqueue/fork/errno, fcntl form            (basic/checks.sh)
#   make -C tests/checks networking   framing, 510 limit, POLLOUT send path, stalled member kept (C++);
#                                     odd input, crash regressions, over-length lines, killed
#                                     and many clients (python, fresh server each)             (networking/)
#   make -C tests/checks client       registration, PRIVMSG/NOTICE/PING/QUIT, channel operator
#                                     commands and numerics, parser, replies (C++, no sockets) (client/)
#   make -C tests/checks bonus        bot dialogue, real DCC transfer (bash, fresh ircserv_bonus) (bonus/)
#   make -C tests/checks socket       one test by name: basic socket flood probe throw overlong longline
#                                     churn closing syscalls parser reply state registration messaging operator bot transfer
#
# By hand: bash tests/checks/run.sh <group|name...>   (no argument = usage)

SHELL := /bin/bash
NAMES := basic socket flood probe throw overlong longline churn closing syscalls parser reply state registration messaging operator bot transfer

.PHONY: all basic networking client bonus $(NAMES) clean help

all networking bonus $(NAMES):
	@bash "$(CURDIR)/run.sh" $@

clean:
	@rm -f "$(CURDIR)"/.*.test "$(CURDIR)"/bonus/received.bin "$(CURDIR)"/networking/.syscalls.so

help:
	@sed -n '2,15p' "$(CURDIR)/Makefile" | sed 's/^# \{0,1\}//'
