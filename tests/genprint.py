#!/usr/bin/env python3
"""Print a random printable-ASCII string with random-length character runs.

Usage: python3 genprint.py [max_len]
"""
import random
import sys

CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .!?,"


def rand_print(chars=CHARS, char_rand_max=5, str_rand_max=80):
    return "".join(
        c * random.randint(1, char_rand_max)
        for _ in range(random.randint(1, str_rand_max))
        for c in random.choice(chars)
    )


if __name__ == "__main__":
    print(rand_print() if len(sys.argv) < 2 else rand_print(str_rand_max=int(sys.argv[1])))