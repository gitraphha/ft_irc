# ft_irc tester -- convenience targets. See README.md.
#
#   make              everything -> artifacts/report.html
#   make live         everything, watching server + client windows live
#   make mandatory | robustness | load | bonus | compare
#   make live-load    the slow-reader load test with the live view
#   make lab          interactive: client windows + server log + help + /walk
#   make clients      N irssi or nc client windows on a launched server (tmux panes or separate windows)
#   make irssi | nc   same, program preselected
#   make monitor      btop-style live monitor (launches the server; drive it with irssi/nc)
#   make attach       monitor a server on the port: one you started, or the tester's while it runs
#   make config       print the tester.conf values
#   make external     test a server you started yourself
#   make debug        print every line sent/received
#   make open         open the report (macOS)      make clean   remove artifacts/
#
# Overrides:  make bonus PORT=6668 PASSWORD=secret
#             make live SET="slow_client=drop overlong=truncate"   (tester.conf values for one run)

PYTHON       ?= python3
BINARY       ?= ../../ircserv
BONUS_BINARY ?= ../../ircserv_bonus
HOST         ?= 127.0.0.1
PORT         ?= 6667
PASSWORD     ?= pass
OUTDIR       ?= artifacts
REPORT       ?= $(OUTDIR)/report.html
SET          ?=
SETS         = $(foreach kv,$(SET),--set $(kv))

COMMON = --binary $(BINARY) --bonus-binary $(BONUS_BINARY) --host $(HOST) --port $(PORT) \
         --password $(PASSWORD) --outdir $(OUTDIR) --report $(REPORT) --no-intro $(SETS)

.PHONY: all mandatory robustness load bonus compare terminal external debug live live-load \
        lab irssi nc clients monitor monitor-bonus attach config open clean help

all:
	@$(PYTHON) test_irc.py --mode all $(COMMON)
mandatory:
	@$(PYTHON) test_irc.py --mode mandatory $(COMMON)
robustness:
	@$(PYTHON) test_irc.py --mode robustness $(COMMON)
load:
	@$(PYTHON) test_irc.py --mode load $(COMMON)
bonus:
	@$(PYTHON) test_irc.py --mode bonus $(COMMON)
compare:
	@$(PYTHON) test_irc.py --mode compare $(COMMON)
terminal:
	@$(PYTHON) test_irc.py --mode terminal $(COMMON)
external:
	@$(PYTHON) test_irc.py --no-start --mode all $(COMMON)
debug:
	@$(PYTHON) test_irc.py --mode all $(COMMON) --debug

live:
	@$(PYTHON) test_irc.py --mode all --live $(COMMON)
live-load:
	@$(PYTHON) test_irc.py --mode load --live $(COMMON)

N ?= 3
lab:
	@$(PYTHON) test_irc.py --mode lab $(COMMON)
irssi:
	@$(PYTHON) test_irc.py --mode irssi $(COMMON) < /dev/tty
nc:
	@$(PYTHON) test_irc.py --mode nc $(COMMON) < /dev/tty
clients:
	@$(PYTHON) test_irc.py --mode clients $(COMMON) < /dev/tty

monitor:
	@$(PYTHON) irctop.py --start $(BINARY) $(PORT) $(PASSWORD)
monitor-bonus:
	@$(PYTHON) irctop.py --start $(BONUS_BINARY) $(PORT) $(PASSWORD)
attach:
	@$(PYTHON) irctop.py --attach $(PORT) $(PASSWORD)

config:
	@$(PYTHON) test_irc.py --show-config
open:
	@open $(REPORT) 2>/dev/null || echo "Report at $(REPORT)"
clean:
	@rm -rf $(OUTDIR)
help:
	@$(PYTHON) test_irc.py --help
