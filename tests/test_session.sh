#!/bin/bash

# test_session.sh — auto-register then drop into an interactive IRC session.
# Usage: ./test_session.sh [host] [port] [password] [nick]

set -u

HOST=${1:-localhost}
PORT=${2:-6667}
PASS=${3:-pass}
NICK=${4:-alice}

exec 3<>"/dev/tcp/$HOST/$PORT" 2>/dev/null || { echo "connect failed"; exit 1; }

printf 'PASS %s\r\nNICK %s\r\nUSER %s 0 * :%s\r\n' "$PASS" "$NICK" "$NICK" "$NICK" >&3

cat <&3 &
catpid=$!

cleanup() {
    kill "$catpid" 2>/dev/null   # drop socket so server sees the disconnect
    exec 3<&- 3>&-
}
trap cleanup EXIT INT TERM

echo "connected. type IRC lines (e.g.  PRIVMSG bot :!help )"
cat >&3
