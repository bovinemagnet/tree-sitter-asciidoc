#!/bin/sh
# Builds the committed parsers against an older tree-sitter runtime and runs
# both corpora through it. The parsers are generated at ABI 14 so that Emacs
# builds linked against tree-sitter 0.22 load them (docs/DESIGN_DECISIONS.md);
# this checks they load and parse there exactly as the corpus expects.
#
# usage: test/compat/run.sh <tree-sitter source checkout>
set -eu

runtime=$(cd "$1" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -O1 -c -I"$runtime/lib/include" -I"$runtime/lib/src" \
  "$runtime/lib/src/lib.c" -o "$work/runtime.o"

status=0
for grammar in asciidoc asciidoc_inline; do
  dir=$root/tree-sitter-$grammar
  cc -O1 -w -DLANGUAGE_FN="tree_sitter_$grammar" \
    -I"$runtime/lib/include" -I"$dir/src" \
    "$root/test/compat/parse.c" "$dir/src/parser.c" "$dir/src/scanner.c" \
    "$work/runtime.o" -o "$work/parse_$grammar"
  printf '%s: ' "$grammar"
  python3 -I "$root/test/compat/corpus.py" "$dir/test/corpus" \
    "$work/parse_$grammar" "$work/$grammar" || status=1
done
exit $status
