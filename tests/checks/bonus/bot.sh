#!/bin/bash

# The bot dialogue over raw TCP (bash /dev/tcp), two clients.
#
#   bash tests/checks/bonus/bot.sh [host] [port] [password] [file]
#   defaults: localhost 6667 pass test.txt; <file> is a name under files/
#   needs a running ./ircserv_bonus

set -u
failed=0

HOST=${1:-localhost}
PORT=${2:-6667}
PASS=${3:-pass}
FILE=${4:-test.txt}
BOT=bot

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
                echo "  ok   [$who] $want"
                return 0
            fi
            echo "       [$who] $line"
        else
            timeout=$((timeout - 1))
        fi
    done
    echo "  FAIL [$who] never got: $want"
    failed=1
    return 1
}

echo "[alice and bob register]"
send 3 "PASS $PASS" "NICK alice" "USER a 0 * :Alice"
send 4 "PASS $PASS" "NICK bob"   "USER b 0 * :Bob"
sleep 1

echo "[bot commands from alice]"
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

echo "[CTCP VERSION and PING]"
send 3 "PRIVMSG $BOT :"$'\x01'"VERSION"$'\x01'
wait4 alice 3 "DCCBot"

send 3 "PRIVMSG $BOT :"$'\x01'"PING 12345"$'\x01'
wait4 alice 3 "PING 12345"

echo "[!send: the offer reaches bob]"
send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "offer sent to bob"
wait4 bob 4 "DCC SEND $FILE"

echo "[!reject frees the slot, a new offer, !accept]"
send 4 "PRIVMSG $BOT :!reject"
wait4 bob 4 "transfer rejected"

send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "offer sent to bob"
wait4 bob 4 "DCC SEND $FILE"

send 4 "PRIVMSG $BOT :!accept"
wait4 bob 4 "transfer accepted"

echo "[a second !send while a transfer is pending]"
send 3 "PRIVMSG $BOT :!send bob $FILE"
wait4 alice 3 "bot busy"

exec 3<&- 3>&-
exec 4<&- 4>&-
[ "$failed" -eq 0 ]
