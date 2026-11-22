#!/bin/sh
set -eu

# The parser is generated with ABI 14 so that it also loads in tree-sitter
# 0.22 to 0.24 runtimes, which some distributions still ship (Emacs on
# Debian 13 links against 0.22). Newer runtimes load ABI 14 as well. See
# docs/DESIGN_DECISIONS.md. This checks the generated parser and that both
# ways of generating it still pass the flag; test/compat/run.sh checks that
# the parser actually loads and parses in a 0.22 runtime.
expected=14
status=0

# Allow a trailing carriage return, for checkouts with CRLF line endings.
actual=$(sed -n 's/^#define LANGUAGE_VERSION \([0-9][0-9]*\).*$/\1/p' src/parser.c)
if [ -z "$actual" ]; then
  echo 'src/parser.c: no LANGUAGE_VERSION found'
  status=1
elif [ "$actual" != "$expected" ]; then
  printf 'src/parser.c has ABI %s, expected %s\n' "$actual" "$expected"
  status=1
fi

for file in package.json Makefile; do
  if ! grep -q -- "generate --abi $expected" "$file"; then
    printf '%s: tree-sitter generate is not passed --abi %s\n' "$file" "$expected"
    status=1
  fi
done

exit $status
