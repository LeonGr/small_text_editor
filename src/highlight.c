#include "highlight.h"
#include "io.h"
#include "languages.h"
#include "render.h"
#include <ctype.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
// #include <tree_sitter/api.h>

extern struct editorConfig E;

/*** syntax highlighting ***/

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
    // printf("node type: \t'%s'\r\n", type);

    // uint32_t start_byte = ts_node_start_byte(root);
    // uint32_t end_byte = ts_node_end_byte(root);
    // printf("start byte: %d, end_byte: %d\r\n",  start_byte, end_byte);

    uint32_t child_count = ts_node_child_count(root);

    // printf("%d child(ren)\r\n", child_count);

    TSPoint start = ts_node_start_point(root);
    TSPoint end = ts_node_end_point(root);

    int highlight = HL_NORMAL;

    /*
     * Direct language agnostic highlighting
     */
    // Number
    if (inStringArray(type, (char*[]) { "number_literal", "integer", "float", "integer_literal", NULL })) {
        highlight = HL_NUMBER;
    }
    // storage, type qualifier
    else if (inStringArray(type, (char*[]) { "storage_class_specifier", "type_qualifier", NULL })) {
        highlight = HL_KEYWORD2;
    }
    // keywords type 1
    else if (inStringArray(type, E.syntax->keyword1)) {
        highlight = HL_KEYWORD1;
    }
    // keywords type 2
    else if (inStringArray(type, E.syntax->keyword2)) {
        highlight = HL_KEYWORD2;
    }
    // primitive types
    else if (inStringArray(type, (char*[]) { "primitive_type", "type_identifier", "sized_type_specifier", NULL })) {
        highlight = HL_KEYWORD2;
    }
    // string
    else if (inStringArray(type, (char*[]) { "string_literal", "system_lib_string", "char_literal", "string", "raw_string_literal", NULL })) {
        highlight = HL_STRING;
    }
    else if (!strcmp(type, "escape_sequence")) {
        highlight = HL_SYNTAX;
    }
    // comment
    else if (inStringArray(type, (char*[]) { "comment", "line_comment", NULL })) {
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
    else if (!strcmp(type, "null")) {
        highlight = HL_CONSTANT;
    }

    /*
     * Node child language agnostic highlighting
     */
    // return
    else if (!strcmp(type, "return_statement")) {
        TSNode return_child = ts_node_child(root, 0);

        if (!ts_node_is_null(return_child)) {
            highlight = HL_KEYWORD2;
            start = ts_node_start_point(return_child);
            end = ts_node_end_point(return_child);
        }
    }

    /*
     * Complex language agnostic highlighting
     */

    // Special identifiers
    else if (!strcmp(type, "identifier")) {
        int len = end.column - start.column;

        // Constants
        if (len > 1) {
            erow *row = &E.row[start.row];

            bool is_constant = true;
            for (uint32_t c = start.column; c < end.column; c++) {
                // Assume constant consists of capital letters, numbers or '_'
                if ((row->chars[c] < '0' || row->chars[c] > '9') &&
                    (row->chars[c] < 'A' || row->chars[c] > 'Z') &&
                     row->chars[c] != '_') {
                    is_constant = false;
                    break;
                }
            }

            if (is_constant) {
                highlight = HL_CONSTANT;
            }
        }

        // Identifiers part of the specified keywords

        // Copy identifier text
        char *word;
        word = malloc(len + 1);
        erow *row = &E.row[start.row];
        memcpy(word, &row->chars[start.column], len);
        word[len] = '\0';

        // printf("WORD:%s\r\n", word);

        if (inStringArray(word, E.syntax->keyword2)) {
            highlight = HL_KEYWORD2;
        } else if (inStringArray(word, E.syntax->keyword1)) {
            highlight = HL_KEYWORD1;
        }

        free(word);
    }

    // function name
    else if (inStringArray(type, (char*[]) { "function_declarator", "call_expression", "call", "function_item", NULL })) {
        TSNode function_name;
        bool set = false;

        if (!strcmp(type, "function_declarator")) {
            char *field_name = "declarator";
            function_name = ts_node_child_by_field_name(root, field_name, strlen(field_name));
            set = true;
        } else if (!strcmp(type, "call_expression")) {
            char *field_name = "function";
            TSNode function_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

            // No children means the identifier with field 'function' is the function name
            if (!ts_node_is_null(function_child) && ts_node_child_count(function_child) == 0) {
                function_name = function_child;
            }
            // Otherwise the function is part of a path and we should just select the 'name' field.
            else {
                field_name = "name";
                function_name = ts_node_child_by_field_name(function_child, field_name, strlen(field_name));
            }

            set = true;
        } else if (!strcmp(type, "call")) {
            char *field_name = "function";
            TSNode function_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

            if (!ts_node_is_null(function_child)) {
                int function_child_count = ts_node_child_count(function_child);

                // No children means the identifier with field 'function' is the function name
                if (function_child_count == 0) {
                    function_name = function_child;
                }
                // Otherwise the function is part of an object and we should just select the 'attribute' field.
                else {
                    field_name = "attribute";
                    function_name = ts_node_child_by_field_name(function_child, field_name, strlen(field_name));
                }

                set = true;
            }
        } else if (!strcmp(type, "function_item")) {
            char *field_name = "name";
            function_name = ts_node_child_by_field_name(root, field_name, strlen(field_name));
            set = true;
        }

        if (set && !ts_node_is_null(function_name)) {
            highlight = HL_FUNCTION;
            start = ts_node_start_point(function_name);
            end = ts_node_end_point(function_name);
        }
    }

    /*
     * Language specific highlighting
     */

    // Rust
    if (!strcmp(E.syntax->filetype, "Rust")) {
        /*
         * Direct
         */

        // meta item (Rust)
        if (!strcmp(type, "meta_item")) {
            highlight = HL_KEYWORD2;
        }

        // mutable specifier keyword (Rust)
        else if (!strcmp(type, "mutable_specifier")) {
            highlight = HL_KEYWORD1;
        }

        // fields (Rust)
        else if (!strcmp(type, "field_identifier")) {
            highlight = HL_FUNCTION;
        }

        // use list (Rust)
        else if (!strcmp(type, "use_list")) {
            highlight = HL_KEYWORD2;
        }

        // use wildcard * (Rust)
        else if (!strcmp(type, "use_wildcard")) {
            highlight = HL_CONSTANT;
        }

        // enum item
        else if (!strcmp(type, "enum_variant")) {
            highlight = HL_CONSTANT;
        }

        /*
         * Child
         */

        // parameter (Rust)
        else if (!strcmp(type, "parameter")) {
            TSNode parameter_child = ts_node_child(root, 0);

            if (!ts_node_is_null(parameter_child)) {
                highlight = HL_KEYWORD1;
                start = ts_node_start_point(parameter_child);
                end = ts_node_end_point(parameter_child);
            }
        }

        // macro name (Rust)
        else if (!strcmp(type, "macro_invocation")) {
            highlight = HL_KEYWORD2;
            TSNode macro_child = ts_node_child(root, 0);

            if (!ts_node_is_null(macro_child)) {
                start = ts_node_start_point(macro_child);
                end = ts_node_end_point(macro_child);
                end.column += 1;
            }
        }

        // field expression (Rust)
        else if (!strcmp(type, "field_expression")) {
            char *field_name = "field";
            TSNode field_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

            if (!ts_node_is_null(field_child)) {
                highlight = HL_FUNCTION;
                start = ts_node_start_point(field_child);
                end = ts_node_end_point(field_child);
            }
        }

        /*
         * Complex
         */

        // scoped identifier (Rust)
        else if (inStringArray(type, (char*[]) { "scoped_identifier", "scoped_type_identifier", NULL })) {
            TSNode parent = ts_node_parent(root);

            if (!ts_node_is_null(parent)) {
                const char *parent_type = ts_node_type(parent);

                // If current node is a nested scoped identifier, highlight whole node
                if (inStringArray(parent_type, (char*[]) { "scoped_identifier", "scoped_use_list", NULL })) {
                    highlight = HL_COMMENT;
                }
                // If parent is a use wildcard, determine highlight based on case
                else if (!strcmp(parent_type, "use_wildcard")) {
                    char *field_name = "name";
                    TSNode name_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

                    TSPoint name_start = ts_node_start_point(name_child);
                    erow *row = &E.row[name_start.row];
                    char first = row->chars[name_start.column];

                    if (first >= 'A' && first <= 'Z') {
                        // Uppercase
                        highlight = HL_KEYWORD2;
                    } else {
                        highlight = HL_COMMENT;
                    }
                }
                // Otherwise, highlight both the path and name (if not a function)
                else {
                    char *field_name = "path";
                    TSNode path_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));
                    TSPoint path_start = ts_node_start_point(path_child);
                    TSPoint path_end = ts_node_end_point(path_child);
                    erow *path_row = &E.row[path_start.row];

                    for (uint32_t c = path_start.column; c < path_end.column; c++) {
                        path_row->highlight[c] = HL_COMMENT;
                    }

                    if (strcmp(parent_type, "call_expression")) {
                        field_name = "name";
                        TSNode name_child = ts_node_child_by_field_name(root, field_name, strlen(field_name));

                        TSPoint name_start = ts_node_start_point(name_child);
                        TSPoint name_end = ts_node_end_point(name_child);
                        erow *name_row = &E.row[name_start.row];

                        for (uint32_t c = name_start.column; c < name_end.column; c++) {
                            name_row->highlight[c] = HL_KEYWORD2;
                        }
                    }
                }
            }
        }

        // match pattern types
        else if (!strcmp(type, "match_pattern")) {
            TSNode match_child = ts_node_child(root, 0);

            if (!ts_node_is_null(match_child)) {
                const char *child_type = ts_node_type(match_child);

                if (strcmp(child_type, "identifier")) {
                    if (!strcmp(child_type, "tuple_struct_pattern")) {
                        char *field_name = "type";
                        TSNode type_child = ts_node_child_by_field_name(match_child, field_name, strlen(field_name));

                        start = ts_node_start_point(type_child);
                        end = ts_node_end_point(type_child);
                        highlight = HL_KEYWORD2;
                    }
                } else {
                    highlight = HL_KEYWORD2;
                }
            }
        }
    }

    // Python
    if (!strcmp(E.syntax->filetype, "Python")) {
        // except (Python)
        if (!strcmp(type, "except_clause")) {
            highlight = HL_KEYWORD2;
            TSNode except_child = ts_node_child(root, 1);

            if (!ts_node_is_null(except_child)) {
                start = ts_node_start_point(except_child);
                end = ts_node_end_point(except_child);
            }
        }

        /*
         * Complex
         */

        // Fields (Python) - atttributes without parameter list
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

        else if (!strcmp(type, "identifier")) {
            // Python __XXX__ constants

            regex_t regex;
            int compiled_regex = regcomp(&regex, "__[[:alpha:]]+__", REG_EXTENDED);

            // If successfully compiled
            if (!compiled_regex) {
                erow *row = &E.row[start.row];

                if (!regexec(&regex, row->chars, 0, NULL, 0)) {
                    highlight = HL_CONSTANT;
                }
            }
        }
    }

    // C
    if (!strcmp(E.syntax->filetype, "c")) {
        // fields
        if (!strcmp(type, "field_identifier")) {
            highlight = HL_FIELD;
        }
    }

    // Set highlight
    if (highlight != HL_NORMAL) {
        // printf("start [%d, %d]\r\n", start.row, start.column);
        // printf("end [%d, %d]\r\n", end.row, end.column);

        erow *row = &E.row[start.row];

        // node spans multiple lines
        if (end.row > start.row) {
            memset(&row->highlight[start.column], highlight, row->size - start.column);

            for (uint32_t r = start.row + 1; r < end.row; r++) {
                row = &E.row[r];
                memset(row->highlight, highlight, row->size);
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

    // Highlight node children
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
        row->highlight = realloc(row->highlight, row->size);
        // Fill array with default highlight
        memset(row->highlight, HL_NORMAL, row->size);
    }

    TSNode root = ts_tree_root_node(E.syntax->tree);

    // printf("UPDATE:\r\n");

    editorHighlightSubtree(root);
}

/*
 * Convert `editorHighlight` constant `hl` to ANSI escape code number
 * See: https://ss64.com/nt/syntax-ansi.html
 */
int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 35; // Magenta
        case HL_KEYWORD1:
            return 34; // Blue
        case HL_KEYWORD2:
            return 33; // Yellow
        case HL_STRING:
            return 32; // Green
        case HL_NUMBER:
            return 31; // Red
        case HL_FUNCTION:
            return 34; // Blue
        case HL_SYNTAX:
            return 31; // Red
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
    if (E.syntax == NULL) {
        for (int i = 0; i < E.numrows; i++) {
            erow *row = &E.row[i];
            row->highlight = realloc(row->highlight, row->size);
            memset(row->highlight, HL_NORMAL, row->size);
        }
        return;
    }

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
    } else if (!strcmp(E.syntax->filetype, "Rust")) {
        TSLanguage *tree_sitter_rust();
        E.syntax->language = tree_sitter_rust();
        ts_parser_set_language(parser, E.syntax->language);
    } else {
        return;
    }

    int len;
    char *source_code = editorRowsToString(&len);

    // editorPrintSourceCode();

    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, len);

    E.syntax->tree = tree;
    E.syntax->parser = parser;

    // editorPrintSyntaxTree();

    editorUpdateHighlight();

    editorCalculateRenderedRows(0, E.numrows, E.numrows);
}

