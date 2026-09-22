#!/usr/bin/env python3
# What a client receives right before the server closes its link:
# 464 on a wrong password, ERROR after QUIT, ERROR after the third over-long line.
#   python3 closing.py [port] [password]      needs a running ./ircserv
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 6667
PW = sys.argv[2] if len(sys.argv) > 2 else "pass"


def ok(cond, text):
    print("  %s %s" % ("ok  " if cond else "FAIL", text))


def talk(lines, wait=2.0):
    """send the lines, read until EOF or timeout -> (data, eof)"""
    s = socket.create_connection((HOST, PORT))
    for l in lines:
        s.sendall(l.encode())
        time.sleep(0.2)
    s.settimeout(wait)
    data = b""
    eof = False
    try:
        while True:
            c = s.recv(4096)
            if not c:
                eof = True
                break
            data += c
    except socket.timeout:
        pass
    s.close()
    return data, eof


print("[wrong password]")
data, eof = talk(["PASS wrong\r\n", "NICK w\r\n", "USER w 0 * :w\r\n"])
ok(b" 464 " in data, "464 ERR_PASSWDMISMATCH received" + ("" if b" 464 " in data else " (got %r)" % data[-80:]))
ok(eof, "then the link is closed")

print("[QUIT]")
data, eof = talk(["PASS %s\r\n" % PW, "NICK q\r\n", "USER q 0 * :q\r\n", "QUIT :bye\r\n"])
ok(b"ERROR :" in data, "ERROR :Closing link received" + ("" if b"ERROR :" in data else " (last bytes %r)" % data[-80:]))
ok(eof, "then the link is closed")

print("[three over-long lines in a row]")
data, eof = talk(["PASS %s\r\n" % PW, "NICK o\r\n", "USER o 0 * :o\r\n"] + ["X" * 600 + "\r\n"] * 3)
ok(data.count(b" 417 ") >= 1, "417 received (%d times)" % data.count(b" 417 "))
ok(eof, "the link is closed after the third one")
print("       ERROR :Closing link before the close: %s" % ("yes" if b"ERROR :" in data else "no"))
