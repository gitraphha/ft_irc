#!/bin/bash
# Runs the checks by name or by group. macOS and Linux.
#
#   bash tests/checks/run.sh                 usage and the list of names
#   bash tests/checks/run.sh <group>         basic | networking | client | bonus | all
#   bash tests/checks/run.sh <name>...       one or more tests by name
#
# C++ tests are compiled with the project flags against the server sources,
# run, and removed again. Scripts (.py / .sh) get a fresh server on a free
# port. Keeps going after a failure. Exit status 1 if anything failed.

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PASSWORD=pass
CXX=(c++ -Wall -Wextra -Werror -pedantic -std=c++98 "-I$ROOT/includes")
SRV=("$ROOT"/srcs/commands/*.cpp "$ROOT"/srcs/server/*.cpp "$ROOT"/srcs/utils/*.cpp)

if [ -t 1 ] || [ -n "${FORCE_COLOR:-}" ]; then
    B=$'\e[1m'; D=$'\e[2m'; R=$'\e[31m'; G=$'\e[32m'; Y=$'\e[33m'; C=$'\e[36m'; M=$'\e[35m'; N=$'\e[0m'
else
    B=; D=; R=; G=; Y=; C=; M=; N=
fi
[ -t 1 ] && stty sane 2>/dev/null

# name -> file | link/server | description
spec() {
    case "$1" in
        basic)        echo "basic/checks.sh||Makefile rules and flags, no relink, allowed functions, one poll(), fcntl form" ;;
        socket)       echo "networking/socket.cpp|SOCKET|line framing, 510-byte limit, POLLOUT send path (Socket class)" ;;
        flood)        echo "networking/flood.cpp|E2E|a channel member stops reading while the channel is flooded" ;;
        probe)        echo "networking/probe.py|ircserv|odd input: NUL, binary, CR CR LF, bare LF, 511 and 4096-byte lines" ;;
        throw)        echo "networking/throw.py|ircserv|MODE on a missing channel and +o by a non-operator do not kill the server" ;;
        overlong)     echo "networking/overlong.py|ircserv|417 on over-length lines, close after the third one in a row" ;;
        longline)     echo "networking/longline.py|ircserv|10 KB without CRLF from three clients while a fourth stays responsive" ;;
        churn)        echo "networking/churn.py|ircserv|20 members: broadcast storm, JOIN/PART churn, RST disconnects, a stalled member" ;;
        closing)      echo "networking/closing.py|ircserv|what the client gets before the server closes its link: 464, ERROR" ;;
        parser)       echo "client/parser.cpp||message grammar: prefix, command, params, trailing" ;;
        reply)        echo "client/reply.cpp||numeric reply formatting" ;;
        state)        echo "client/state.cpp|SRV|client state: nick casefolding, invites, identity, prefix, flags" ;;
        registration) echo "client/registration.cpp|SRV|PASS/NICK/USER/CAP in every order, 431 432 433 461 462 464" ;;
        messaging)    echo "client/messaging.cpp|SRV|PING/PONG, QUIT, NOTICE, PRIVMSG and their numerics" ;;
        operator)     echo "client/operator.cpp|SRV|482/442 for non-operators; KICK INVITE TOPIC MODE i t k o l" ;;
        bot)          echo "bonus/bot.sh|ircserv_bonus|bot dialogue: !help !version !whoami !send !reject !accept, CTCP" ;;
        transfer)     echo "bonus/transfer.sh|ircserv_bonus|a DCC transfer through the bot: offer, accept, bytes received and compared" ;;
        *)            return 1 ;;
    esac
}
G_BASIC="basic"
G_NET="socket flood probe throw overlong longline churn closing"
G_CLIENT="parser reply state registration messaging operator"
G_BONUS="bot transfer"
ALL="$G_BASIC $G_NET $G_CLIENT $G_BONUS"

if [ "$#" -eq 0 ]; then
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    echo "groups: basic networking client bonus all"
    echo "names:  $ALL"
    exit 0
fi
names=()
for a in "$@"; do
    case "$a" in
        all) names+=($ALL) ;; basic) names+=($G_BASIC) ;; networking) names+=($G_NET) ;;
        client) names+=($G_CLIENT) ;; bonus) names+=($G_BONUS) ;; *) names+=("$a") ;;
    esac
done

# Renders a test's output in one style:  [section]  ->  section title,
# "  ok   text" -> [PASS] text, "  FAIL text" -> [FAIL] text, the rest dimmed.
render() {
    local line
    while IFS= read -r line; do
        case "$line" in
            *" test:  ["*"]: "*)   continue ;;   # server log lines (Logger)
            "  ok "*)   t="${line#  ok }";   echo "  ${G}[PASS]${N} ${t#"${t%%[! ]*}"}" ;;
            "  FAIL "*) t="${line#  FAIL }"; echo "  ${R}[FAIL]${N} ${t#"${t%%[! ]*}"}" ;;
            "["*"]"*)              echo "${B}${line}${N}" ;;
            "")                    ;;
            *)                     echo "${D}${line}${N}" ;;
        esac
    done
}

build_server() {   # $1 = ircserv | ircserv_bonus
    if [ "$1" = ircserv_bonus ]; then make -s -C "$ROOT" bonus >/dev/null 2>&1; else make -s -C "$ROOT" >/dev/null 2>&1; fi
    [ -x "$ROOT/$1" ]
}
start_server() {   # $1 = binary; sets SRV_PID PORT LOG
    PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    LOG=$(mktemp -t ircserv_check.XXXXXX)
    ( cd "$ROOT" && exec "$ROOT/$1" "$PORT" "$PASSWORD" > "$LOG" 2>&1 ) &
    SRV_PID=$!
    for i in $(seq 1 40); do
        python3 -c "import socket; socket.create_connection(('127.0.0.1', $PORT), timeout=0.5).close()" 2>/dev/null && break
        sleep 0.1
    done
    echo "${D}  $1 started for this test: pid $SRV_PID, port $PORT${N}"
}
stop_server() { kill $SRV_PID 2>/dev/null; wait $SRV_PID 2>/dev/null; rm -f "$LOG"; SRV_PID=""; }
trap 'if [ -n "$SRV_PID" ]; then stop_server; fi' EXIT

passed=(); failed=()
for name in "${names[@]}"; do
    line=$(spec "$name") || { echo "no such test: $name  (names: $ALL)"; failed+=("$name"); continue; }
    IFS='|' read -r file kind desc <<< "$line"
    file="$HERE/$file"; dir="$(dirname "$file")"
    echo; echo "${B}${C}[ $name ]${N}  ${D}${desc}${N}"
    case "$file" in
        *.cpp)
            objs=()
            case "$kind" in
                SRV)    objs=("${SRV[@]}") ;;
                SOCKET) objs=("$ROOT/srcs/server/Socket.cpp" "$ROOT/srcs/server/Logger.cpp") ;;
            esac
            bin="$HERE/.$name.test"
            if ! "${CXX[@]}" "-I$dir" -o "$bin" "$file" "${objs[@]}"; then
                echo "  ${R}[FAIL]${N} does not compile with the project flags"; failed+=("$name"); continue
            fi
            if [ "$kind" = E2E ]; then
                build_server ircserv || { echo "  ${R}[FAIL]${N} make failed"; failed+=("$name"); rm -f "$bin"; continue; }
                "$bin" "$ROOT" 2>&1 | render; rc=${PIPESTATUS[0]}
            else
                "$bin" 2>&1 | render; rc=${PIPESTATUS[0]}
            fi
            rm -f "$bin" ;;
        */basic/*.sh)
            bash "$file" 2>&1 | render; rc=${PIPESTATUS[0]} ;;
        *.py|*.sh)
            build_server "$kind" || { echo "  ${R}[FAIL]${N} make failed for $kind"; failed+=("$name"); continue; }
            start_server "$kind"
            out=$(mktemp)
            case "$file" in
                *.py) (cd "$dir" && python3 "$(basename "$file")" "$PORT" "$PASSWORD") 2>&1 | tee "$out" | render; rc=${PIPESTATUS[0]} ;;
                *.sh) (cd "$dir" && bash "$(basename "$file")" 127.0.0.1 "$PORT" "$PASSWORD") 2>&1 | tee "$out" | render; rc=${PIPESTATUS[0]} ;;
            esac
            grep -qE "^  FAIL |Traceback" "$out" && rc=1
            kill -0 $SRV_PID 2>/dev/null || { echo "  ${R}[FAIL]${N} the server died during $name"; rc=1; }
            rm -f "$out"; stop_server ;;
    esac
    [ "$rc" -eq 0 ] && passed+=("$name") || failed+=("$name")
done
echo; echo "${M}$(printf -- '-%.0s' $(seq 1 62))${N}"
for n in "${passed[@]}"; do echo "  ${G}PASS${N}  $n"; done
for n in "${failed[@]}"; do echo "  ${R}FAIL${N}  $n"; done
echo "  Results: ${G}${#passed[@]} pass${N} / ${R}${#failed[@]} fail${N} / $(( ${#passed[@]} + ${#failed[@]} )) total"
echo "${M}$(printf -- '-%.0s' $(seq 1 62))${N}"
[ ${#failed[@]} -eq 0 ]
