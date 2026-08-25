#! /bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

c++ -Wall -Wextra -Werror -pedantic -std=c++98 -I includes $SCRIPT_DIR/src/test_reply.cpp