#include <stdlib.h>
#include <tree_sitter/api.h>

#ifndef LANGUAGES_H
#define LANGUAGES_H

/*
 * Struct for storing information for highlighting a particular filetype
 */
struct editorSyntax {
    char *filetype;
    // Keywords in the filename to detect filetype
    char **filematch;

    // // Keywords of the current filetype (two types: add | after a keyword to set type2)
    // char **keywords;
    // // String that starts a single line comment
    // char *single_line_comment_start;
    // // String that starts a multi line comment
    // char *multi_line_comment_start;
    // // String that ends a multi line comment
    // char *multi_line_comment_end;
    // // Flags that determine what to highlight
    // int flags;

    // language specific highlight strings
    char **keyword1;
    char **keyword2;
    char **syntax1;
    char **syntax2;

    // char **keyword1;
    // char **keyword2;
    // char **types;
    // char *return;
    // char *number;
    // char **function;

    // tree-sitter syntax tree
    TSTree *tree;
    // current tree-sitter language
    TSLanguage *language;
    // tree-sitter parser for current language
    TSParser *parser;
};

extern struct editorSyntax HLDB[];

size_t num_hldb_entries();

#endif
