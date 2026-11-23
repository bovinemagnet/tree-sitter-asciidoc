#include "include/scanner.h"
#include "include/base_types.h"
#include "include/quick_buffer.h"
#include "include/utils.h"
#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#ifndef LIST_INIT_SIZE
#define LIST_INIT_SIZE 10
#endif  // ifndef LIST_INIT_SIZE

#ifndef LIST_GROW_SIZE
#define LIST_GROW_SIZE 10
#endif  // ifndef LIST_GROW_SIZE

void *tree_sitter_asciidoc_external_scanner_create() {
    Scanner *scanner = (Scanner *)ts_malloc(sizeof(Scanner));
    scanner_init(scanner);
    return scanner;
}

void tree_sitter_asciidoc_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    scanner_free(scanner);
    ts_free(scanner);
}

unsigned tree_sitter_asciidoc_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;

    QuickBuffer qb = quick_buffer_new(buffer, TREE_SITTER_SERIALIZATION_BUFFER_SIZE);

    scanner_serialize(scanner, &qb);

    return qb.pos;
}

void tree_sitter_asciidoc_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *s = (Scanner *)payload;
    s->len = 0;
    s->header_pending = false;
    s->header_line_end_pending = false;
    s->table_header_option = 0;
    s->table_header_cols = 0;

    if(!buffer || length == 0) {
        return;
    }

    QuickBuffer qb = quick_buffer_new((void *)buffer, length);
    scanner_deserialize(s, &qb);
}

