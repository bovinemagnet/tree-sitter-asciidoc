#!/bin/sh
set -eu

# The parser is generated with ABI 14 so that it also loads in tree-sitter
# 0.22 to 0.24 runtimes, which some distributions still ship (Emacs on
# Debian 13 links against 0.22). Newer runtimes load ABI 14 as well.
expected=14
actual=$(sed -n 's/^#define LANGUAGE_VERSION \([0-9]*\)$/\1/p' src/parser.c)

if [ "$actual" != "$expected" ]; then
  printf 'src/parser.c has ABI %s, expected %s\n' "$actual" "$expected"
  exit 1
fi
