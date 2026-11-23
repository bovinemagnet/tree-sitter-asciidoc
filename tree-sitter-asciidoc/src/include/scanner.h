#pragma once

#include "base_types.h"
#include "quick_buffer.h"

typedef enum BlockKind {
    BLOCK_KIND_DELIMITED,
    BLOCK_KIND_TABLE,
    BLOCK_KIND_LISTING,
    BLOCK_KIND_FENCED,
    BLOCK_KIND_LITERAL,
    BLOCK_KIND_SIDEBAR,
    BLOCK_KIND_QUOTED,
    BLOCK_KIND_CSV_TABLE,
    BLOCK_KIND_DSV_TABLE
} BlockKind;

typedef struct Node {
    BlockKind kind;
    usize counter;
} Node;

typedef struct Scanner {
    usize capacity;
    usize len;
    Node *buffer;
    /// Set while an opening `|===` is being scanned, when the table opens with a header
    /// row. Read back on the very next token, so nothing can come between the two.
    bool header_pending;
    /// Set with `header_pending` when the header row comes from a header option rather
    /// than a blank line, so it ends with its first line. Read back by the line ending
    /// that closes that line.
    bool header_line_end_pending;
    /// `TABLE_HEADER_OPTION_*` bits from the block attribute lists in front of the next
    /// `|===`, and the column count their `cols` gives, or 0. Recorded when the `[` of
    /// such a list is scanned, and only when the list leads to a table fence, because
    /// the blocks in between may not pass through the scanner at all: an option on a
    /// paragraph must not reach a later table. Cleared by the next `|===`.
    u8 table_header_option;
    usize table_header_cols;
} Scanner;

/// Options in a block attribute list that decide whether a table has a header row.
enum {
    TABLE_HEADER_OPTION_HEADER = 1,
    TABLE_HEADER_OPTION_NOHEADER = 2,
};

/// How the row after an opening `|===` is read.
typedef enum HeaderRow {
    HEADER_ROW_NONE,
    /// The first line, followed by a blank one.
    HEADER_ROW_IMPLICIT,
    /// The first row, made a header by an option, ending with its line.
    HEADER_ROW_FIRST_LINE,
} HeaderRow;

static inline void scanner_init(Scanner *self);
static inline void scanner_free(Scanner *self);

static inline Node *scanner_top(Scanner const *self);
static inline bool scanner_is_matching_raw_block(Scanner *self);
static inline bool scanner_is_expect_block_start(Scanner const *self);
/**
 * @brief check scanner is matching a block
 *
 * @param self Scanner
 * @param kind BlockKind
 * @param counter block counter, zero means any counter.
 * @return true if matched.
 */
static inline bool scanner_is_matching(Scanner const *self, BlockKind kind, usize counter);
static inline Result scanner_serialize(Scanner const *self, QuickBuffer *qb);
static inline Result scanner_deserialize(Scanner *self, QuickBuffer *qb);
/**
 * @brief pop if scanner_is_matching block
 *
 * @param self Scanner
 * @param kind BlockKind
 * @param counter block counter, zero means any counter.
 * @return true if pop a block.
 */
static inline bool scanner_pop_kind(Scanner *self, BlockKind kind, usize counter);
static inline void scanner_pop(Scanner *self);
static inline void scanner_push(Scanner *self, BlockKind kind, usize counter);
