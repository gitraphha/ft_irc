# ft_irc tests
#
#   make -C tests                 checks (all groups), then the dynamic tester (all suites)
#   make -C tests checks          tests/checks: fixed C++/python/bash tests: basic networking client bonus
#   make -C tests tester          tests/dynamic_tester: protocol suites over TCP, HTML report
#   make -C tests lab             interactive lab (several client windows, server log, walkthrough)
#   make -C tests clients         irssi or nc client windows on a launched server
#   make -C tests monitor         live dashboard of the server
#
# Each part has its own Makefile with more targets:  make -C tests/checks help   make -C tests/dynamic_tester help

SHELL := /bin/bash
REPORT := $(CURDIR)/dynamic_tester/artifacts/report.html

.PHONY: all checks tester lab clients monitor clean

all:
	@[ -t 1 ] && export FORCE_COLOR=1; t=$$(mktemp); \
	 $(MAKE) --no-print-directory -C "$(CURDIR)/checks" all 2>&1 | tee "$$t"; r1=$${PIPESTATUS[0]}; \
	 c=$$(sed $$'s/\e\\[[0-9;]*m//g' "$$t" | grep -E '^ *Results:' | tail -1 | sed 's/^ *Results: *//'); \
	 $(MAKE) --no-print-directory -C "$(CURDIR)/dynamic_tester" all 2>&1 | tee "$$t"; r2=$${PIPESTATUS[0]}; \
	 d=$$(sed $$'s/\e\\[[0-9;]*m//g' "$$t" | grep -E '^ *Results:' | tail -1 | sed 's/^ *Results: *//'); rm -f "$$t"; \
	 url=$$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).as_uri())' "$(REPORT)"); \
	 echo; echo "================================ tests ================================"; \
	 printf '  %-16s %s\n' "checks" "$${c:-did not run}"; \
	 printf '  %-16s %s\n' "dynamic tester" "$${d:-did not run}"; \
	 printf '  %-16s %s\n' "report" "$$url"; \
	 printf '  %-16s %s\n' "" "(Cmd-click / Ctrl-click the link, or: make -C tests/dynamic_tester open)"; \
	 echo "======================================================================="; \
	 [ $$r1 = 0 ] && [ $$r2 = 0 ]

checks:
	@[ -t 1 ] && export FORCE_COLOR=1; $(MAKE) --no-print-directory -C "$(CURDIR)/checks" all

tester:
	@$(MAKE) --no-print-directory -C "$(CURDIR)/dynamic_tester" all

lab clients monitor:
	@$(MAKE) --no-print-directory -C "$(CURDIR)/dynamic_tester" $@

clean:
	@$(MAKE) --no-print-directory -C "$(CURDIR)/checks" clean
	@$(MAKE) --no-print-directory -C "$(CURDIR)/dynamic_tester" clean
