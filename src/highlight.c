#include "highlight.h"
#include "io.h"
#include "languages.h"
#include <ctype.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

extern struct editorConfig E;

/*** syntax highlighting ***/

/*
 * Returns `true` is character `c` is considered a separator of words
 */
bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:\"", c) != NULL;
}

bool inStringArray(const char *string, char **array) {
    int i = 0;
    char *current = array[i];
    while (array[i] != NULL) {
        if (!strcmp(string, current)) {
            return true;
        }

        current = array[++i];
    }

    return false;
}

void editorHighlightSubtree(TSNode root) {
    if (ts_node_is_null(root)) {
        return;
    }

    const char *type = ts_node_type(root);
    printf("node type: %s\r\n", type);

    uint32_t child_count = ts_node_child_count(root);

    printf("%d child(ren)\r\n", child_count);

    TSPoint start = ts_node_start_point(root);
    TSPoint end = ts_node_end_point(root);

    int highlight = HL_NORMAL;

    // Number
    if (inStringArray(type, (char*[]) { "number_literal", "integer", "float", NULL })) {
        highlight = HL_NUMBER;
    }
    // storage, type qualifier
    else if (inStringArray(type, (char*[]) { "storage_class_specifier", "type_qualifier", NULL })) {
        highlight = HL_KEYWORD2;
    }
    // compiler info
    else if (inStringArray(type, E.syntax->keyword2)) {
        highlight = HL_KEYWORD2;
    }
    // primitive types
    else if (inStringArray(type, (char*[]) { "primitive_type", "type_identifier", "sized_type_specifier", NULL })) {
        highlight = HL_KEYWORD2;
    }
    // return
    else if (!strcmp(type, "return_statement")) {
        highlight = HL_KEYWORD2;
        TSNode return_child = ts_node_child(root, 0);

        if (!ts_node_is_null(return_child)) {
            start = ts_node_start_point(return_child);
            end = ts_node_end_point(return_child);
        }
    }
    // except (Python)
    else if (!strcmp(type, "except_clause")) {
        highlight = HL_KEYWORD2;
        TSNode return_child = ts_node_child(root, 1);

        if (!ts_node_is_null(return_child)) {
            start = ts_node_start_point(return_child);
            end = ts_node_end_point(return_child);
        }
    }
    // keyword
    else if (inStringArray(type, E.syntax->keyword1)) {
        highlight = HL_KEYWORD1;
    }
    // function name
    else if (inStringArray(type, (char*[]) { "function_declarator", "call_expression", "call", NULL })) {
        highlight = HL_FUNCTION;
        TSNode function_child = ts_node_child(root, 0);

        if (!strcmp(type, "call")) {
            if (!ts_node_is_null(function_child)) {
                char *field_name = "attribute";
                TSNode attribute_child = ts_node_child_by_field_name(function_child, field_name, strlen(field_name));
                if (!ts_node_is_null(attribute_child)) {
                    start = ts_node_start_point(attribute_child);
                    end = ts_node_end_point(attribute_child);
                } else {
                    start = ts_node_start_point(function_child);
                    end = ts_node_end_point(function_child);
                }
            }
        } else {
            if (!ts_node_is_null(function_child)) {
                start = ts_node_start_point(function_child);
                end = ts_node_end_point(function_child);
            }
        }

    }
    // string
    else if (inStringArray(type, (char*[]) { "string_literal", "system_lib_string", "char_literal", "string", NULL })) {
        highlight = HL_STRING;
    }
    else if (!strcmp(type, "escape_sequence")) {
        highlight = HL_SYNTAX;
    }
    // comment
    else if (!strcmp(type, "comment")) {
        highlight = HL_COMMENT;
    }
    // syntax separators
    else if (inStringArray(type, E.syntax->syntax1)) {
        highlight = HL_SYNTAX;
    }
    else if (inStringArray(type, E.syntax->syntax2)) {
        highlight = HL_KEYWORD1;
    }
    // unary expression
    else if (inStringArray(type, (char*[]) { "unary_expression", "pointer_expression", "pointer_declarator", "abstract_pointer_declarator", NULL })) {
        highlight = HL_KEYWORD1;
        end.column = start.column + 1;
    }
    // fields
    else if (!strcmp(type, "field_identifier")) {
        highlight = HL_FIELD;
    }
    // the python parser does not explicitely mark fields, but uses arguments
    else if (!strcmp(type, "attribute")) {

        // functions can also be arguments, ignore if attribute is followed by argument list
        TSNode sibling = ts_node_next_sibling(root);
        const char *sibling_type = "";

        if (!ts_node_is_null(sibling)) {
             sibling_type = ts_node_type(sibling);
        }

        // If there is no sibling or the sibling is not an argument list, find and highlight the field
        if (strcmp(sibling_type, "argument_list")) {
            char *field_name = "attribute";
            TSNode attribute_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

            if (!ts_node_is_null(attribute_child)) {
                highlight = HL_FIELD;
                start = ts_node_start_point(attribute_child);
                end = ts_node_end_point(attribute_child);
            }
        }
    }
    // constants
    else if (!strcmp(type, "identifier")) {
        int len = end.column - start.column;
        if (len > 1) {
            erow *row = &E.row[start.row];

            bool is_constant = true;
            for (uint32_t c = start.column; c < end.column; c++) {
                // Assume constant consists of capital letters, numbers or '_'
                if ((row->chars[c] < 48 || row->chars[c] > 58) && //
                    (row->chars[c] < 65 || row->chars[c] > 90) &&
                     row->chars[c] != '_') {
                    is_constant = false;
                    break;
                }
            }

            // Python __XXX__ constants
            regex_t regex;
            int compiled_regex = regcomp(&regex, "__[[:alpha:]]+__", REG_EXTENDED);
            if (!compiled_regex) {
                is_constant = is_constant || !regexec(&regex, row->chars, 0, NULL, 0);
            }

            if (is_constant) {
                highlight = HL_CONSTANT;
            }
        }
    }
    else if (!strcmp(type, "null")) {
        highlight = HL_CONSTANT;
    }

    if (highlight != HL_NORMAL) {
        printf("start [%d, %d]\r\n", start.row, start.column);
        printf("end [%d, %d]\r\n", end.row, end.column);

        erow *row = &E.row[start.row];

        // node spans multiple lines
        if (end.row > start.row) {
            memset(&row->highlight[start.column], highlight, row->renderSize - start.column);

            for (uint32_t r = start.row + 1; r < end.row; r++) {
                row = &E.row[r];
                memset(row->highlight, highlight, row->renderSize);
            }

            row = &E.row[end.row];
            // printf("rendersize: %d", row->renderSize);
            // printf("end.column: %d", end.column);
            memset(row->highlight, highlight, end.column);
        }
        // node spans single line
        else {
            memset(&row->highlight[start.column], highlight, end.column - start.column);
        }
    }

    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(root, i);
        editorHighlightSubtree(child);
    }
}

