#include "highlight.h"
#include "languages.h"
#include <stdlib.h>

/*** filetypes ***/

/* C/C++ highlighting */

// file extensions
char *C_HL_extensions[] = { ".c", ".h", ".cc", ".cpp", NULL };
// highlight groups
char *C_HL_keyword1[] = { "break", "case", "class", "continue", "do", "else", "enum", "false", "for", "goto", "if", "return", "sizeof", "struct", "switch", "true", "typedef", "union", "while", NULL };
char *C_HL_keyword2[] = { "#include", "#ifdef", "#endif", "#ifndef", "#define", NULL };
char *C_HL_syntax1[] = { "(", ")", "{", "}", "[", "]", ";", ".", NULL };
char *C_HL_syntax2[] = { "?", ":", NULL };

// /* Python highlighting */

// file extensions
char *Python_HL_extensions[] = { ".py", ".py3", NULL };
char *Python_HL_keyword1[] = { "and", "as", "assert", "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", NULL };
char *Python_HL_keyword2[] = { "false", "true", "none", NULL };
char *Python_HL_syntax1[] = { "(", ")", "{", "}", "[", "]", ";", ".", NULL };
char *Python_HL_syntax2[] = { "?", ":", NULL };

/* Rust highlighting */

// Rust file extensions
char *Rust_HL_extensions[] = { ".rs", NULL };
char *Rust_HL_keyword1[] = { "as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", NULL };
char *Rust_HL_keyword2[] = { "false", "true", "None", "Some", "Ok", "Err", NULL };
char *Rust_HL_syntax1[] = { "(", ")", "{", "}", "[", "]", "<", ">", ";", ".", "::", "&", NULL };
char *Rust_HL_syntax2[] = { "?", ":", "->", "=>", "#", NULL };

/* Haskell highlighting */

// Haskell file extensions
char *Haskell_HL_extensions[] = { ".hs", NULL };
char *Haskell_HL_keyword1[] = {
    "as", "of", "case", "class", "data", "data family", "data instance", "default", "deriving", "deriving instance", "do", "forall", "foreign", "hiding", "if", "then", "else", "import", "infix", "infixl", "infixr", "instance", "let", "in", "mdo", "module", "newtype", "proc", "qualified", "rec", "type", "type family", "type instance", "where", NULL };
char *Haskell_HL_keyword2[] = { NULL };
char *Haskell_HL_syntax1[] = { "(", ")", "{", "}", "[", "]", "<", ">", ";", ".", "::", "&", "|", ":", NULL };
char *Haskell_HL_syntax2[] = { "?", "<-", "->", "=>", "#", "@", NULL };
    // "True|", "False|", "String|", "Char|", "Read|", "Show|", "Eq|", "Ord|", "Enum|", "Bounded|", "IO|", "Maybe|", "Either|", "Ordering|", "Functor|", "Monad|", "Num|", "Complex|", "Real|", "Integral|", "Fractional|", "Floating|", "Int|", "Integer|", "Double|", "Rational|", "Ratio|", "RealFloat|", "RealFrac|", NULL

/*
 * highlight database
 */
struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keyword1,
        C_HL_keyword2,
        C_HL_syntax1,
        C_HL_syntax2,
        NULL, NULL, NULL
    },
    {
        "Python",
        Python_HL_extensions,
        Python_HL_keyword1,
        Python_HL_keyword2,
        Python_HL_syntax1,
        Python_HL_syntax2,
        NULL, NULL, NULL
    },
    {
        "Rust",
        Rust_HL_extensions,
        Rust_HL_keyword1,
        Rust_HL_keyword2,
        Rust_HL_syntax1,
        Rust_HL_syntax2,
        NULL, NULL, NULL
    },
    {
        "Haskell",
        Haskell_HL_extensions,
        Haskell_HL_keyword1,
        Haskell_HL_keyword2,
        Haskell_HL_syntax1,
        Haskell_HL_syntax2,
        NULL, NULL, NULL
    }
};

#define NUM_HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

size_t num_hldb_entries() {
    return NUM_HLDB_ENTRIES;
}
