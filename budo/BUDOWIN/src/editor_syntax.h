#ifndef BUDOWIN_EDITOR_SYNTAX_H
#define BUDOWIN_EDITOR_SYNTAX_H

#include <ctype.h>
#include <dirent.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BW_SYNTAX_LANGUAGES 64
#define BW_SYNTAX_RULES 32
#define BW_SYNTAX_SPANS 16

enum BwSyntaxStyle {
    BW_SYNTAX_TEXT, BW_SYNTAX_KEYWORD, BW_SYNTAX_COMMENT,
    BW_SYNTAX_STRING, BW_SYNTAX_NUMBER, BW_SYNTAX_DIRECTIVE,
    BW_SYNTAX_HEADING, BW_SYNTAX_TAG, BW_SYNTAX_OPERATOR
};

typedef struct BwSyntaxRule {
    regex_t regex;
    unsigned char style;
} BwSyntaxRule;

typedef struct BwSyntaxSpan {
    char begin[32], end[32];
    unsigned char style;
    int multiline, escape;
} BwSyntaxSpan;

typedef struct BwSyntaxLanguage {
    char name[64], extensions[256], filenames[256], keywords[4096];
    BwSyntaxRule rules[BW_SYNTAX_RULES];
    BwSyntaxSpan spans[BW_SYNTAX_SPANS];
    int rule_count, span_count;
} BwSyntaxLanguage;

static BwSyntaxLanguage bw_syntax_languages[BW_SYNTAX_LANGUAGES];
static int bw_syntax_count;

static void bw_syntax_clear(void)
{
    for (int i = 0; i < bw_syntax_count; ++i)
        for (int j = 0; j < bw_syntax_languages[i].rule_count; ++j)
            regfree(&bw_syntax_languages[i].rules[j].regex);
    memset(bw_syntax_languages, 0, sizeof(bw_syntax_languages));
    bw_syntax_count = 0;
}

static int bw_syntax_style(const char *name)
{
    static const char *names[] = {"text", "keyword", "comment", "string", "number",
                                 "directive", "heading", "tag", "operator"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i])) return (int)i;
    return -1;
}

static int bw_syntax_copy(char *dest, size_t capacity, const char *source)
{
    size_t length = strlen(source);
    if (length >= capacity) return 0;
    memcpy(dest, source, length + 1);
    return 1;
}