void editorUpdateHighlight() {
    // Reset highlight
    for (int i = 0; i < E.numrows; i++) {
        erow *row = &E.row[i];

        // Set correct highlighting array size
        row->highlight = realloc(row->highlight, row->renderSize);
        // Fill array with default highlight
        memset(row->highlight, HL_NORMAL, row->renderSize);
    }

    TSNode root = ts_tree_root_node(E.syntax->tree);

    printf("UPDATE:\r\n");

    editorHighlightSubtree(root);
}

/*
 * Calculate syntax highlighting for the given `row`
 */
// void editorUpdateSyntax(erow *row) {
    // // Set correct highlighting array size
    // row->highlight = realloc(row->highlight, row->renderSize);
    // // Fill array with default highlight
    // memset(row->highlight, HL_NORMAL, row->renderSize);

    // return;
    // if (E.syntax == NULL) return;

    // char **keywords = E.syntax->keywords;

    // char *scs = E.syntax->single_line_comment_start;
    // char *mcs = E.syntax->multi_line_comment_start;
    // char *mce = E.syntax->multi_line_comment_end;

    // int scs_len = scs ? strlen(scs) : 0;
    // int mcs_len = mcs ? strlen(mcs) : 0;
    // int mce_len = mce ? strlen(mce) : 0;

    // bool previous_is_separator = true;
    // char in_string_delimiter = '\0';
    // bool in_comment = row->index > 0 && E.row[row->index - 1].open_comment;

    // int i = 0;
    // while (i < row->renderSize) {
        // char c = row->render[i];
        // unsigned char previous_highlight = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

        // // Single line comment
        // if (scs_len && !in_string_delimiter && !in_comment) {
            // // Check if the current character position contains the comment start string
            // if (!strncmp(&row->render[i], scs, scs_len)) {
                // // Set the highlight of the rest of the line to comment
                // memset(&row->highlight[i], HL_COMMENT, row->renderSize - i);
                // // Don't apply highlighting to the rest of the row
                // break;
            // }
        // }

        // // Multi line comment
        // if (mcs_len && mcs_len && !in_string_delimiter) {
            // if (in_comment) {
                // row->highlight[i] = HL_MLCOMMENT;

                // if (!strncmp(&row->render[i], mce, mce_len)) {
                    // memset(&row->highlight[i], HL_COMMENT, mce_len);
                    // i += mce_len;
                    // in_comment = false;
                    // previous_is_separator = true;
                    // continue;
                // } else {
                    // i++;
                    // continue;
                // }
            // } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                // memset(&row->highlight[i], HL_COMMENT, mcs_len);
                // i += mcs_len;
                // in_comment = true;
                // continue;
            // }
        // }

        // if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            // // Check if we're in a string
            // if (in_string_delimiter) {
                // row->highlight[i] = HL_STRING;

                // // An escape character started with '\' means that we should not stop the string highlighting
                // // (we make sure there is an extra character after the backslash)
                // if (c == '\\' && i + 1 < row->renderSize) {
                    // row->highlight[i + 1] = HL_STRING;
                    // i += 2;
                    // continue;
                // }

                // // Reset in_string_delimiter if we reached the end delimeter
                // if (c == in_string_delimiter) {
                    // in_string_delimiter = '\0';
                // }

                // i++;
                // previous_is_separator = true;
                // continue;
            // } else {
                // // Check if this is the start of a string
                // if (c == '"' || c == '\'') {
                    // in_string_delimiter = c;
                    // row->highlight[i] = HL_STRING;
                    // i++;
                    // continue;
                // }
            // }
        // }

        // if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            // if ((isdigit(c) && (previous_is_separator || previous_highlight == HL_NUMBER)) ||
                    // (c == '.' && previous_highlight == HL_NUMBER)) {
                // row->highlight[i] = HL_NUMBER;
                // i++;
                // previous_is_separator = false;
                // continue;
            // }
        // }

        // if (previous_is_separator) {
            // int j;
            // for (j = 0; keywords[j]; j++) {
                // int keyword_len = strlen(keywords[j]);
                // int is_type2 = keywords[j][keyword_len - 1] == '|';

                // if (is_type2) {
                    // keyword_len--;
                // }

                // // Check if the current character is the start of a keyword...
                // if (!strncmp(&row->render[i], keywords[j], keyword_len) &&
                        // // ...and is followed by a separator (so it's not the start of a different word)
                        // isSeparator(row->render[i + keyword_len])) {
                    // // Set the highlight for the keyword
                    // memset(&row->highlight[i], is_type2 ? HL_KEYWORD2 : HL_KEYWORD1, keyword_len);
                    // i += keyword_len;
                    // break;
                // }
            // }

            // if (keywords[j] != NULL) {
                // previous_is_separator = false;
                // continue;
            // }
        // }

        // previous_is_separator = isSeparator(c);
        // i++;
    // }

    // bool changed = row->open_comment != in_comment;
    // row->open_comment = in_comment;
    // // If the comment status changed, update syntax color
    // if (changed && row->index + 1 < E.numrows) {
        // editorUpdateSyntax(&E.row[row->index + 1]);
    // }
