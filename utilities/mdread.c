/* mdread.c - A markdown pretty printer for the Linux terminal.
   It supports common markdown elements such as headers, lists (ordered and unordered),
   bold and italic inline formatting, and blockquotes.
   HTML tags (e.g. <br>, <p>, etc.) are removed from the output.

   Compile with:
     gcc -std=c11 -o mdread mdread.c

   Usage:
     ./mdread <filename>
     Running without a filename prints supported markdown features and exits.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 4096   // Maximum line length from input file.
#define OUT_SIZE (MAX_LINE * 2)  // Output buffer size (allows room for ANSI codes).

// get_ansi_code: Returns the ANSI escape code corresponding to the current formatting state.
const char *get_ansi_code(int bold, int italic) {
    if (bold && italic)
        return "\033[1;3m";  // Bold and italic.
    if (bold)
        return "\033[1m";    // Bold.
    if (italic)
        return "\033[3m";    // Italic.
    return "\033[0m";        // Reset.
}

// remove_html_tags: Removes any HTML tags (content between '<' and '>') from the input string.
// The resulting string is stored in output.
void remove_html_tags(const char *input, char *output) {
    int in_tag = 0;
    int j = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == '<') {
            in_tag = 1;
            continue;
        }
        if (input[i] == '>' && in_tag) {
            in_tag = 0;
            continue;
        }
        if (!in_tag) {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

// process_inline: Processes inline markdown formatting for bold and italic.
// It recognizes the following markers:
//    **text** or __text__  => bold
//    *text* or _text_      => italic
// It also supports a triple marker (*** or ___) to toggle both bold and italic.
// The transformed text (with ANSI escape sequences) is stored in output.
void process_inline(const char *input, char *output) {
    int i = 0, j = 0;
    int bold_active = 0, italic_active = 0;

    while (input[i] != '\0') {
        // Check for markdown markers '*' or '_'
        if (input[i] == '*' || input[i] == '_') {
            char marker = input[i];
            int count = 0;
            // Count consecutive marker characters.
            while (input[i] == marker) {
                count++;
                i++;
            }
            // Process markers in groups.
            while (count > 0) {
                if (count >= 3) {
                    // Toggle both bold and italic.
                    bold_active = !bold_active;
                    italic_active = !italic_active;
                    const char *code = get_ansi_code(bold_active, italic_active);
                    j += sprintf(output + j, "%s", code);
                    count -= 3;
                } else if (count == 2) {
                    // Toggle bold.
                    bold_active = !bold_active;
                    const char *code = get_ansi_code(bold_active, italic_active);
                    j += sprintf(output + j, "%s", code);
                    count -= 2;
                } else { // count == 1
                    // Toggle italic.
                    italic_active = !italic_active;
                    const char *code = get_ansi_code(bold_active, italic_active);
                    j += sprintf(output + j, "%s", code);
                    count -= 1;
                }
            }
        } else {
            // Regular character; copy it.
            output[j++] = input[i++];
        }
    }
    // Reset formatting at the end of the line.
    if (bold_active || italic_active) {
        j += sprintf(output + j, "%s", "\033[0m");
    }
    output[j] = '\0';
}

// trim_left: Returns a pointer to the first non-whitespace character in the string.
char *trim_left(char *str) {
    while (*str && isspace((unsigned char)*str))
        str++;
    return str;
}

// remove_trailing_hashes: Removes trailing spaces and '#' characters from header text
// if the '#' markers are preceded by at least one whitespace.
// This ensures that headers like "### MY HEADER ###" are printed as "MY HEADER".
void remove_trailing_hashes(char *s) {
    int len = strlen(s);
    if (len == 0) return;

    // Remove trailing newline if present.
    if (s[len-1] == '\n') {
        s[len-1] = '\0';
        len--;
    }

    // Trim trailing whitespace.
    while (len > 0 && isspace((unsigned char)s[len-1]))
        len--;

    // Identify trailing '#' characters.
    int hashStart = len;
    while (hashStart > 0 && s[hashStart-1] == '#')
        hashStart--;

    // Only remove the trailing '#' markers if they are preceded by a space.
    if (hashStart < len && hashStart > 0 && isspace((unsigned char)s[hashStart-1])) {
        s[hashStart-1] = '\0';
    } else {
        s[len] = '\0';
    }
}

int main(int argc, char *argv[]) {
    FILE *fp;
    char line[MAX_LINE];
    char cleaned[MAX_LINE];
    char inline_out[OUT_SIZE];
    char *filename;
    int last_was_list_item = 0; // Track if the last printed line was a list item.

    if (argc <= 1) {
        const char *help_text[] = {
            "mdread - simple markdown viewer",
            "",
            "Usage: mdread <file>",
            "",
            "Supported markdown elements:",
            "  - Headers: lines starting with one or more '#' characters. Level 1",
            "    headers are bold and underlined; deeper levels are printed in bold.",
            "  - Unordered lists: lines beginning with '-', '*' or '+', followed by",
            "    a space. Items are shown with a leading '-'.",
            "  - Ordered lists: digits followed by a '.' and a space. The number is",
            "    preserved in the output.",
            "  - Inline formatting: '*text*' or '_text_' for italics; '**text**' or",
            "    '__text__' for bold; triple markers apply both.",
            "  - Blockquotes: lines starting with '>' (optionally followed by a space)",
            "    are italicized and prefixed with '>'.",
            "  - HTML tags: text enclosed in '<' and '>' is stripped from the output.",
            "",
            "Notes:",
            "  - Output uses standard ASCII characters compatible with lat1u-08.psf.",
            "  - Inline styles reset at the end of each line to avoid spilling over.",
            NULL
        };

        for (int i = 0; help_text[i] != NULL; i++)
            puts(help_text[i]);

        return EXIT_SUCCESS;
    }

    filename = argv[1];

    fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return EXIT_FAILURE;
    }

    // Process the file line by line.
    while (fgets(line, sizeof(line), fp)) {
        // Remove HTML tags.
        remove_html_tags(line, cleaned);
        // Trim leading whitespace.
        char *trimmed = trim_left(cleaned);

        // If the line is empty, only print a newline if the previous line wasn't a list item.
        if (*trimmed == '\0') {
            if (!last_was_list_item)
                printf("\n");
            continue;
        }

        // Check for markdown header (lines starting with '#').
        if (trimmed[0] == '#') {
            // If the previous block was a list, insert an extra newline.
            if (last_was_list_item)
                printf("\n");

            int level = 0;
            while (trimmed[level] == '#')
                level++;
            // Skip the '#' markers and following space if present.
            int pos = level;
            if (trimmed[pos] == ' ')
                pos++;
            // Copy header text into a temporary buffer.
            char headerText[MAX_LINE];
            strncpy(headerText, trimmed + pos, MAX_LINE - 1);
            headerText[MAX_LINE - 1] = '\0';
            // Remove trailing '#' markers from header text.
            remove_trailing_hashes(headerText);
            process_inline(headerText, inline_out);
            // Apply header formatting based on level.
            // Level 1: Bold and Underlined, Level 2 and beyond: Bold.
            if (level == 1) {
                printf("\033[1;4m%s\033[0m\n", inline_out);
            } else {
                printf("\033[1m%s\033[0m\n", inline_out);
            }
            last_was_list_item = 0;
        }
        // Check for unordered list items (starting with '-', '*', or '+')
        else if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
                 isspace((unsigned char)trimmed[1])) {
            int pos = 2; // Skip marker and following space.
            process_inline(trimmed + pos, inline_out);
            // Remove trailing newline if present.
            size_t len = strlen(inline_out);
            if (len > 0 && inline_out[len-1] == '\n')
                inline_out[len-1] = '\0';
            printf("  - %s\n", inline_out);
            last_was_list_item = 1;
        }
        // Check for ordered list items (starting with digits followed by '.' and a space)
        else if (isdigit((unsigned char)trimmed[0])) {
            int idx = 0;
            while (isdigit((unsigned char)trimmed[idx]))
                idx++;
            if (trimmed[idx] == '.' && isspace((unsigned char)trimmed[idx+1])) {
                int pos = idx + 2;  // Skip number, dot, and space.
                char number[64];
                strncpy(number, trimmed, idx);
                number[idx] = '\0';
                process_inline(trimmed + pos, inline_out);
                // Remove trailing newline if present.
                size_t len = strlen(inline_out);
                if (len > 0 && inline_out[len-1] == '\n')
                    inline_out[len-1] = '\0';
                printf("  %s. %s\n", number, inline_out);
                last_was_list_item = 1;
            } else {
                process_inline(trimmed, inline_out);
                printf("%s", inline_out);
                last_was_list_item = 0;
            }
        }
        // Check for blockquotes (lines starting with '>')
        else if (trimmed[0] == '>') {
            int pos = 1;
            if (trimmed[pos] == ' ')
                pos++;
            process_inline(trimmed + pos, inline_out);
            printf("\033[3m> %s\033[0m\n", inline_out);  // Italicize blockquotes.
            last_was_list_item = 0;
        }
        // Default: process as a regular paragraph.
        else {
            process_inline(trimmed, inline_out);
            printf("%s", inline_out);
            last_was_list_item = 0;
        }
    }

    fclose(fp);
    return EXIT_SUCCESS;
}
