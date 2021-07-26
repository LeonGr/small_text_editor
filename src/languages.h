#include <stdlib.h>

#ifndef LANGUAGES_H
#define LANGUAGES_H

/*
 * Struct for storing information for highlighting a particular filetype
 */
struct editorSyntax {
    char *filetype;
    // Keywords in the filename to detect filetype
    char **filematch;
    // Keywords of the current filetype (two types: add | after a keyword to set type2)
    char **keywords;
    // String that starts a single line comment
    char *single_line_comment_start;
    // String that starts a multi line comment
    char *multi_line_comment_start;
    // String that ends a multi line comment
    char *multi_line_comment_end;
    // Flags that determine what to highlight
    int flags;
};

extern struct editorSyntax HLDB[];

size_t num_hldb_entries();

#endif