static int bw_syntax_load_file(const char *path)
{
    if (bw_syntax_count == BW_SYNTAX_LANGUAGES) {
        fprintf(stderr, "BUDOWIN: language limit reached: %s\n", path);
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (!file) { perror(path); return 0; }
    BwSyntaxLanguage *language = &bw_syntax_languages[bw_syntax_count];
    char buffer[8192];
    int valid = 1, line = 0;
    while (fgets(buffer, sizeof(buffer), file)) {
        ++line;
        size_t length = strlen(buffer);
        if (length == sizeof(buffer) - 1 && buffer[length - 1] != '\n') { valid = 0; break; }
        buffer[strcspn(buffer, "\r\n")] = 0;
        if (!buffer[0] || buffer[0] == '#') continue;
        char *value = strchr(buffer, '=');
        if (!value) { valid = 0; break; }
        *value++ = 0;
        if (!strcmp(buffer, "name")) valid = bw_syntax_copy(language->name, sizeof(language->name), value);
        else if (!strcmp(buffer, "extensions")) valid = bw_syntax_copy(language->extensions, sizeof(language->extensions), value);
        else if (!strcmp(buffer, "filenames")) valid = bw_syntax_copy(language->filenames, sizeof(language->filenames), value);
        else if (!strcmp(buffer, "keywords")) valid = bw_syntax_copy(language->keywords, sizeof(language->keywords), value);
        else if (!strcmp(buffer, "rule")) {
            char *pattern = strchr(value, '|');
            if (!pattern || language->rule_count == BW_SYNTAX_RULES) { valid = 0; break; }
            *pattern++ = 0;
            int style = bw_syntax_style(value);
            BwSyntaxRule *rule = &language->rules[language->rule_count];
            if (style < 0) { valid = 0; break; }
            int error = regcomp(&rule->regex, pattern, REG_EXTENDED);
            if (error) {
                char message[160];
                regerror(error, &rule->regex, message, sizeof(message));
                fprintf(stderr, "BUDOWIN: %s:%d: %s\n", path, line, message);
                valid = 0;
                break;
            }
            rule->style = (unsigned char)style;
            ++language->rule_count;
        } else if (!strcmp(buffer, "span")) {
            char *begin = strchr(value, '|');
            char *end = begin ? strchr(begin + 1, '|') : NULL;
            char *flags = end ? strchr(end + 1, '|') : NULL;
            if (!flags || language->span_count == BW_SYNTAX_SPANS) { valid = 0; break; }
            *begin++ = *end++ = *flags++ = 0;
            int style = bw_syntax_style(value);
            BwSyntaxSpan *span = &language->spans[language->span_count];
            if (style < 0 || !*begin || !*end ||
                !bw_syntax_copy(span->begin, sizeof(span->begin), begin) ||
                !bw_syntax_copy(span->end, sizeof(span->end), end)) { valid = 0; break; }
            span->style = (unsigned char)style;
            span->multiline = strstr(flags, "multiline") != NULL;
            span->escape = strstr(flags, "escape") != NULL;
            ++language->span_count;
        } else valid = 0;
        if (!valid) break;
    }
    if (ferror(file)) valid = 0;
    if (fclose(file)) valid = 0;
    if (!language->name[0] || (!language->extensions[0] && !language->filenames[0])) valid = 0;
    if (!valid) {
        fprintf(stderr, "BUDOWIN: invalid language definition %s:%d; skipped\n", path, line);
        for (int i = 0; i < language->rule_count; ++i) regfree(&language->rules[i].regex);
        memset(language, 0, sizeof(*language));
        return 0;
    }
    ++bw_syntax_count;
    return 1;
}

static int bw_syntax_lang_file(const struct dirent *entry)
{
    const char *dot = strrchr(entry->d_name, '.');
    return dot && !strcasecmp(dot, ".lang");
}

static void bw_syntax_load(const char *directory)
{
    struct dirent **entries = NULL;
    bw_syntax_clear();
    int count = scandir(directory, &entries, bw_syntax_lang_file, alphasort);
    if (count < 0) { perror(directory); return; }
    for (int i = 0; i < count; ++i) {
        char path[4096];
        int size = snprintf(path, sizeof(path), "%s/%s", directory, entries[i]->d_name);
        if (size > 0 && (size_t)size < sizeof(path)) (void)bw_syntax_load_file(path);
        else fprintf(stderr, "BUDOWIN: language path too long\n");
        free(entries[i]);
    }
    free(entries);
}

static int bw_syntax_word(const char *list, const char *word, size_t length, int insensitive)
{
    while (*list) {
        while (*list && isspace((unsigned char)*list)) ++list;
        const char *start = list;
        while (*list && !isspace((unsigned char)*list)) ++list;
        if ((size_t)(list - start) == length &&
            !(insensitive ? strncasecmp(start, word, length) : strncmp(start, word, length))) return 1;
    }
    return 0;
}

static const BwSyntaxLanguage *bw_syntax_for_path(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *extension = strrchr(base, '.');
    for (int i = 0; i < bw_syntax_count; ++i) {
        const BwSyntaxLanguage *language = &bw_syntax_languages[i];
        if ((extension && bw_syntax_word(language->extensions, extension, strlen(extension), 1)) ||
            bw_syntax_word(language->filenames, base, strlen(base), 0)) return language;
    }
    return NULL;
}

/* State is an open multiline span index + 1, local to a document render. */
static void bw_syntax_line(const BwSyntaxLanguage *language, const char *text,
                           int *state, unsigned char *styles)
{
    size_t length = strlen(text), pos = 0;
    if (styles) memset(styles, BW_SYNTAX_TEXT, length);
    if (!language) { *state = 0; return; }
    while (pos < length) {
        int span_index = *state - 1;
        if (!*state) {
            for (int i = 0; i < language->span_count; ++i) {
                size_t begin_length = strlen(language->spans[i].begin);
                if (begin_length <= length - pos && !strncmp(text + pos, language->spans[i].begin, begin_length)) {
                    span_index = i;
                    break;
                }
            }
        }
        if (span_index >= 0 && span_index < language->span_count) {
            const BwSyntaxSpan *span = &language->spans[span_index];
            size_t start = pos, end_length = strlen(span->end);
            if (!*state) pos += strlen(span->begin);
            *state = span_index + 1;
            while (pos < length) {
                if (span->escape && text[pos] == '\\') { pos += pos + 1 < length ? 2 : 1; continue; }
                if (end_length <= length - pos && !strncmp(text + pos, span->end, end_length)) {
                    pos += end_length;
                    *state = 0;
                    break;
                }
                ++pos;
            }
            if (styles) memset(styles + start, span->style, pos - start);
            if (!span->multiline) *state = 0;
            continue;
        }
        size_t matched = 0;
        for (int i = 0; i < language->rule_count; ++i) {
            regmatch_t match;
            if (!regexec(&language->rules[i].regex, text + pos, 1, &match, pos ? REG_NOTBOL : 0) &&
                match.rm_so == 0 && match.rm_eo > 0) {
                matched = (size_t)match.rm_eo;
                if (styles) memset(styles + pos, language->rules[i].style, matched);
                break;
            }
        }
        if (matched) { pos += matched; continue; }
        if (isalpha((unsigned char)text[pos]) || text[pos] == '_') {
            size_t start = pos++;
            while (isalnum((unsigned char)text[pos]) || text[pos] == '_') ++pos;
            if (styles && bw_syntax_word(language->keywords, text + start, pos - start, 0))
                memset(styles + start, BW_SYNTAX_KEYWORD, pos - start);
        } else ++pos;
    }
}
#endif