void editorUpdateSyntaxTree(int old_end_row, int old_end_column, int old_end_byte, int new_end_row, int new_end_column, int new_end_byte) {
    if (E.syntax == NULL) {
        return;
    }

    // Select edit range, setting the lowest point as the start
    TSInputEdit edit;

    if (new_end_row < old_end_row || (new_end_row == old_end_row && new_end_column < old_end_column)) {
        edit.start_byte = new_end_byte;
        edit.start_point = (TSPoint){ new_end_row, new_end_column };
    } else {
        edit.start_byte = old_end_byte;
        edit.start_point = (TSPoint){ old_end_row, old_end_column };
    }
    edit.new_end_byte = new_end_byte;
    edit.new_end_point = (TSPoint){ new_end_row, new_end_column };

    edit.old_end_byte = old_end_byte;
    edit.old_end_point = (TSPoint){ old_end_row, old_end_column };

    ts_tree_edit(E.syntax->tree, &edit);

    int len;
    char *source_code = editorRowsToString(&len);

    // editorPrintSourceCode();

    // TSTree *tree = ts_parser_parse_string(E.syntax->parser, NULL, source_code, len);
    TSTree *tree = ts_parser_parse_string(E.syntax->parser, E.syntax->tree, source_code, len);
    E.syntax->tree = tree;

    // editorPrintSyntaxTree();

    editorUpdateHighlight();

    editorCalculateRenderedRows(edit.start_point.row, edit.old_end_point.row, edit.new_end_point.row);
}
