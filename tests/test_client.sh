#! /bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_BIN="$SCRIPT_DIR/test_client"

trap 'rm -f "$TEST_BIN"' EXIT

c++ -Wall -Wextra -Werror -pedantic -std=c++98 -I "$ROOT_DIR/includes" \
	-o "$TEST_BIN" \
	"$SCRIPT_DIR/src/test_client.cpp" \
	"$ROOT_DIR/srcs/server/Client.cpp" || exit 1

"$TEST_BIN"
