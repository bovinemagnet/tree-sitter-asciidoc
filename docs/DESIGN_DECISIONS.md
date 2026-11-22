# Design decisions

This records the non-obvious choices behind the current grammar, so they are not
re-litigated or accidentally undone. For how the pieces fit together, see
[ARCHITECTURE.md](ARCHITECTURE.md).

## Split block and inline grammars

AsciiDoc is parsed by a block grammar that injects an inline grammar (see
[ARCHITECTURE.md](ARCHITECTURE.md)). The alternative, one grammar that handles
both, would need a single external scanner juggling block nesting and
constrained inline formatting at once. The split keeps each grammar and its
scanner small enough to reason about, mirrors how tree-sitter's Markdown grammar
is built, and lets the inline grammar be reused wherever inline content appears
(paragraphs, table cells, macro targets).

## Boundaries live in the external scanner

AsciiDoc has too much context sensitivity for a pure LR grammar: a delimiter
line can open or close a block depending on what is already open, `*` is a
bullet at the start of a line and emphasis mid-line, and constrained formatting
depends on word boundaries and on a matching delimiter existing later in the
paragraph. These decisions are made in C in `src/scanner.c` and surfaced as
external tokens. The grammar rules then stay declarative.

Consequence: the scanner carries serialized state (the open-block stack), so it
behaves correctly under incremental re-parsing.

## Constrained formatting via lookahead-gated openers

The inline scanner emits an opening delimiter (for example `_emphasis_begin`)
only when a valid matching close exists later in the injected text, which is the
whole paragraph: the look-ahead runs over line endings but not past a blank
line. This is the mechanism that keeps a lone `*` or the `#` in "issue #2" as
plain punctuation rather than error-recovering into an unterminated formatting
node. Typographic-quote openers and the hard line break use the same gate, but
look only at the rest of the line.

A delimiter between two word characters, such as each `_` in
`do_something_useful`, never reaches the scanner. AsciiDoc does not open or
close a constrained span inside a word, and the scanner cannot look back to see
the letter before a delimiter, so with a paragraph-wide look-ahead such a `_`
would pair with any later close. The inline grammar instead lexes the whole run
(`do_something_useful`, `a*b`) as one `word`. A doubled delimiter is not part of
that run, so the unconstrained `__x__` and `**x**` still work inside a word.

## Sections wrap content but keep `title1`..`title5`

When section nesting was added, two shapes were possible: rename headings to a
single `title` node, or keep the existing `title1`..`title5` and wrap them in a
new `section`. We kept the level-specific heading nodes and wrapped them. The
heading level is already encoded in the marker token, so a generic `title` would
not add information, and renaming would break every existing highlight query and
any downstream consumer. The additive choice landed cleanly with no query
changes.

Sections nest at the document level only. Inside container blocks
(open/sidebar/quote/delimited blocks, table cells) a heading stays a flat
`section_block`, because headings there are rare (usually discrete headings) and
nesting them would widen the blast radius for little gain.

An illegal level skip (for example `==` straight to `====`) still nests rather
than erroring: a section body accepts any deeper section, and the innermost open
section greedily claims a deeper heading.

For the same reason a top-level section may start at any level, not only `==`.
Asciidoctor warns about the out-of-sequence level but still renders it, and
include fragments such as Antora partials are usually written at the level they
will be included at. Requiring `==` turned such a file into one `ERROR` node.
The cost is parser size: the parser has to track which levels are open, and
letting any level open at the top roughly doubles those combinations (about
9,300 states became about 14,800). Avoiding that would mean moving section
boundaries into the external scanner.

## `section_block` is the content wrapper, and it stayed

`section_block` wraps a single content block (an optional attribute list / block
title plus one block). Keeping it as the wrapper, and routing only headings
through the new `section` nodes, is what allowed section nesting to be a purely
additive change: the existing highlight and injection queries match
`section_block` and kept working unchanged.

## Attribute lists are parsed into structure

Both block attribute lists (`[source#id.role%opt,ruby,cols="1,2"]`) and inline
macro attribute lists (`image:x[Alt Text,200,role=external]`) are parsed into
`positional_attr` and `named_attr` nodes, with the first positional carrying the
style and the `#id` / `.role` / `%option` shorthand. Earlier these were a single
opaque string.

Rules that fall out of matching Asciidoctor:

- A quoted value may contain commas (`["Arthur, King"]`); an unquoted value may
  not (the comma separates entries).
- An unquoted **named** value may contain `=` (a URL query string, say); a
  positional value may not, because the `=` is what makes an entry named.
- `stem` / `latexmath` / `pass` keep their attribute raw and unsplit, since math
  and passthrough content must not be comma-split.

The block and inline grammars use the same node names for these so consumers
learn one shape.

