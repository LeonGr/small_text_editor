#include "editor.h"
#include <tree_sitter/api.h>

#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
    HL_FUNCTION,
    HL_SYNTAX1,
    HL_SYNTAX2,
    HL_CONSTANT,
    HL_FIELD,
};

/*
 * Calculate syntax highlighting for the given `row`
 */
void editorUpdateSyntax(erow *row);

/*
 * Convert `editorHighlight` constant `hl` to ANSI escape code number
 * See: https://ss64.com/nt/syntax-ansi.html
 */
int editorSyntaxToColor(int hl);

/*
 * Check if the current filetype is supported and highlight it
 */
void editorSelectSyntaxHighlight();

void editorInitSyntaxTree();

void editorUpdateSyntaxHighlight(int old_end_row, int old_end_column, int old_end_byte, int new_end_row, int new_end_column, int new_end_byte);

void editorHighlightSyntaxTree();

#endif
