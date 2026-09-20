#!/bin/bash

# test_transfer.sh — full DCC sender-side end-to-end test. No nc.
# Registers a nick, offers a file via the bot, parses the DCC port from the
# offer line, accepts, connects with embedded python3 (holds the socket open
# until EOF), prints the received bytes and byte count.

# Usage: ./test_transfer.sh [irc-host] [irc-port] [password] [nick] [filename]

set -u

HOST=${1:-127.0.0.1}
IRCPORT=${2:-6667}
PASS=${3:-pass}
WHO=${4:-alice}
FILE=${5:-file}
OUT=received.bin

tmp=$(mktemp)

exec 3<>"/dev/tcp/$HOST/$IRCPORT" || { echo "irc connect failed"; exit 1; }

cat <&3 > "$tmp" &
catpid=$!
cleanup() {
    kill "$catpid" 2>/dev/null
    exec 3<&- 2>/dev/null
    rm -f "$tmp"
}
trap cleanup EXIT INT TERM

sleep 0.3
printf 'PASS %s\r\nNICK %s\r\nUSER %s 0 * :%s\r\n' "$PASS" "$WHO" "$WHO" "$WHO" >&3
sleep 1
printf 'PRIVMSG bot :!send %s %s\r\n' "$WHO" "$FILE" >&3
sleep 1.5

dcc=$(grep -oa 'DCC SEND [^[:space:]]* [^[:space:]]* [0-9]* [0-9]*' "$tmp" | tail -1)
echo "offer line : $dcc"

# fields: DCC SEND <filename> <ip> <port> <size>
ip=$(echo "$dcc" | awk '{print $4}')
port=$(echo "$dcc" | awk '{print $5}')
if [ -z "$port" ]; then
    echo "NO OFFER (bot busy / bad filename?)"
    sed 's/^/  irc  /' "$tmp"
    exit 1
fi

printf 'PRIVMSG bot :!accept\r\n' >&3
sleep 1
echo "accept reply:"
sed 's/^/  irc  /' "$tmp" | tail -3

echo "connecting to $ip:$port ..."
python3 - "$ip" "$port" <<'PYEOF' | tee "$OUT"
import socket
import sys

ip, port = sys.argv[1], int(sys.argv[2])
s = socket.create_connection((ip, port))
print("python local end:", s.getsockname(), file=sys.stderr)
s.settimeout(10)
data = b""
try:
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
except socket.timeout:
    print("TIMEOUT: no data / no close within 10s", file=sys.stderr)
s.close()
print("RECEIVED %d bytes: %r" % (len(data), data))
PYEOF

printf '\n^--- received bytes (saved to %s, %s bytes)\n' "$OUT" "$(wc -c <"$OUT" 2>/dev/null || echo 0)"