## Markdown compatibility, where Asciidoctor allows it

Asciidoctor accepts some Markdown syntax. We support what it does:

- ATX headings (`#`..`######`) map to the same section levels as `=`.
- Fenced code blocks (` ``` `) map onto the listing block, tracked with a
  dedicated stack kind so a backtick fence never closes a `----` listing of the
  same length. The opening fence's info string (the language) is pulled into the
  start marker.
- Markdown blockquotes (`>`) are supported.

## Text replacements use source forms

Inline replacements match what you type, not the rendered entity: `->`, `=>`,
`<-`, `<=`, `(C)`, `(R)`, `(TM)`, `...`. (An earlier version matched the HTML
entities like `-&gt;`, which never appear in source.)

## Counter attribute references

`{counter:name}` and the silent `{counter2:name}` (with an optional
`{counter:name:5}` seed) parse to a dedicated `counter` node. The function name
and its trailing colon are one token, so a plain `{counter}` reference still
resolves to an ordinary attribute reference by longest match.

## Generated parser is committed; use the pinned CLI

`src/parser.c` and friends are generated from `grammar.js` and committed, and CI
fails if they drift. Always regenerate with the tree-sitter CLI version pinned
in `package.json`. A different CLI version rewrites `src/tree_sitter/array.h` and
reshuffles tables, producing a noisy, version-specific diff.

## Parsers are generated with ABI 14

Both parsers are generated with `tree-sitter generate --abi 14`, one step below
the CLI's default of ABI 15. The decision was made so that the grammars load in
Emacs. Emacs builds that distributions ship are often linked against an older
tree-sitter runtime: Emacs 30.1 on Debian 13 uses tree-sitter 0.22. The 0.22 to
0.24 runtimes load ABI 13 and 14 only, and they reject an ABI 15 parser outright
with a `version-mismatch` error, so the editor gets no parser at all (#15,
bbatsov/asciidoc-mode#20). Newer runtimes, from 0.25 to at least 0.27, load
ABIs 13 to 15, so Zed and other current editors are unaffected.

What ABI 14 gives up, compared with 15:

- **Reserved words** (`reserved:` in `grammar.js`). Neither grammar uses them,
  so nothing changes today, but they cannot be added while the parsers stay on
  ABI 14.
- **Supertypes** (`supertypes:`). Neither grammar declares any.
  `ts_language_supertypes()` reports none.
- **The language name compiled into the parser.** `ts_language_name()` returns
  `NULL` instead of `"asciidoc"` or `"asciidoc_inline"`. Editors register a
  grammar under a name they choose, so loading is not affected. Code that asks
  the loaded language for its name gets nothing back.
- **Version metadata.** `ts_language_metadata()` returns `NULL` instead of the
  version from `tree-sitter.json`. A consumer that wants the grammar's version
  has to read it from `tree-sitter.json` or the release tag instead. As a side
  effect, `parser.c` no longer changes when the version is bumped, so the
  release job's regenerate step normally has nothing to commit.

Each grammar's `generate` script in `package.json` and its `Makefile` pass the
flag, and a hand-run `tree-sitter generate` must pass `--abi 14` as well.
Three checks keep it that way:

- `test/abi.sh` in each grammar, run by `pnpm test`, fails if `src/parser.c` is
  at any other ABI, or if `package.json` or the `Makefile` no longer passes the
  flag. `pnpm test` regenerates first, so locally it checks the regenerated
  parser rather than the one about to be committed.
- CI's regenerate check fails if the committed `src/parser.c` differs from what
  `pnpm generate` produces, which catches a parser regenerated by hand without
  the flag.
- CI's `old-runtime` job builds the committed parsers against the tree-sitter
  0.22 runtime and runs both corpora through it (`test/compat/run.sh`). This
  checks that the parser loads there, which a correct ABI number alone does not
  guarantee: a scanner change that calls a newer runtime function would still
  break it.

This may need to be reverted later. Move back to ABI 15 when one of these
happens:

- a grammar change needs reserved words or supertypes
- a consumer needs the language name or version metadata from the parser
- a future tree-sitter runtime drops support for ABI 14 (0.27 still loads
  ABIs 13 to 15)
- the Emacs builds that matter have moved to tree-sitter 0.25 or newer

To revert, change `--abi 14` to `--abi 15` in both grammars' `generate`
scripts and Makefiles, set `expected=15` in both `test/abi.sh` files, and
regenerate. Then either remove CI's `old-runtime` job or move its
`TREE_SITTER_REF` and `TREE_SITTER_SHA` to a 0.25 or newer runtime, because the
0.22 runtime rejects an ABI 15 parser. That cuts off the 0.22 to 0.24 runtimes
again, so record the change in the release notes.
