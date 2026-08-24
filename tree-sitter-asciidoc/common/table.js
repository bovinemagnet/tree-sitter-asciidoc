const { anySep1, escaped_ch, anySep } = require('../../common/common');

// One or more `cell`s separated by `sep`, where any cell may be empty but the
// whole record may not (so a blank line is never mistaken for a row).
function dsvSep(sep, cell) {
  return choice(
    seq(cell, repeat(seq(sep, optional(cell)))),
    seq(sep, optional(cell), repeat(seq(sep, optional(cell)))),
  );
}

exports.rules = {
  // CSV (`,===`) and DSV (`:===`) tables share the `<delim>===` fence with
  // `|===` tables but separate their cells with a comma or colon instead of
  // a leading `|`.  Each record is one line of delimiter-separated cells.
  csv_table_block: $ =>
    prec.left(
      seq(
        $.csv_table_block_marker,
        repeat($.csv_record),
        $.csv_table_block_marker,
      ),
    ),
  // A record is non-empty (the bare newline after the opening fence is not a
  // row) but individual fields may be empty, as in `a,,c` or `,b`.
  csv_record: $ => seq(dsvSep(',', alias(repeat1(/[^,\r\n]/), $.table_cell_content)), $._block_end),

  dsv_table_block: $ =>
    prec.left(
      seq(
        $.dsv_table_block_marker,
        repeat($.dsv_record),
        $.dsv_table_block_marker,
      ),
    ),
  dsv_record: $ => seq(dsvSep(':', alias(repeat1(/[^:\r\n]/), $.table_cell_content)), $._block_end),

  table_block: $ =>
    prec.left(
      seq(
        $.table_block_marker,
        optional($.table_header_row),
        repeat(choice($.table_cell, $.ntable_block)),
        $.table_block_marker,
      ),
    ),
  // Carries no `prec.right`: that resolved the choice between reducing the cell
  // and extending it with block content in favour of extending, which put a
  // following `ntable_block` out of reach.  The `[$.table_cell]` conflict in
  // `grammar.js` leaves the choice open instead.
  table_cell: $ =>
    seq(
      optional($.table_cell_attr),
      choice(
        seq(
          '|',
          token.immediate(/\r?\n/),
          anySep(
            alias($._section_block, $.section_block),
            $.list_continuation,
          ),
        ),
        // A `| text` cell may carry block content after its text, as an
        // `a`-styled column does: `| text`, a blank line, then `[NOTE]`/`====`.
        seq(
          '|',
          $.table_cell_content,
          // Text may resume after a block: a cell often reads as prose, an
          // example listing, then more prose.  `table_cell_content` stops at
          // the next `|`, so a later run cannot reach into the following row
          // or past the closing fence any more than the first one can.
          repeat(
            seq(
              alias($._cell_block, $.section_block),
              optional(alias($._cell_text_after, $.table_cell_content)),
            ),
          ),
        ),
      ),
    ),
  // Only blocks that announce themselves with a marker may follow a cell's
  // text.  A paragraph or a list would equally match the `| ...` line that
  // starts the next row, so the cell would swallow the rest of the table;
  // those keep being absorbed into `table_cell_content` as flat text.  Tables
  // are left out for the same reason -- the closing `|===` would open a nested
  // table inside the cell rather than close the one the cell belongs to.
  _cell_block: $ =>
    seq(
      repeat($.element_attr),
      choice(
        $.delimited_block,
        $.listing_block,
        $.literal_block,
        $.open_block,
        $.sidebar_block,
        $.quoted_block,
        $.passthrough_block,
        $.admonition,
      ),
    ),
  // Text resuming after a block must start on a real character.  Letting it
  // start on whitespace would wrap the blank line between the block and the
  // next row in a content node of its own, which the surrounding blank lines
  // are already handled as.
  _cell_text_after: $ => seq(/[^|\s]/, repeat(choice(/[^|]/, '\\|'))),
  table_cell_content: $ => repeat1(choice(/[^|]/, '\\|')),

  // AsciiDoc promotes a table's first line to a header row when a blank line follows it,
  // which is also the layout `[%header]` and `options="header"` tables are written in.
  // `_table_header_start` is a zero-width token the scanner emits only in that case; the
  // attribute list itself sits outside `table_block` and is not visible from here.
  // Settling the question before any cell is read keeps header and body cells out of one
  // parse state, so the two can end their content differently.
  table_header_row: $ =>
    seq($._table_header_start, repeat1($.table_cell), $._table_header_row_end),
  // Matched as one token so that it outruns the single character a cell's content would
  // otherwise take the first newline as.
  _table_header_row_end: _ => token(seq(/\r?\n/, /[ \t]*/, /\r?\n/)),

  ntable_block: $ =>
    prec.left(
      seq(
        optional($.element_attr),
        $.ntable_block_marker,
        repeat($.ntable_cell),
        $.ntable_block_marker,
      ),
    ),
  ntable_cell: $ =>
    seq(
      optional($.table_cell_attr),
      choice(
        seq(
          '!',
          token.immediate(/\r?\n/),
          alias($._section_block, $.section_block),
        ),
        seq('!', repeat1(escaped_ch('!'))),
      ),
    ),
};