// }

/*
 * Convert `editorHighlight` constant `hl` to ANSI escape code number
 * See: https://ss64.com/nt/syntax-ansi.html
 */
int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36; // Cyan
        case HL_KEYWORD1:
            return 33; // Yellow
        case HL_KEYWORD2:
            return 32; // Green
        case HL_STRING:
            return 35; // Magenta
        case HL_NUMBER:
            return 31; // Red
        case HL_FUNCTION:
            return 34; // Blue
        case HL_SYNTAX:
            return 32; // Yellow
        case HL_CONSTANT:
            return 31; // Red
        case HL_FIELD:
            return 36; // Cyan
        default:
            return 37; // White
    }
}

/*
 * Check if the current filetype is supported and highlight it
 */
void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    // strrchr returns a pointerr to the last occurrence of the '.' in the filename, NULL if none
    char *extension = strrchr(E.filename, '.');

    unsigned entries = num_hldb_entries();

    // Loop through supported highlights
    for (unsigned int i = 0; i < entries; i++) {
        struct editorSyntax *s = &HLDB[i];

        unsigned int j = 0;
        // Loop through filematch keywords
        while (s->filematch[j]) {
            // Check if filematch keyword is an extension
            int is_extension = (s->filematch[j][0] == '.');

            // Check if file extension matches or the filename matches
            if ((is_extension && extension && !strcmp(extension, s->filematch[j])) ||
                    (!is_extension && strstr(E.filename, s->filematch[j]))) {
                E.syntax = s;

                // Rehighlight file
                // for (int filerow = 0; filerow < E.numrows; filerow++) {
                    // editorUpdateSyntax(&E.row[filerow]);
                // }

                return;
            }

            j++;
        }
    }
}

