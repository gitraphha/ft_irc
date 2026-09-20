#!/bin/bash

# DCC bot lifecycle test — no irssi/netcat needed.
# Uses bash /dev/tcp: one fd per IRC session, kept open for the whole test.

# Usage: ./test_bot.sh [host] [port] [password] [testfile]
# Requires: server running (./ircserv_bonus PORT PASS, files/<testfile> present)

set -u

HOST=${1:-localhost}
PORT=${2:-6667}
PASS=${3:-pass}
FILE=${4:-test.txt}
BOT=bot

PASSED=0
FAILED=0

exec 3<>"/dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "cannot connect"; exit 1; }  # alice
exec 4<>"/dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "cannot connect"; exit 1; }  # bob

send() { # fd rest...
    local fd=$1; shift
    printf '%s\r\n' "$@" >&"$fd"
}

# wait4 <label> <fd> <expected-substring> [timeout]
# drains lines until substring found, prints them for visibility
wait4() {
    local who=$1 fd=$2 want=$3 timeout=${4:-5}
    local line
    while (( timeout > 0 )); do
        if IFS= read -r -t 1 line <&"$fd"; then
            (( timeout -= 0 ))
            if [[ "$line" == *"$want"* ]]; then
                echo "PASS [$who] saw: $want"
                PASSED=$((PASSED + 1))
                return 0
            fi
            echo "  [$who] $line"
        else
            timeout=$((timeout - 1))
        fi
    done
    echo "FAIL [$who] never saw: $want"
    FAILED=$((FAILED + 1))
    return 1
}

echo "== registering alice (fd3) and bob (fd4) =="
send 3 "PASS $PASS" "NICK alice" "USER a 0 * :Alice"
send 4 "PASS $PASS" "NICK bob"   "USER b 0 * :Bob"
sleep 1

echo "== bot commands (alice -> bot) =="
send 3 "PRIVMSG $BOT :!version"
wait4 alice 3 "I am DCCBot on"

send 3 "PRIVMSG $BOT :!whoami"
wait4 alice 3 "You are alice"

send 3 "PRIVMSG $BOT :!help"
wait4 alice 3 "!send"

send 3 "PRIVMSG $BOT :!bogus"
wait4 alice 3 "Unknown command"

send 3 "PRIVMSG $BOT :!accept"
wait4 alice 3 "no pending transfers"

echo "== CTCP =="
send 3 "PRIVMSG $BOT :"$'\x01'"VERSION"$'\x01'
wait4 alice 3 "DCCBot"

send 3 "PRIVMSG $BOT :"$'\x01'"PING 12345"$'\x01'
wait4 alice 3 "PING 12345"

echo "== send flow =="
send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "offer sent to bob"
wait4 bob 4 "DCC SEND $FILE"

echo "== reject frees the slot, then accept =="
send 4 "PRIVMSG $BOT :!reject"
wait4 bob 4 "transfer rejected"

send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "offer sent to bob"
wait4 bob 4 "DCC SEND $FILE"

send 4 "PRIVMSG $BOT :!accept"
wait4 bob 4 "transfer accepted"

echo "== busy while a transfer is active =="
send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "bot busy"

echo
echo "PASSED=$PASSED FAILED=$FAILED"
exec 3<&- 3>&-
exec 4<&- 4>&-
[ "$FAILED" -eq 0 ]
