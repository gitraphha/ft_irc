#!/bin/bash
# A DCC transfer through the bot: offer, accept, connect to the DCC port, read
# to EOF, compare with the original file.
#
#   bash tests/checks/bonus/transfer.sh [host] [port] [password] [nick] [file] [botnick]
#   defaults: 127.0.0.1 6667 pass alice test.txt DCCBot; <file> is a name under files/
#   needs a running ./ircserv_bonus; the bytes received are saved next to this script as received.bin

set -u

HOST=${1:-127.0.0.1}
IRCPORT=${2:-6667}
PASS=${3:-pass}
WHO=${4:-alice}
FILE=${5:-test.txt}
BOT=${6:-DCCBot}
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORIG="$HERE/../../../files/$FILE"
OUT="$HERE/received.bin"

tmp=$(mktemp)
exec 3<>"/dev/tcp/$HOST/$IRCPORT" 2>/dev/null || { echo "  FAIL cannot connect to $HOST:$IRCPORT"; exit 1; }
cat <&3 > "$tmp" &
catpid=$!
cleanup() { kill "$catpid" 2>/dev/null; exec 3<&- 2>/dev/null; rm -f "$tmp"; }
trap cleanup EXIT INT TERM

echo "[$WHO offers files/$FILE to $WHO through the bot]"
sleep 0.3
printf 'PASS %s\r\nNICK %s\r\nUSER %s 0 * :%s\r\n' "$PASS" "$WHO" "$WHO" "$WHO" >&3
sleep 1
printf 'PRIVMSG %s :!send %s %s\r\n' "$BOT" "$WHO" "$FILE" >&3
sleep 1.5
dcc=$(grep -oa 'DCC SEND [^[:space:]]* [^[:space:]]* [0-9]* [0-9]*' "$tmp" | tail -1)
ip=$(echo "$dcc" | awk '{print $4}')
port=$(echo "$dcc" | awk '{print $5}')
size=$(echo "$dcc" | awk '{print $6}')
if [ -z "$port" ]; then
    echo "  FAIL no DCC SEND offer received"
    sed 's/^/       /' "$tmp"
    exit 1
fi
echo "  ok   offer received: $dcc"

echo "[$WHO accepts and connects to $ip:$port]"
printf 'PRIVMSG %s :!accept\r\n' "$BOT" >&3
sleep 1
grep -qa "accepted" "$tmp" && echo "  ok   bot confirmed the accept" || echo "  FAIL no accept confirmation from the bot"
python3 - "$ip" "$port" "$OUT" <<'PYEOF'
import socket, sys
ip, port, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = socket.create_connection((ip, port)); s.settimeout(10)
data = b""
try:
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        data += chunk
except socket.timeout:
    print("  FAIL no data or no close within 10 s")
s.close()
open(out, "wb").write(data)
PYEOF
got=$(wc -c < "$OUT" | tr -d ' ')
[ "$got" -gt 0 ] && echo "  ok   $got bytes received, EOF from the sender" || echo "  FAIL nothing received"
if [ -f "$ORIG" ]; then
    cmp -s "$ORIG" "$OUT" && echo "  ok   identical to files/$FILE ($size bytes announced)" || echo "  FAIL differs from files/$FILE"
else
    echo "       files/$FILE not found here, content not compared"
fi
[ "$got" -gt 0 ] && { [ ! -f "$ORIG" ] || cmp -s "$ORIG" "$OUT"; }