bool tree_sitter_asciidoc_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *s = (Scanner *)payload;

    if(lexer->eof(lexer)) {
        if(valid_symbols[TOKEN_TYPE_EOF]) {
            lexer->result_symbol = TOKEN_TYPE_EOF;
            return true;
        }
    }

    // Whether the table opened with a header row was settled while its `|===` was scanned,
    // so this only has to hand the answer over. Nothing is consumed either way, which is
    // what lets the rest of the scanner run untouched when there is no header row.
    if(valid_symbols[TOKEN_TABLE_HEADER_START] && s->header_pending) {
        s->header_pending = false;
        lexer->mark_end(lexer);
        lexer->result_symbol = TOKEN_TABLE_HEADER_START;
        return true;
    }

    // A header row made by a header option ends with its line, where a body cell's
    // content would run on to the next `|`. The row was checked to fit on that line when
    // its `|===` was scanned.
    if(valid_symbols[TOKEN_TABLE_HEADER_LINE_END] && s->header_line_end_pending &&
       is_newline(lexer->lookahead)) {
        s->header_line_end_pending = false;
        skip_line_ending(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol = TOKEN_TABLE_HEADER_LINE_END;
        return true;
    }

    usize start_pos = lexer->get_column(lexer);

    if(valid_symbols[TOKEN_BLOCK_COMMENT_END_MARKER]) {
        if(start_pos != 0) {
            return false;
        }
        if(parse_sequence(lexer, "////")) {
            if(is_newline(lexer->lookahead)) {
                lexer->result_symbol = TOKEN_BLOCK_COMMENT_END_MARKER;
                return true;
            }
        }

        return false;
    }

    // The line ending inside a paragraph joins the next line to it only when that
    // line is prose. A blank line, the end of input or a line that could open a
    // block is left to `_block_end`, which ends the paragraph. In error recovery
    // every symbol is valid, and the block-comment check above has already
    // returned, so this never runs then.
    if(valid_symbols[TOKEN_PARAGRAPH_SOFT_BREAK] && is_newline(lexer->lookahead)) {
        skip_line_ending(lexer);
        lexer->mark_end(lexer);
        if(continues_paragraph(lexer)) {
            lexer->result_symbol = TOKEN_PARAGRAPH_SOFT_BREAK;
            return true;
        }
        return false;
    }

    if(start_pos == 0) {
        if(valid_symbols[TOKEN_ADMONITION_NOTE]) {
            do {
                if(parse_sequence(lexer, "NOTE")) {
                    lexer->result_symbol = TOKEN_ADMONITION_NOTE;
                } else if(parse_sequence(lexer, "TIP")) {
                    lexer->result_symbol = TOKEN_ADMONITION_TIP;
                } else if(parse_sequence(lexer, "IMPORTANT")) {
                    lexer->result_symbol = TOKEN_ADMONITION_IMPORTANT;
                } else if(parse_sequence(lexer, "CAUTION")) {
                    lexer->result_symbol = TOKEN_ADMONITION_CAUTION;
                } else if(parse_sequence(lexer, "WARNING")) {
                    lexer->result_symbol = TOKEN_ADMONITION_WARNING;
                } else {
                    break;
                }

                // mark_end caps the token at the keyword word; the colon and
                // space are only lookahead.  Advance them with skip=false: a
                // skip=true advance after mark_end would collapse the token to
                // a zero-width marker (the label would fall outside the tree).
                lexer->mark_end(lexer);
                if(lexer->lookahead == ':') {
                    lexer->advance(lexer, false);
                    if(lexer->lookahead == ' ') {
                        return true;
                    }
                }
            } while(0);
        }

        bool is_alpha_lower = is_ascii_alpha_lower(lexer->lookahead);
        if(valid_symbols[TOKEN_LIST_MARKER_ALPHA]) {
            if(parse_ordered_marker(lexer)) {
                return true;
            }
        }

        if(is_alpha_lower && scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME]) {
                if(parse_sequence(lexer, "include")) {
                    lexer->mark_end(lexer);
                    if(parse_sequence(lexer, "::")) {
                        lexer->result_symbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        if(is_alpha_lower && !scanner_is_matching_raw_block(s)) {
            if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] || valid_symbols[TOKEN_TERM]) {
                while(is_ascii_alpha_lower(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                lexer->mark_end(lexer);
                i32 c = lexer->lookahead;
                if(c == ':' || c == ';') {
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool followed_ws = is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer);
                    bool desc_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    // `name:: ` (delimiter followed by space/EOL) is a
                    // description-list term, whereas `name::target[]` is a
                    // block macro.  mark_end sits after the bare word, so the
                    // delimiter run we just consumed is only lookahead.
                    if(valid_symbols[TOKEN_TERM] && desc_run && followed_ws) {
                        lexer->result_symbol = TOKEN_TERM;
                        return true;
                    }
                    if(valid_symbols[TOKEN_BLOCK_MACRO_NAME] && c == ':' && run >= 2) {
                        lexer->result_symbol = TOKEN_BLOCK_MACRO_NAME;
                        return true;
                    }
                }
            }
        }

        if(lexer->get_column(lexer) == 0) {
            switch(lexer->lookahead) {
                case '=': {
                    consume('=', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    if(!scanner_is_matching_raw_block(s)) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_new_line(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DELIMITED, counter)) {
                                lexer->result_symbol = TOKEN_DELIMITED_BLOCK_END_MARKER;
                                scanner_pop(s);
                            } else {
                                scanner_push(s, BLOCK_KIND_DELIMITED, counter);
                                lexer->result_symbol = TOKEN_DELIMITED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }

                    if(!scanner_is_matching_raw_block(s)) {
                        usize level = TOKEN_TITLE_H0_MARKER - 1 + lexer->get_column(lexer);
                        if(level <= TOKEN_TITLE_H5_MARKER && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = level;
                            return true;
                        }
                    }
                    break;
                }
                case '#': {
                    // Markdown-style ATX heading (compat syntax): a run of `#`
                    // maps to the same section levels as `=` (`#` -> level 0,
                    // `######` -> level 5), requiring a space before the title.
                    consume('#', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    if(!scanner_is_matching_raw_block(s)) {
                        usize level = TOKEN_TITLE_H0_MARKER - 1 + lexer->get_column(lexer);
                        if(level <= TOKEN_TITLE_H5_MARKER && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = level;
                            return true;
                        }
                    }
                    break;
                }
                case '*': {
                    usize counter = 0;
                    consume('*', lexer, false, &counter, USIZE_MAX);
                    lexer->mark_end(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);

                    if(valid_symbols[TOKEN_SIDEBAR_BLOCK_START_MARKER] || valid_symbols[TOKEN_SIDEBAR_BLOCK_END_MARKER]) {
                        usize col = lexer->get_column(lexer);
                        if(col >= 4 && (is_new_line(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_SIDEBAR, col)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_SIDEBAR_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_SIDEBAR, col);
                                    lexer->result_symbol = TOKEN_SIDEBAR_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '*') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_STAR]) {
                        lexer->result_symbol = TOKEN_LIST_MARKER_STAR;
                        return is_unordered_marker;
                    }

                    break;
                }
                case '-': {
                    usize counter = 0;
                    consume('-', lexer, false, &counter, USIZE_MAX);
                    lexer->mark_end(lexer);
                    bool is_unordered_marker = is_white_space(lexer->lookahead);
                    if(valid_symbols[TOKEN_OPEN_BLOCK_MARKER]) {
                        if(lexer->get_column(lexer) == 2 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_OPEN_BLOCK_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_QUOTED_PARAGRAPH_MARKER]) {
                        if(lexer->get_column(lexer) == 2 && is_white_space(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_QUOTED_PARAGRAPH_MARKER;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LISTING_BLOCK_START_MARKER] || valid_symbols[TOKEN_LISTING_BLOCK_END_MARKER]) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LISTING, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LISTING_BLOCK_END_MARKER;
                                return true;
                            } else {
                                if(!scanner_is_matching_raw_block(s)) {
                                    scanner_push(s, BLOCK_KIND_LISTING, counter);
                                    lexer->result_symbol = TOKEN_LISTING_BLOCK_START_MARKER;
                                    return true;
                                }
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        skip_white_space(lexer);
                        while(lexer->lookahead == '-') {
                            lexer->advance(lexer, false);
                            skip_white_space(lexer);
                            ++counter;
                            if(counter > 3) {
                                break;
                            }
                        }
                        if(counter == 3 && is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_HYPHEN] && is_unordered_marker) {
                        lexer->result_symbol = TOKEN_LIST_MARKER_HYPHEN;
                        return true;
                    }
                    break;
                }
                case '`': {
                    // Markdown-style fenced code block (compat): a line of
                    // 3+ backticks, optionally followed by an info string
                    // (language) on the opening fence.  It reuses the listing
                    // block markers but is tracked with a dedicated stack kind
                    // so a backtick fence never closes a `----` block of the
                    // same length.
                    usize counter = 0;
                    consume('`', lexer, false, &counter, USIZE_MAX);
                    if((valid_symbols[TOKEN_LISTING_BLOCK_START_MARKER] ||
                        valid_symbols[TOKEN_LISTING_BLOCK_END_MARKER]) &&
                       counter >= 3) {
                        if(scanner_is_matching(s, BLOCK_KIND_FENCED, counter)) {
                            // Closing fence: bare backticks only.
                            if(is_newline(lexer->lookahead) || is_eof(lexer)) {
                                lexer->mark_end(lexer);
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LISTING_BLOCK_END_MARKER;
                                return true;
                            }
                        } else if(!scanner_is_matching_raw_block(s)) {
                            // Opening fence: pull the rest of the line (the
                            // info string) into the marker so the body starts
                            // on the next line.
                            while(!is_newline(lexer->lookahead) && !is_eof(lexer)) {
                                lexer->advance(lexer, false);
                            }
                            lexer->mark_end(lexer);
                            scanner_push(s, BLOCK_KIND_FENCED, counter);
                            lexer->result_symbol = TOKEN_LISTING_BLOCK_START_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '.': {
                    consume('.', lexer, false, NULL, USIZE_MAX);
                    lexer->mark_end(lexer);
                    if(valid_symbols[TOKEN_LITERAL_BLOCK_MARKER]) {
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_LITERAL, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            } else if(!scanner_is_matching_raw_block(s)) {
                                scanner_push(s, BLOCK_KIND_LITERAL, counter);
                                lexer->result_symbol = TOKEN_LITERAL_BLOCK_MARKER;
                                return true;
                            }
                        }
                    }

                    if(valid_symbols[TOKEN_LIST_MARKER_DOT]) {
                        if(is_white_space(lexer->lookahead)) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_LIST_MARKER_DOT;
                            return true;
                        }
                    }

                    if(valid_symbols[TOKEN_BLOCK_TITLE_MARKER]) {
                        if(lexer->get_column(lexer) == 1) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_BLOCK_TITLE_MARKER;
                            return true;
                        }
                    }

                    break;
                }
                case ':': {  // DSV table (:===) or document attribute (:name:)
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);  // document-attr marker is just the colon
                    if(valid_symbols[TOKEN_DSV_TABLE_BLOCK_MARKER] && lexer->lookahead == '=') {
                        // Count the `=` run by hand rather than with consume(),
                        // which would move mark_end past the colon and corrupt
                        // the document-attr fallback below.
                        usize counter = 0;
                        while(lexer->lookahead == '=') {
                            lexer->advance(lexer, false);
                            ++counter;
                        }
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_DSV_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_DSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_DSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    if(valid_symbols[TOKEN_DOCUMENT_ATTR_MARKER]) {
                        lexer->result_symbol = TOKEN_DOCUMENT_ATTR_MARKER;
                        return true;
                    }
                    break;
                }
                case '[': {
                    if(valid_symbols[TOKEN_ELEMENT_ATTR_MARKER]) {
                        lexer->advance(lexer, false);
                        // `[[` is a block anchor, not an attribute list.  `[#`
                        // is the id shorthand (`[#id]`) and is handled by the
                        // grammar, so it must not be rejected here.
                        if(lexer->lookahead == '[') {
                            return false;
                        }
                        lexer->mark_end(lexer);
                        lexer->result_symbol = TOKEN_ELEMENT_ATTR_MARKER;
                        scan_attr_list_for_table(s, lexer);
                        return true;
                    }
                    break;
                }
                case '\'': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('\'', lexer)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    break;
                }
                case '<': {
                    if(valid_symbols[TOKEN_BREAKS_MARKS]) {
                        if(parse_breaks('<', lexer)) {
                            lexer->result_symbol = TOKEN_BREAKS_MARKS;
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_CALLOUT_LIST_MARKER]) {
                        if(lexer->get_column(lexer) == 1) {
                            if(parse_sequence(lexer, ".>")) {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_CALLOUT_LIST_MARKER;
                                if(is_white_space(lexer->lookahead)) {
                                    return true;
                                }
                            }
                            if(parse_number(lexer)) {
                                if(lexer->lookahead == '>') {
                                    lexer->advance(lexer, false);
                                    lexer->mark_end(lexer);
                                    if(is_white_space(lexer->lookahead)) {
                                        lexer->result_symbol = TOKEN_CALLOUT_LIST_MARKER;
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case ',': {  // CSV table
                    if(valid_symbols[TOKEN_CSV_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_CSV_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_CSV_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_CSV_TABLE, counter);
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '|': {  // Table
                    if(valid_symbols[TOKEN_TABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        usize counter = 0;
                        consume('=', lexer, false, &counter, USIZE_MAX);
                        if(counter >= 3 && is_newline(lexer->lookahead)) {
                            if(scanner_is_matching(s, BLOCK_KIND_TABLE, 0)) {
                                if(scanner_is_matching(s, BLOCK_KIND_TABLE, counter)) {
                                    lexer->mark_end(lexer);
                                    lexer->result_symbol = TOKEN_TABLE_BLOCK_MARKER;
                                    scanner_pop(s);
                                    // An option recorded in front of a fence that turned
                                    // out to close a table must not reach the next one.
                                    s->table_header_option = 0;
                                    s->table_header_cols = 0;
                                    s->header_line_end_pending = false;
                                    return true;
                                }
                            } else {
                                lexer->mark_end(lexer);
                                lexer->result_symbol = TOKEN_TABLE_BLOCK_MARKER;
                                scanner_push(s, BLOCK_KIND_TABLE, counter);
                                HeaderRow row =
                                    scan_header_row(lexer, s->table_header_option, s->table_header_cols);
                                s->table_header_option = 0;
                                s->table_header_cols = 0;
                                s->header_pending = row != HEADER_ROW_NONE;
                                s->header_line_end_pending = row == HEADER_ROW_FIRST_LINE;
                                return true;
                            }
                        }
                    }
                    break;
                }
                case '!': {  // NTable
                    if(valid_symbols[TOKEN_NTABLE_BLOCK_MARKER]) {
                        lexer->advance(lexer, false);
                        consume('=', lexer, false, NULL, USIZE_MAX);
                        if(is_newline(lexer->lookahead)) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_NTABLE_BLOCK_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '/': {
                    if(valid_symbols[TOKEN_LINE_COMMENT_MARKER] ||
                       valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                        if(parse_sequence(lexer, "//")) {
                            lexer->mark_end(lexer);
                            if(valid_symbols[TOKEN_BLOCK_COMMENT_START_MARKER]) {
                                if(parse_sequence(lexer, "//")) {
                                    if(is_newline(lexer->lookahead)) {
                                        lexer->result_symbol = TOKEN_BLOCK_COMMENT_START_MARKER;
                                        lexer->mark_end(lexer);
                                        return true;
                                    }
                                }
                            }
                            lexer->result_symbol = TOKEN_LINE_COMMENT_MARKER;
                            return true;
                        }
                    }
                    break;
                }
                case '_': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_START_MARKER] || valid_symbols[TOKEN_QUOTED_BLOCK_END_MARKER]) {
                        consume('_', lexer, false, NULL, USIZE_MAX);
                        lexer->mark_end(lexer);
                        usize counter = lexer->get_column(lexer);
                        if(counter >= 4 && (is_newline(lexer->lookahead) || is_eof(lexer))) {
                            if(scanner_is_matching(s, BLOCK_KIND_QUOTED, counter)) {
                                scanner_pop(s);
                                lexer->result_symbol = TOKEN_QUOTED_BLOCK_END_MARKER;
                            } else {
                                scanner_push(s, BLOCK_KIND_QUOTED, counter);
                                lexer->result_symbol = TOKEN_QUOTED_BLOCK_START_MARKER;
                            }
                            return true;
                        }
                    }
                    break;
                }
                case '>': {
                    if(valid_symbols[TOKEN_QUOTED_BLOCK_MD_MARKER]) {
                        lexer->advance(lexer, false);
                        lexer->mark_end(lexer);
                        lexer->result_symbol = TOKEN_QUOTED_BLOCK_MD_MARKER;
                        if(is_white_space(lexer->lookahead) || is_eof(lexer) || is_newline(lexer->lookahead)) {
                            return true;
                        }
                    }
                    break;
                }
                case '+': {
                    lexer->advance(lexer, false);
                    if(valid_symbols[TOKEN_LIST_CONTINUATION]) {
                        if(is_newline(lexer->lookahead)) {
                            lexer->result_symbol = TOKEN_LIST_CONTINUATION;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }
                    if(valid_symbols[TOKEN_PASSTHROUGH_BLOCK_MARKER]) {
                        if(parse_sequence(lexer, "+++")) {
                            lexer->mark_end(lexer);
                            lexer->result_symbol = TOKEN_PASSTHROUGH_BLOCK_MARKER;
                            if(is_new_line(lexer->lookahead) || is_eof(lexer)) {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        if(
            lexer->get_column(lexer) == 0 && valid_symbols[TOKEN_IDENT_MARKER] && is_white_space(lexer->lookahead)
        ) {
            skip_white_space(lexer);
            if(!is_new_line(lexer->lookahead) && !is_eof(lexer)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = TOKEN_IDENT_MARKER;
                return true;
            }
        }

        // Description-list term: the run of text at the start of a line that
        // is terminated by a description marker (`::`/`:::`/`::::`/`;;`
        // followed by whitespace or a line break), e.g. the `CPU` in
        // `CPU:: the brain`.  This is probed last, so every other line-start
        // construct gets first refusal; the emitted token spans from column 0
        // (where the scan began) to just before the delimiter run regardless
        // of how far earlier probes advanced the cursor.  When no marker is
        // found it returns false and the line stays an ordinary paragraph.
        if(valid_symbols[TOKEN_TERM] && !scanner_is_matching_raw_block(s)) {
            usize term_len = 0;
            while(!is_newline(lexer->lookahead) && !is_eof(lexer)) {
                i32 c = lexer->lookahead;
                if((c == ':' || c == ';') && term_len > 0) {
                    lexer->mark_end(lexer);
                    usize run = 0;
                    while(lexer->lookahead == c) {
                        lexer->advance(lexer, false);
                        ++run;
                    }
                    bool valid_run = (c == ':' && run >= 2 && run <= 4) || (c == ';' && run == 2);
                    if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
                        lexer->result_symbol = TOKEN_TERM;
                        return true;
                    }
                    term_len += run;
                    continue;
                }
                lexer->advance(lexer, false);
                ++term_len;
            }
            return false;
        }
    }

    // Description-list marker: `::`, `:::`, `::::` or `;;`, always followed
    // by whitespace or a line break.  It is only valid once a term has been
    // read, i.e. mid-line, so the column-0 block above is skipped and the
    // lexer sits on the delimiter run.  A run that is not a valid marker
    // (e.g. a lone `:` in a URL or `a::b` without trailing space) returns
    // false so the delimiter falls back to ordinary term text.
    if(valid_symbols[TOKEN_DESCRIPTION_MARKER] &&
       (lexer->lookahead == ':' || lexer->lookahead == ';')) {
        i32 delim = lexer->lookahead;
        usize run = 0;
        while(lexer->lookahead == delim) {
            lexer->advance(lexer, false);
            ++run;
        }
        bool valid_run = (delim == ':' && run >= 2 && run <= 4) || (delim == ';' && run == 2);
        if(valid_run && (is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) || is_eof(lexer))) {
            lexer->mark_end(lexer);
            lexer->result_symbol = TOKEN_DESCRIPTION_MARKER;
            return true;
        }
        return false;
    }

    if(valid_symbols[TOKEN_CALLOUT_MARKER]) {
        parse_sequence(lexer, "#");
        parse_sequence(lexer, "//");
        parse_sequence(lexer, ";;");
        skip_white_space(lexer);

        if(parse_sequence(lexer, "<.>")) {
            lexer->mark_end(lexer);
            if(is_newline(lexer->lookahead)) {
                lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                return true;
            }
        }

        if(parse_number(lexer)) {
            if(lexer->lookahead == '>') {
                lexer->advance(lexer, false);
                lexer->mark_end(lexer);
                if(is_newline(lexer->lookahead)) {
                    lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                    return true;
                }
            }
        }

        if(parse_sequence(lexer, "!--")) {
            if(lexer->lookahead == '.' || parse_number(lexer)) {
                if(parse_sequence(lexer, "-->")) {
                    lexer->mark_end(lexer);
                    if(is_new_line(lexer->lookahead)) {
                        lexer->result_symbol = TOKEN_CALLOUT_MARKER;
                        return true;
                    }
                }
            }
        }
    }

    if(
        start_pos == lexer->get_column(lexer) &&
        valid_symbols[TOKEN_CELL_ATTR]
    ) {
        bool has_token = false;
        while(parse_table_attr(lexer)) {
            has_token = true;
        }

        if(has_token) {
            if(lexer->lookahead == '|' || lexer->lookahead == '!') {
                lexer->result_symbol = TOKEN_CELL_ATTR;
                lexer->mark_end(lexer);
                return true;
            }
        }
    }

    return false;
}

bool parse_ordered_marker(TSLexer *lexer) {
    if(lexer->get_column(lexer) != 0) {
        return false;
    }

    if(is_ascii_digit(lexer->lookahead)) {
        while(is_ascii_digit(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        lexer->result_symbol = TOKEN_LIST_MARKER_DIGIT;
    } else if(is_ascii_alpha_lower(lexer->lookahead)) {
        lexer->result_symbol = TOKEN_LIST_MARKER_ALPHA;
        lexer->advance(lexer, false);
    } else if(is_geek_lower(lexer->lookahead)) {
        lexer->result_symbol = TOKEN_LIST_MARKER_GEEK;
        lexer->advance(lexer, false);
    } else {
        return false;
    }

    if(lexer->lookahead != '.') {
        return false;
    }
    lexer->advance(lexer, false);
    if(!is_white_space(lexer->lookahead)) {
        return false;
    }
    lexer->mark_end(lexer);
    return true;
}

static inline bool is_white_space(i32 ch) {
    return ch == ' ' || ch == '\t';
}

static inline bool is_ascii_digit(i32 ch) {
    return ch >= '0' && ch <= '9';
}

static inline bool is_ascii_alpha_lower(i32 ch) {
    return ch >= 'a' && ch <= 'z';
}

static inline bool is_geek_lower(i32 ch) {
    return ch >= 945 && ch <= 969;
}

static inline bool parse_breaks(char start, TSLexer *lexer) {
    i32 counter = 0;

    while(lexer->lookahead == start) {
        lexer->advance(lexer, false);
        skip_white_space(lexer);
        ++counter;
    }

    lexer->mark_end(lexer);

    return counter >= 3 && is_newline(lexer->lookahead);
}

static inline bool skip_white_space(TSLexer *lexer) {
    bool has_skiped = false;
    while(is_white_space(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_skiped = true;
    }

    return has_skiped;
}

static inline bool is_newline(i32 ch) {
    return ch == '\r' || ch == '\n';
}

/// Consume one line ending, if there is one.
static inline bool skip_line_ending(TSLexer *lexer) {
    if(lexer->lookahead != '\r' && lexer->lookahead != '\n') {
        return false;
    }
    if(lexer->lookahead == '\r') {
        lexer->advance(lexer, false);
    }
    if(lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
    }
    return true;
}

/// Decides how the row after an opening `|===` is read. AsciiDoc promotes a table's first
/// line to its header row when a blank line follows it; a `header` option makes the first
/// row the header row without one, and a `noheader` option stops either.
///
/// A row made a header by an option has to end with its line, so it is only accepted when
/// the line is sure to be the whole row: it holds at least one cell, does not end in a
/// bare `|` (whose cell would take the line ending), and, when `cols` gives the column
/// count, holds exactly that many cells. A header row written over several lines is left
/// unmarked rather than half marked.
///
/// Called with the lexer sitting on the line ending of an opening `|===`, whose token has
/// already been marked, so everything read here is lookahead and widens nothing.
static inline HeaderRow scan_header_row(TSLexer *lexer, u8 header_option, usize header_cols) {
    if((header_option & TABLE_HEADER_OPTION_NOHEADER) && !(header_option & TABLE_HEADER_OPTION_HEADER)) {
        return HEADER_ROW_NONE;
    }
    bool explicit_header = header_option & TABLE_HEADER_OPTION_HEADER;
    if(!skip_line_ending(lexer)) {
        return HEADER_ROW_NONE;
    }

    for(bool first_line = true;; first_line = false) {
        // The line has to be cells, not the delimiter closing an empty table, which a
        // blank line may equally well follow.
        bool closes_the_table = lexer->lookahead == '|';
        usize delimiter_run = 0;

        bool has_content = false;
        bool blank = true;
        usize cells = 0;
        bool escaped = false;
        bool ends_with_separator = false;
        while(!is_eof(lexer) && !is_newline(lexer->lookahead)) {
            i32 ch = lexer->lookahead;
            has_content = true;
            if(closes_the_table && !is_white_space(ch)) {
                if(ch == '=' && delimiter_run < USIZE_MAX) {
                    ++delimiter_run;
                } else if(delimiter_run > 0) {
                    closes_the_table = false;
                }
            }
            if(!is_white_space(ch)) {
                blank = false;
                ends_with_separator = ch == '|' && !escaped;
            }
            if(ch == '|' && !escaped && cells < USIZE_MAX) {
                ++cells;
            }
            escaped = ch == '\\' && !escaped;
            lexer->advance(lexer, false);
        }
        if(closes_the_table && delimiter_run >= 3) {
            return HEADER_ROW_NONE;
        }

        if(first_line && has_content) {
            if(skip_line_ending(lexer)) {
                while(is_white_space(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                if(is_newline(lexer->lookahead)) {
                    return HEADER_ROW_IMPLICIT;
                }
            }
        }
        if(!explicit_header) {
            return HEADER_ROW_NONE;
        }
        if(!has_content) {
            // A blank line between the fence and the first row; the row is on a later line.
            if(!skip_line_ending(lexer)) {
                return HEADER_ROW_NONE;
            }
            continue;
        }
        if(blank || cells == 0 || ends_with_separator) {
            return HEADER_ROW_NONE;
        }
        if(header_cols != 0 && cells != header_cols) {
            return HEADER_ROW_NONE;
        }
        return HEADER_ROW_FIRST_LINE;
    }
}

#define ATTR_LIST_MAX 256

static inline bool attr_word_is(char const *word, usize len, char const *expected) {
    usize i = 0;
    for(; i < len && expected[i] != '\0'; ++i) {
        if(word[i] != expected[i]) {
            return false;
        }
    }
    return i == len && expected[i] == '\0';
}

static inline u8 attr_header_option(char const *word, usize len) {
    if(attr_word_is(word, len, "header")) {
        return TABLE_HEADER_OPTION_HEADER;
    }
    if(attr_word_is(word, len, "noheader")) {
        return TABLE_HEADER_OPTION_NOHEADER;
    }
    return 0;
}

static inline void attr_trim(char const **text, usize *len) {
    while(*len > 0 && is_white_space((*text)[0])) {
        ++*text;
        --*len;
    }
    while(*len > 0 && is_white_space((*text)[*len - 1])) {
        --*len;
    }
}

/// The column count a `cols` value gives: a lone number is that many columns, otherwise
/// each comma- or semicolon-separated entry is one column, or `n` for `n*`. 0 when unsure.
static inline usize attr_column_count(char const *value, usize len) {
    usize entries = 0;
    usize columns = 0;
    usize i = 0;
    bool lone_number = true;
    usize lone_value = 0;
    while(i <= len) {
        usize start = i;
        while(i < len && value[i] != ',' && value[i] != ';') {
            ++i;
        }
        char const *entry = value + start;
        usize entry_len = i - start;
        attr_trim(&entry, &entry_len);
        if(entry_len == 0) {
            return 0;
        }
        ++entries;
        usize digits = 0;
        usize number = 0;
        while(digits < entry_len && is_ascii_digit(entry[digits]) && number < 1000) {
            number = number * 10 + (usize)(entry[digits] - '0');
            ++digits;
        }
        if(digits < entry_len && entry[digits] == '*') {
            if(digits == 0 || number == 0) {
                return 0;
            }
            columns += number;
            lone_number = false;
        } else {
            columns += 1;
            if(digits != entry_len) {
                lone_number = false;
            }
            lone_value = number;
        }
        ++i;
    }
    if(entries == 1 && lone_number) {
        return lone_value;
    }
    return columns;
}

/// Reads the header options and `cols` out of the text of one block attribute list.
static inline void attr_list_options(char const *text, usize len, u8 *option, usize *cols) {
    usize i = 0;
    for(bool first_entry = true; i <= len; first_entry = false) {
        usize start = i;
        char quote = 0;
        while(i < len && (quote || text[i] != ',')) {
            if(quote && text[i] == quote) {
                quote = 0;
            } else if(!quote && (text[i] == '"' || text[i] == '\'')) {
                quote = text[i];
            }
            ++i;
        }
        char const *entry = text + start;
        usize entry_len = i - start;
        ++i;
        attr_trim(&entry, &entry_len);

        usize eq = 0;
        while(eq < entry_len && entry[eq] != '=' && entry[eq] != '"' && entry[eq] != '\'') {
            ++eq;
        }
        if(eq < entry_len && entry[eq] == '=') {
            char const *name = entry;
            usize name_len = eq;
            attr_trim(&name, &name_len);
            char const *value = entry + eq + 1;
            usize value_len = entry_len - eq - 1;
            attr_trim(&value, &value_len);
            if(value_len >= 2 && (value[0] == '"' || value[0] == '\'') && value[value_len - 1] == value[0]) {
                ++value;
                value_len -= 2;
            }
            if(attr_word_is(name, name_len, "options") || attr_word_is(name, name_len, "opts")) {
                usize j = 0;
                while(j <= value_len) {
                    usize word_start = j;
                    while(j < value_len && value[j] != ',') {
                        ++j;
                    }
                    char const *word = value + word_start;
                    usize word_len = j - word_start;
                    attr_trim(&word, &word_len);
                    *option |= attr_header_option(word, word_len);
                    ++j;
                }
            } else if(attr_word_is(name, name_len, "cols")) {
                *cols = attr_column_count(value, value_len);
            }
        } else if(first_entry) {
            // The style slot may carry `%option` shorthand, as in `[%autowidth%header]`.
            for(usize j = 0; j < entry_len; ++j) {
                if(entry[j] != '%') {
                    continue;
                }
                usize word_start = j + 1;
                usize k = word_start;
                while(k < entry_len && entry[k] != '%' && entry[k] != '#' && entry[k] != '.') {
                    ++k;
                }
                *option |= attr_header_option(entry + word_start, k - word_start);
                j = k - 1;
            }
        }
    }
}

/// Skips the rest of the current line and its line ending. False at the end of input.
static inline bool skip_rest_of_line(TSLexer *lexer) {
    while(!is_eof(lexer) && !is_newline(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }
    return skip_line_ending(lexer);
}

/// Whether the lines ahead lead straight to a `|===` fence, past only what may sit between
/// a block's attribute lists and the block: blank lines, more attribute lists or anchors,
/// block titles and line comments.
static inline bool leads_to_table_fence(TSLexer *lexer) {
    for(;;) {
        if(is_eof(lexer)) {
            return false;
        }
        while(is_white_space(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        i32 ch = lexer->lookahead;
        if(is_newline(ch)) {
            skip_line_ending(lexer);
            continue;
        }
        if(lexer->get_column(lexer) != 0) {
            return false;
        }
        lexer->advance(lexer, false);
        switch(ch) {
            case '[':
                break;
            case '.':
                if(is_white_space(lexer->lookahead) || is_newline(lexer->lookahead) ||
                   lexer->lookahead == '.' || is_eof(lexer)) {
                    return false;
                }
                break;
            case '/':
                if(lexer->lookahead != '/') {
                    return false;
                }
                lexer->advance(lexer, false);
                // `////` opens a block comment, which is not skipped.
                if(lexer->lookahead == '/') {
                    return false;
                }
                break;
            case '|': {
                usize run = 0;
                while(lexer->lookahead == '=' && run < USIZE_MAX) {
                    ++run;
                    lexer->advance(lexer, false);
                }
                while(is_white_space(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                return run >= 3 && (is_newline(lexer->lookahead) || is_eof(lexer));
            }
            default:
                return false;
        }
        if(!skip_rest_of_line(lexer)) {
            return false;
        }
    }
}

/// Called after the `[` of a block attribute list has been marked, so everything read here
/// is lookahead. When the list leads to a table fence, its header options and `cols` are
/// recorded for that fence to read; options on any other block are never recorded.
static inline void scan_attr_list_for_table(Scanner *s, TSLexer *lexer) {
    char text[ATTR_LIST_MAX];
    usize len = 0;
    while(!is_eof(lexer) && !is_newline(lexer->lookahead)) {
        if(len == ATTR_LIST_MAX) {
            return;
        }
        i32 ch = lexer->lookahead;
        text[len++] = ch >= 0 && ch < 0x80 ? (char)ch : '?';
        lexer->advance(lexer, false);
    }
    while(len > 0 && is_white_space(text[len - 1])) {
        --len;
    }
    // Only a list that closes at the end of its line is an attribute line.
    if(len == 0 || text[len - 1] != ']') {
        return;
    }
    --len;

    u8 option = 0;
    usize cols = 0;
    attr_list_options(text, len, &option, &cols);
    if(option == 0 && cols == 0) {
        return;
    }
    if(!skip_line_ending(lexer) || !leads_to_table_fence(lexer)) {
        return;
    }
    s->table_header_option |= option;
    if(cols != 0) {
        s->table_header_cols = cols;
    }
}

static inline bool is_eof(TSLexer *lexer) {
    return lexer->eof(lexer);
}

static inline bool is_new_line(i32 ch) {
    return ch == '\r' || ch == '\n';
}

static inline bool consume(i32 ch, TSLexer *lexer, bool skip_space, usize *counter, usize max) {
    bool has_space = false;
    if(skip_space) {
        has_space |= skip_white_space(lexer);
    }

    while(lexer->lookahead == ch) {
        lexer->advance(lexer, false);
        if(skip_space) {
            has_space |= skip_white_space(lexer);
        }
        --max;
        if(counter != NULL) {
            *counter += 1;
        }
        if(max == 0) {
            break;
        }
    }

    lexer->mark_end(lexer);
    return has_space;
}

static inline bool parse_sequence_impl(TSLexer *lexer, char const *sequence, usize len) {
    usize pos = 0;

    while(pos < len) {
        if(lexer->lookahead != sequence[pos]) {
            return false;
        }

        lexer->advance(lexer, false);
        ++pos;
    }

    return true;
}

static inline bool parse_number(TSLexer *lexer) {
    bool has_number = false;
    while(is_ascii_digit(lexer->lookahead)) {
        lexer->advance(lexer, false);
        has_number = true;
    }
    return has_number;
}

char table_cell_attr_align_left = '<';
char table_cell_attr_align_right = '>';
char table_cell_attr_align_middle = '^';
char table_cell_attr_style_none = 'd';
char table_cell_attr_style_strong = 's';
char table_cell_attr_style_emphasis = 'e';
char table_cell_attr_style_monospaced = 'm';
char table_cell_attr_style_header = 'h';
char table_cell_attr_style_literal = 'l';
char table_cell_attr_style_asciidoc = 'a';

static inline bool parse_table_attr(TSLexer *lexer) {
    if(lexer->lookahead == table_cell_attr_align_left ||
       lexer->lookahead == table_cell_attr_align_right ||
       lexer->lookahead == table_cell_attr_align_middle ||
       lexer->lookahead == table_cell_attr_style_none ||
       lexer->lookahead == table_cell_attr_style_strong ||
       lexer->lookahead == table_cell_attr_style_emphasis ||
       lexer->lookahead == table_cell_attr_style_monospaced ||
       lexer->lookahead == table_cell_attr_style_header ||
       lexer->lookahead == table_cell_attr_style_literal ||
       lexer->lookahead == table_cell_attr_style_asciidoc) {
        lexer->advance(lexer, false);
        return true;
    }
    // table_cell_span_vertical = /\.\d+\+/;
    if(lexer->lookahead == '.') {
        lexer->advance(lexer, false);
        return parse_number(lexer);
    }
    // table_cell_span_width = /\d+\+/;
    // table_cell_span_vw = /\d+\.\d+\+/;
    if(parse_number(lexer)) {
        if(lexer->lookahead == '.') {
            lexer->advance(lexer, false);
            if(!parse_number(lexer)) {
                return false;
            }
        }
        if(lexer->lookahead == '+') {
            lexer->advance(lexer, false);
            return true;
        }
    }

    return false;
}

static inline bool scanner_is_matching(Scanner const *self, BlockKind kind, usize counter) {
    Node *top = scanner_top(self);
    if(!top) {
        return false;
    }

    if(counter == 0) {
        return top->kind == kind;
    }
    return top->kind == kind && top->counter == counter;
}
static inline Result scanner_serialize(Scanner const *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    ret &= quick_buffer_write_usize(qb, self->len);
    ret &= quick_buffer_extend_bytes(qb, self->buffer, sizeof(Node) * self->len);
    ret &= quick_buffer_write_usize(qb, self->header_pending ? 1 : 0);
    ret &= quick_buffer_write_usize(qb, self->header_line_end_pending ? 1 : 0);
    ret &= quick_buffer_write_usize(qb, self->table_header_option);
    ret &= quick_buffer_write_usize(qb, self->table_header_cols);

    return ret;
}

static inline Result scanner_deserialize(Scanner *self, QuickBuffer *qb) {
    Result ret = RESULT_OK;

    usize len = 0;
    ret &= quick_buffer_read_usize(qb, &len);
    if(self->capacity < len) {
        self->buffer = ts_realloc(self->buffer, len * sizeof(Node));
        self->capacity = len;
    }
    self->len = len;
    ret &= quick_buffer_read_bytes(qb, self->buffer, len * sizeof(Node));

    usize header_pending = 0;
    ret &= quick_buffer_read_usize(qb, &header_pending);
    self->header_pending = header_pending != 0;

    usize header_line_end_pending = 0;
    ret &= quick_buffer_read_usize(qb, &header_line_end_pending);
    self->header_line_end_pending = header_line_end_pending != 0;

    usize table_header_option = 0;
    ret &= quick_buffer_read_usize(qb, &table_header_option);
    self->table_header_option = (u8)table_header_option;
    ret &= quick_buffer_read_usize(qb, &self->table_header_cols);

    return ret;
}

static inline bool scanner_pop_kind(Scanner *self, BlockKind kind, usize counter) {
    if(scanner_is_matching(self, kind, counter)) {
        scanner_pop(self);
        return true;
    }

    return false;
}

static inline void scanner_pop(Scanner *self) {
    if(self->len == 0) {
        return;
    }
    self->len -= 1;
}

static inline void scanner_push(Scanner *self, BlockKind kind, usize counter) {
    const usize BUFFER_SIZE = TREE_SITTER_SERIALIZATION_BUFFER_SIZE - sizeof(usize);

    if(self->len == self->capacity) {
        usize grow_counter = BUFFER_SIZE / sizeof(Node) - self->capacity;
        if(grow_counter == 0) {
            return;
        }
        if(LIST_GROW_SIZE < grow_counter) {
            grow_counter = LIST_GROW_SIZE;
        }

        self->capacity = self->capacity + grow_counter;
        self->buffer = ts_realloc(self->buffer, sizeof(Node) * self->capacity);
    }

    Node *top = &(self->buffer[self->len++]);
    top->kind = kind;
    top->counter = counter;
}

static inline void scanner_init(Scanner *self) {
    bzero(self, sizeof(Scanner));
    self->buffer = ts_malloc(LIST_INIT_SIZE * sizeof(Node));
    self->capacity = LIST_INIT_SIZE;
}

static inline void scanner_free(Scanner *self) {
    ts_free(self->buffer);
    bzero(self, sizeof(Scanner));
}

static inline bool scanner_is_matching_raw_block(Scanner *self) {
    return scanner_is_matching(self, BLOCK_KIND_LISTING, 0) ||
           scanner_is_matching(self, BLOCK_KIND_FENCED, 0) ||
           scanner_is_matching(self, BLOCK_KIND_LITERAL, 0);
}

static inline Node *scanner_top(Scanner const *self) {
    if(self->len == 0) {
        return NULL;
    }

    return &(self->buffer[self->len - 1]);
}

// The longest line `continues_paragraph` reads. A longer line is not judged and so
// ends the paragraph, as every line did before paragraphs spanned lines.
#define PARAGRAPH_PEEK 512

/// Whether `ch` can begin a word: an ASCII letter or digit, or any non-ASCII
/// character, which covers letters in other scripts.
static inline bool is_word_start(i32 ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || is_ascii_digit(ch) ||
           ch > 0x7f;
}

/// Reads the line the lexer is on, which the caller has already marked as lookahead,
/// and says whether it is prose that continues a paragraph.
static inline bool continues_paragraph(TSLexer *lexer) {
    i32 line[PARAGRAPH_PEEK];
    usize len = 0;
    while(!lexer->eof(lexer) && !is_newline(lexer->lookahead)) {
        if(len == PARAGRAPH_PEEK) {
            return false;
        }
        line[len++] = lexer->lookahead;
        lexer->advance(lexer, false);
    }
    return is_prose_line(line, len);
}

/// Conservative: a line is prose only when nothing about it could open a block.
static inline bool is_prose_line(i32 const *line, usize len) {
    // Blank, indented, or opening with a marker character (`*`, `.`, `[`, `=`, `|`, …).
    if(len == 0 || !is_word_start(line[0])) {
        return false;
    }

    // An ordered list marker (`1.`, `a.`, `α.`) followed by white space. This also
    // stops at prose such as `Mr. Smith`, which then starts a paragraph of its own.
    usize word = 0;
    while(word < len && is_word_start(line[word])) {
        word++;
    }
    if(word < len && line[word] == '.' && (word + 1 == len || is_white_space(line[word + 1]))) {
        return false;
    }

    // An admonition label: `NOTE:` and the like.
    static char const *const labels[] = {"NOTE", "TIP", "IMPORTANT", "CAUTION", "WARNING"};
    for(usize l = 0; l < sizeof(labels) / sizeof(labels[0]); l++) {
        usize n = strlen(labels[l]);
        if(len > n && line[n] == ':') {
            usize i = 0;
            while(i < n && line[i] == (i32)labels[l][i]) {
                i++;
            }
            if(i == n) {
                return false;
            }
        }
    }

    // Description list terms (`term::`, `term;;`), block macros (`image::`) and
    // preprocessor directives (`ifdef::`) all carry a doubled `:` or `;`.
    for(usize i = 1; i < len; i++) {
        if((line[i] == ':' && line[i - 1] == ':') || (line[i] == ';' && line[i - 1] == ';')) {
            return false;
        }
    }

    return true;
}
