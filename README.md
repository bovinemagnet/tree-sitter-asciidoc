# tree-sitter-asciidoc

[![tree-sitter-asciidoc on crates.io](https://img.shields.io/crates/v/tree-sitter-asciidoc?label=tree-sitter-asciidoc)](https://crates.io/crates/tree-sitter-asciidoc)
[![tree-sitter-asciidoc-inline on crates.io](https://img.shields.io/crates/v/tree-sitter-asciidoc-inline?label=tree-sitter-asciidoc-inline)](https://crates.io/crates/tree-sitter-asciidoc-inline)

[tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for [AsciiDoc](https://docs.asciidoctor.org/asciidoc/latest/).

## About this fork

This is a fork of [cathaysia/tree-sitter-asciidoc](https://github.com/cathaysia/tree-sitter-asciidoc). Upstream was archived and made read-only in October 2026, so this fork is now maintained independently, on its `master` branch, for two projects that depend on it:

- [zed-asciidoc-antora](https://github.com/bovinemagnet/zed-asciidoc-antora), the AsciiDoc and Antora extension for the Zed editor
- [AdocFoundry](https://github.com/bovinemagnet/AdocFoundry), a desktop editor for AsciiDoc and Antora documentation

Both pin the grammars to a commit on `master`. As well as everything upstream released, `master` carries changes upstream never took:

- a `table_header_row` node, so a table's header row can be styled apart from its body, whether a blank line after the first row or a `header` option (`[%header]`, `options="header"`) makes it one
- table cells written as `| text` that can carry block content, such as an admonition or a listing, with the cell's text resuming after the block

The crates.io packages linked above are upstream's releases and include none of these changes. To use this fork, depend on this repository at a commit on `master`.

Please report issues and send pull requests to this repository rather than upstream.

## Runtime compatibility

The parsers are generated with tree-sitter ABI 14, so they load in any tree-sitter runtime that supports ABI 14, not only 0.25 and newer. CI checks this against the 0.22 runtime. That includes Emacs builds that distributions ship against older runtimes, such as Emacs 30.1 on Debian 13. With ABI 14, `ts_language_name()` and `ts_language_metadata()` return nothing for these grammars, and this may move back to ABI 15 later. See [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md#parsers-are-generated-with-abi-14) for the reasons and the trade-offs.
