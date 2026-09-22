#!/bin/bash
# Evaluation sheet "Basic checks", verified on the sources:
#   - Makefile has NAME/all/clean/fclean/re, builds with the required flags,
#     executable named ircserv, a second make does not relink
#   - only functions from the subject's allowed list (no read/write/open/sleep/...)
#   - exactly one poll() (or equivalent) call site, no select/epoll/kqueue
#   - no fork, no errno-driven logic
#   - every fcntl() is fcntl(fd, F_SETFL, O_NONBLOCK)
# Usage: bash tests/checks/basic/checks.sh

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
fail=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; fail=1; }
code() { # source lines with comments and string literals stripped, prefixed file:line
    find "$ROOT/srcs" "$ROOT/includes" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.tpp' -o -name '*.ipp' \) |
    while read -r f; do
        awk -v F="${f#$ROOT/}" '
            BEGIN { inc = 0 }
            {
                line = $0
                if (inc) { if (sub(/.*\*\//, "", line)) inc = 0; else next }
                while (match(line, /\/\*/)) { if (match(line, /\/\*.*\*\//)) { line = substr(line, 1, RSTART-1) substr(line, RSTART+RLENGTH) } else { line = substr(line, 1, RSTART-1); inc = 1; break } }
                sub(/\/\/.*/, "", line)
                gsub(/"([^"\\]|\\.)*"/, "\"\"", line)
                print F ":" NR ": " line
            }' "$f"
    done
}
SRC=$(code)

echo "[Makefile and build]"
for rule in all clean fclean re; do
    grep -qE "^$rule[[:space:]]*:" "$ROOT/Makefile" && ok "rule $rule" || bad "rule $rule missing"
done
grep -qE '^[[:space:]]*NAME[[:space:]]*[:?]?=[[:space:]]*ircserv' "$ROOT/Makefile" && ok "NAME = ircserv" || bad "NAME is not ircserv"
grep -qE '^\s*CXXFLAGS.*-Wall.*-Wextra.*-Werror' "$ROOT/Makefile" && ok "flags -Wall -Wextra -Werror" || bad "flags -Wall -Wextra -Werror missing"
grep -qE '\-std=c\+\+98' "$ROOT/Makefile" && ok "flag -std=c++98" || bad "flag -std=c++98 missing"
if make -s -C "$ROOT" >/dev/null 2>&1 && [ -x "$ROOT/ircserv" ]; then ok "make builds ./ircserv"; else bad "make did not produce ./ircserv"; fi
second=$(make -C "$ROOT" 2>&1 | grep -cE '(c\+\+|clang|g\+\+).* -o ')
[ "$second" -eq 0 ] && ok "a second make does not relink" || bad "a second make recompiled/relinked ($second command(s))"

echo "[allowed functions only]"
# not in the subject's list: read/write (send/recv are), open, stat, sleep/usleep, system, pipe, dup, exec*, ioctl
forb=$(printf '%s\n' "$SRC" | grep -E '(^|[^A-Za-z0-9_:.>])(read|write|open|stat|sleep|usleep|system|pipe|dup|dup2|execve|execl|execvp|ioctl|select)[[:space:]]*\(' | grep -vE 'std::|\.read\(|\.write\(|\.open\(|is_open\(')
if [ -z "$forb" ]; then ok "no function outside the subject's list (read/write/open/stat/sleep/system/pipe/dup/exec/ioctl)"; else bad "functions outside the allowed list:"; printf '%s\n' "$forb" | cut -d: -f1,2,3 | sed 's/^/       /'; fi

echo "[one poll()]"
polls=$(printf '%s\n' "$SRC" | grep -E '(^|[^A-Za-z0-9_:.>])poll[[:space:]]*\(')
n=$(printf '%s\n' "$polls" | grep -c .)
if [ "$n" -eq 1 ]; then ok "exactly one poll() call site: $(printf '%s\n' "$polls" | cut -d: -f1,2)"; else bad "$n poll() call sites:"; printf '%s\n' "$polls" | cut -d: -f1,2 | sed 's/^/       /'; fi
if printf '%s\n' "$SRC" | grep -qE '(^|[^A-Za-z0-9_])(select|epoll_wait|epoll_create|kqueue|kevent)[[:space:]]*\('; then bad "select/epoll/kqueue found"; else ok "no select/epoll/kqueue"; fi

echo "[no fork, no errno]"
if printf '%s\n' "$SRC" | grep -qE '(^|[^A-Za-z0-9_])fork[[:space:]]*\('; then bad "fork() found"; else ok "no fork()"; fi
if printf '%s\n' "$SRC" | grep -qE '(^|[^A-Za-z0-9_])errno([^A-Za-z0-9_]|$)'; then bad "errno used:"; printf '%s\n' "$SRC" | grep -E '(^|[^A-Za-z0-9_])errno' | cut -d: -f1,2 | sed 's/^/       /'; else ok "errno is not used"; fi

echo "[fcntl form]"
fc=$(printf '%s\n' "$SRC" | grep -E '(^|[^A-Za-z0-9_])fcntl[[:space:]]*\(')
badfc=$(printf '%s\n' "$fc" | grep -vE 'fcntl[[:space:]]*\([^,]+,[[:space:]]*F_SETFL[[:space:]]*,[[:space:]]*O_NONBLOCK[[:space:]]*\)' | grep .)
if [ -z "$badfc" ]; then ok "every fcntl() is fcntl(fd, F_SETFL, O_NONBLOCK) ($(printf '%s\n' "$fc" | grep -c .) call(s))"; else bad "fcntl() used another way:"; printf '%s\n' "$badfc" | sed 's/^/       /'; fi

echo "[non-blocking sockets]"
printf '%s\n' "$SRC" | grep -q 'O_NONBLOCK' && ok "O_NONBLOCK is set" || bad "O_NONBLOCK never set"

exit $fail