void editorPrintSyntaxTree() {
    TSNode root = ts_tree_root_node(E.syntax->tree);

    char *string = ts_node_string(root);
    printf("Syntax tree:\r\n%s\r\n", string);

    free(string);
}

void editorPrintSourceCode() {
    for (int i = 0; i < E.numrows; i++) {
        erow *row = &E.row[i];
        printf("%s\r\n", row->chars);
    }
}

void editorInitSyntaxTree() {
    TSParser *parser = ts_parser_new();

    printf("Filetype: %s\r\n", E.syntax->filetype);

    if (!strcmp(E.syntax->filetype, "c")) {
        TSLanguage *tree_sitter_c();
        E.syntax->language = tree_sitter_c();
        ts_parser_set_language(parser, E.syntax->language);
    } else if (!strcmp(E.syntax->filetype, "Python")) {
        TSLanguage *tree_sitter_python();
        E.syntax->language = tree_sitter_python();
        ts_parser_set_language(parser, E.syntax->language);
    }
    else {
        return;
    }

    int len;
    char *source_code = editorRowsToString(&len);

    editorPrintSourceCode();

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, len);

    E.syntax->tree = tree;
    E.syntax->parser = parser;

    editorPrintSyntaxTree();
}

void editorUpdateSyntaxTree(int start_row, int start_column, int old_end_row, int old_end_column, int new_end_row, int new_end_column) {
    TSInputEdit edit;
    edit.start_point = (TSPoint){ start_row, start_column };
    edit.old_end_point = (TSPoint){ old_end_row, old_end_column };
    edit.new_end_point = (TSPoint){ new_end_row, new_end_column };

    ts_tree_edit(E.syntax->tree, &edit);

    int len;
    char *source_code = editorRowsToString(&len);

    editorPrintSourceCode();

    TSTree *tree = ts_parser_parse_string(E.syntax->parser, E.syntax->tree, source_code, len);
    E.syntax->tree = tree;

    editorPrintSyntaxTree();

    editorUpdateHighlight();
}
