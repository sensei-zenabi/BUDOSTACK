/*
 * libedit.c (tailored version)
 *
 * This version implements a simplified syntax highlighter for C source lines
 * that is designed to integrate better with edit.c’s row rendering and
 * selection highlighting.
 *
 * Changes relative to the original:
 *  - Only C keywords are highlighted (in blue). String literals, character 
 *    literals, and comments are left unmodified so that extra ANSI codes do not 
 *    interfere with the editor’s display calculations and selection highlighting.
 *  - The function still returns a newly allocated string containing ANSI escape 
 *    codes, but its visible length equals the original line’s length.
 *
 * Design principles:
 *  - Plain C using -std=c11 and only standard libraries.
 *  - No separate header files.
 *  - Detailed inline comments explain the design and choices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * highlight_c_line
 *
 * Takes a single line of C code (null-terminated string) and returns a new
 * string with ANSI escape sequences inserted for syntax highlighting of
 * C keywords. It leaves string/character literals and comments unchanged.
 *
 * The caller is responsible for freeing the returned string.
 *
 * Limitations:
 *  - Only keywords are highlighted.
 *  - This version is tailored for integration with edit.c so that the extra
 *    escape sequences do not affect cursor positioning and selection highlighting.
 */
char *highlight_c_line(const char *line) {
    size_t len = strlen(line);
    /* Worst-case: every character is wrapped in escape codes.
       Allocate extra space accordingly. */
    size_t buf_size = len * 3 + 1;
    char *result = malloc(buf_size);
    if (!result) {
        return NULL;
    }
    size_t ri = 0;  // index in result

    for (size_t i = 0; i < len; i++) {
        char c = line[i];

        /* Check for potential keyword start:
           - A keyword starts with a letter or underscore
           - and is preceded by a non-identifier character or is at the beginning */
        if ((c == '_' || isalpha((unsigned char)c)) &&
            ((i == 0) || (!isalnum((unsigned char)line[i - 1]) && line[i - 1] != '_'))) {
            size_t j = i;
            while (j < len && (line[j] == '_' || isalnum((unsigned char)line[j])))
                j++;
            size_t word_len = j - i;
            char word[64];
            if (word_len < sizeof(word)) {
                memcpy(word, line + i, word_len);
                word[word_len] = '\0';
            } else {
                word[0] = '\0';
            }
            /* List of C keywords to highlight */
            int is_keyword = 0;
            static const char *keywords[] = {
                "auto", "break", "case", "char", "const", "continue", "default",
                "do", "double", "else", "enum", "extern", "float", "for", "goto",
                "if", "inline", "int", "long", "register", "restrict", "return",
                "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
                "union", "unsigned", "void", "volatile", "while",
                "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
                "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
            };
            size_t num_keywords = sizeof(keywords) / sizeof(keywords[0]);
            for (size_t k = 0; k < num_keywords; k++) {
                if (strcmp(word, keywords[k]) == 0) {
                    is_keyword = 1;
                    break;
                }
            }
            if (is_keyword) {
                const char *kw_color = "\x1b[34m";  // blue color for keywords
                size_t cl = strlen(kw_color);
                if (ri + cl < buf_size) {
                    memcpy(result + ri, kw_color, cl);
                    ri += cl;
                }
                for (size_t k = i; k < j; k++) {
                    result[ri++] = line[k];
                }
                const char *reset = "\x1b[0m";
                size_t rl = strlen(reset);
                if (ri + rl < buf_size) {
                    memcpy(result + ri, reset, rl);
                    ri += rl;
                }
                i = j - 1;
                continue;
            }
        }

        /* Default: copy the current character unchanged */
        result[ri++] = c;
    }
    result[ri] = '\0';
    return result;
}

/*
 * For testing purposes, one may compile this file with a main() function.
 * Remove or comment out the main() below when using this file as a library.
 *
 * To compile:
 *    cc -std=c11 -Wall -o libedit_test libedit.c
 */
/*
int main(void) {
    const char *test_lines[] = {
        "int main() {",
        "    // This is a comment",
        "    char *s = \"Hello, world!\";",
        "    if (s != NULL) return 0;",
        "}",
        NULL
    };
    for (int i = 0; test_lines[i] != NULL; i++) {
        char *highlighted = highlight_c_line(test_lines[i]);
        if (highlighted) {
            printf("%s\n", highlighted);
            free(highlighted);
        }
    }
    return 0;
}
*/
