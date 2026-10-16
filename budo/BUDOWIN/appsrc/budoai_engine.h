#ifndef BUDOAI_ENGINE_H
#define BUDOAI_ENGINE_H
/* Native POSIX implementation. No host UI state or network facilities. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define AI_PATH 4096
#define AI_CHUNK 640
#define AI_TERMS 96
#define AI_DOCS 256
#define AI_PASSAGES 4096
#define AI_DEPTH 16
#define AI_QUERY 24
#define AI_RESULTS 6
#define AI_WORD_SLOTS 8192
#define AI_WORD_LIMIT 4096
#define AI_FILE_BYTES (2L * 1024 * 1024)
#define AI_TOTAL_BYTES (32L * 1024 * 1024)

typedef struct AiBag {
    uint64_t term[AI_TERMS], pair[AI_TERMS];
    unsigned short pairs;
    unsigned short freq[AI_TERMS], count, length;
} AiBag;
typedef struct AiWord {
    char text[48];
    uint64_t raw_hash, term;
} AiWord;
enum { AI_GENERIC, AI_DEFINITION, AI_PROCEDURE, AI_CAUSE, AI_SIMPLER, AI_EQUIPMENT, AI_IDENTITY };
#define AI_F_DEFINE 1U
#define AI_F_STEPS 2U
#define AI_F_CAUSE 4U
#define AI_F_SIMPLE 8U
#define AI_F_EQUIPMENT 16U
#define AI_F_PERMISSION 32U
#define AI_F_COMPLETE 64U

typedef struct AiDoc {
    char path[AI_PATH];
    dev_t device;
    ino_t inode;
    off_t size;
    struct timespec modified;
} AiDoc;
typedef struct AiPassage {
    AiBag bag;
    long offset;
    unsigned int first_line, last_line;
    unsigned short doc, bytes;
    unsigned char heading, features;
    uint64_t checksum, relation[8];
    unsigned char relations;
} AiPassage;
typedef struct AiDir { DIR *handle; char path[AI_PATH]; } AiDir;
typedef struct AiHit { int passage; double score; } AiHit;
typedef struct AiEngine {
    char root[AI_PATH], category[AI_PATH], question[256];
    AiDoc *docs;
    AiWord *words;
    int word_count;
    AiPassage *passages;
    size_t count, capacity;
    int documents, errors, limited, depth, phase;
    AiDir dirs[AI_DEPTH];
    FILE *file;
    int current_doc;
    long offset, total_bytes;
    unsigned int line;
    uint64_t query[AI_QUERY], query_pair[AI_QUERY];
    int query_pairs;
    unsigned int df[AI_QUERY];
    int query_count, question_type, use_context, intent, permission;
    uint64_t subject[AI_QUERY];
    int subject_count;
    uint64_t identity;
    char corrections[160];
    size_t cursor;
    double average;
    AiHit hits[AI_RESULTS * 4];
    int hit_count, result_count;
    int result[AI_RESULTS];
    char excerpts[AI_RESULTS][AI_CHUNK + 1];
} AiEngine;

enum { AI_READY, AI_INDEX, AI_DF, AI_SCORE };

static uint64_t ai_hash(const char *s, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i) {
        hash ^= (unsigned char)s[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static void ai_normalize(char *word)
{
    /* Small lexical equivalence classes, independent of topics or questions. */
    static const char *const aliases[][2] = {
        {"stuff", "equipment"}, {"gear", "equipment"}, {"kit", "equipment"},
        {"remember", "memory"}, {"remembers", "memory"}, {"remembering", "memory"},
        {"electrical", "electricity"}, {"electric", "electricity"},
        {"things", "object"}, {"thing", "object"}, {"objects", "object"},
        {"moving", "motion"}, {"moves", "motion"}, {"move", "motion"},
        {"movement", "motion"}, {"find", "search"}, {"finding", "search"},
        {"locate", "search"}, {"items", "list"}, {"item", "list"}
    };
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        if (!strcmp(word, aliases[i][0])) { strcpy(word, aliases[i][1]); break; }
    }
    size_t n = strlen(word);
    if (n > 4 && !strcmp(word + n - 3, "ies")) strcpy(word + n - 3, "y");
    else if (n > 5 && !strcmp(word + n - 3, "ing")) {
        word[n - 3] = '\0';
        if (n > 6 && word[n - 4] == word[n - 5]) word[n - 4] = '\0';
    } else if (n > 4 && !strcmp(word + n - 2, "ed")) word[n - 2] = '\0';
    else if (n > 3 && word[n - 1] == 's' && word[n - 2] != 's') word[n - 1] = '\0';
    n = strlen(word);
    if (n > 4 && word[n - 1] == 'e') word[n - 1] = '\0';
}
static int ai_stop(const char *word)
{
    static const char stop[] = " a an the i we you they he she it is are was were be been being do does did to of for and or in on at by with from as this that these those my our your their me us can could would should will shall may might must have has had what who where when why how about explain tell please need needed use used using ";
    char surrounded[52];
    snprintf(surrounded, sizeof(surrounded), " %s ", word);
    return strstr(stop, surrounded) != NULL;
}
static void ai_learn_word(AiEngine *e, const char *raw, uint64_t term)
{
    if (!e || !e->words || e->word_count >= AI_WORD_LIMIT) return;
    uint64_t hash = ai_hash(raw, strlen(raw));
    size_t slot = (size_t)(hash % AI_WORD_SLOTS);
    while (e->words[slot].raw_hash && e->words[slot].raw_hash != hash)
        slot = (slot + 1) % AI_WORD_SLOTS;
    if (e->words[slot].raw_hash) return;
    AiWord *w = &e->words[slot];
    snprintf(w->text, sizeof(w->text), "%s", raw);
    w->raw_hash = hash;
    w->term = term;
    ++e->word_count;
}
/* A single insertion, deletion, substitution or adjacent transposition. */
static int ai_one_edit(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b), i = 0;
    if (na + 1 < nb || nb + 1 < na) return 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    if (!a[i] && !b[i]) return 0;
    if (na == nb) {
        if (!strcmp(a + i + 1, b + i + 1)) return 1;
        return i + 1 < na && a[i] == b[i + 1] && a[i + 1] == b[i] &&
            !strcmp(a + i + 2, b + i + 2);
    }
    return na > nb ? !strcmp(a + i + 1, b + i) : !strcmp(a + i, b + i + 1);
}
static uint64_t ai_correct(AiEngine *e, const char *raw, uint64_t original)
{
    if (!e->words || strlen(raw) < 5) return original;
    uint64_t hash = ai_hash(raw, strlen(raw));
    size_t slot = (size_t)(hash % AI_WORD_SLOTS);
    while (e->words[slot].raw_hash) {
        if (e->words[slot].raw_hash == hash) return original;
        slot = (slot + 1) % AI_WORD_SLOTS;
    }
    const AiWord *best = NULL;
    for (int i = 0; i < AI_WORD_SLOTS; ++i) {
        AiWord *w = &e->words[i];
        if (!w->raw_hash || !ai_one_edit(raw, w->text)) continue;
        if (best && best->term != w->term) return original; /* Ambiguous: never guess. */
        if (!best || strcmp(w->text, best->text) < 0) best = w;
    }
    if (!best || best->term == original) return original;
    size_t used = strlen(e->corrections);
    snprintf(e->corrections + used, sizeof(e->corrections) - used, "%s%s -> %s",
             used ? "; " : "", raw, best->text);
    return best->term;
}
static void ai_tokenize_ex(const char *text, AiBag *bag, AiEngine *e, int query)
{
    memset(bag, 0, sizeof(*bag));
    uint64_t previous = 0;
    for (size_t i = 0; text[i];) {
        if (!isalnum((unsigned char)text[i]) || (unsigned char)text[i] >= 128) { ++i; continue; }
        char word[48], raw[48];
        size_t n = 0;
        while (isalnum((unsigned char)text[i]) && (unsigned char)text[i] < 128) {
            if (n < sizeof(word) - 1) word[n++] = (char)tolower((unsigned char)text[i]);
            ++i;
        }
        word[n] = '\0';
        if (n == 1 && !isdigit((unsigned char)word[0])) continue;
        if (ai_stop(word)) continue;
        if (query && (!strcmp(word, "go") || !strcmp(word, "start") || !strcmp(word, "begin") ||
            !strcmp(word, "mean") || !strcmp(word, "means"))) continue;
        snprintf(raw, sizeof(raw), "%s", word);
        ai_normalize(word);
        uint64_t hash = ai_hash(word, strlen(word));
        if (query) hash = ai_correct(e, raw, hash);
        else ai_learn_word(e, raw, hash);
        if (previous && bag->pairs < AI_TERMS)
            bag->pair[bag->pairs++] = (previous * UINT64_C(1099511628211)) ^ hash;
        previous = hash;
        ++bag->length;
        int t;
        for (t = 0; t < bag->count && bag->term[t] != hash; ++t) {}
        if (t == bag->count) {
            if (bag->count == AI_TERMS) continue;
            bag->term[bag->count++] = hash;
        }
        ++bag->freq[t];
    }
}
static void ai_tokenize(const char *text, AiBag *bag)
{
    ai_tokenize_ex(text, bag, NULL, 0);
}
/* Narrow copula support: exact adjacent subject/predicate heads, not inference. */
static int ai_article(const char *s)
{
    return !strcmp(s, "a") || !strcmp(s, "an") || !strcmp(s, "the");
}
static int ai_relations(const char *text, uint64_t *keys, int query)
{
    char words[128][48];
    int count = 0, found = 0;
    for (size_t i = 0; text[i] && count < 128;) {
        if (!isalnum((unsigned char)text[i])) { ++i; continue; }
        size_t n = 0;
        while (isalnum((unsigned char)text[i])) {
            if (n < 47) words[count][n++] = (char)tolower((unsigned char)text[i]);
            ++i;
        }
        words[count++][n] = '\0';
    }
    if (query) {
        if (count < 3 || (strcmp(words[0], "is") && strcmp(words[0], "are"))) return 0;
        int left = 1;
        while (left < count && ai_article(words[left])) ++left;
        int right = left + 1;
        while (right < count && !ai_article(words[right])) ++right;
        if (right == count) right = count - 1;
        else ++right;
        if (right >= count || right <= left) return 0;
        int subject = right - 1;
        while (subject >= left && ai_article(words[subject])) --subject;
        if (subject < left) return 0;
        ai_normalize(words[subject]);
        ai_normalize(words[right]);
        keys[0] = ai_hash(words[subject], strlen(words[subject])) * UINT64_C(1099511628211) ^
            ai_hash(words[right], strlen(words[right]));
        return 1;
    }
    for (int i = 1; i + 1 < count && found < 8; ++i) {
        if (strcmp(words[i], "is") && strcmp(words[i], "are")) continue;
        int right = i + 1;
        while (right < count && ai_article(words[right])) ++right;
        if (right >= count) continue;
        char left[48], predicate[48];
        snprintf(left, sizeof(left), "%s", words[i - 1]);
        snprintf(predicate, sizeof(predicate), "%s", words[right]);
        ai_normalize(left);
        ai_normalize(predicate);
        keys[found++] = ai_hash(left, strlen(left)) * UINT64_C(1099511628211) ^
            ai_hash(predicate, strlen(predicate));
    }
    return found;
}
static unsigned char ai_features(const char *text)
{
    char lower[AI_CHUNK + 1];
    size_t i = 0;
    for (; text[i] && i < AI_CHUNK; ++i) lower[i] = (char)tolower((unsigned char)text[i]);
    lower[i] = '\0';
    unsigned char f = 0;
    if (strstr(lower, "definition:") || strstr(lower, " is ") || strstr(lower, " means ") ||
        strstr(lower, " are ")) f |= AI_F_DEFINE;
    if (strstr(lower, "step 1") || strstr(lower, "step 2") || strstr(lower, "step 3") ||
        strstr(lower, "procedure:") || strstr(lower, "instructions:")) f |= AI_F_STEPS;
    if ((strstr(lower, "first ") || strstr(lower, "first,")) &&
        (strstr(lower, "then ") || strstr(lower, "finally "))) f |= AI_F_STEPS;
    if (strstr(lower, "1.") && strstr(lower, "2.")) f |= AI_F_STEPS;
    if (strstr(lower, "step 1") && strstr(lower, "step 2")) f |= AI_F_COMPLETE;
    if (strstr(lower, "because ") || strstr(lower, "due to ") || strstr(lower, "reason:")) f |= AI_F_CAUSE;
    if (strstr(lower, "simple explanation:") || strstr(lower, "in simple words:")) f |= AI_F_SIMPLE;
    if (strstr(lower, "equipment") || strstr(lower, "you need")) f |= AI_F_EQUIPMENT;
    if (strstr(lower, "can use ") || strstr(lower, "may use ") || strstr(lower, "must not ") ||
        strstr(lower, "cannot ") || strstr(lower, "do not ")) f |= AI_F_PERMISSION;
    return f;
}
static int ai_frequency(const AiBag *bag, uint64_t term)
{
    for (int i = 0; i < bag->count; ++i) if (bag->term[i] == term) return bag->freq[i];
    return 0;
}
static int ai_path(char *out, const char *base, const char *name)
{
    int n = snprintf(out, AI_PATH, "%s/%s", base, name);
    return n >= 0 && n < AI_PATH;
}
static void ai_error(AiEngine *e, const char *path, const char *reason)
{
    ++e->errors;
    fprintf(stderr, "BUDOAI: %s: %s\n", path, reason);
}
static void ai_close_io(AiEngine *e)
{
    if (e->file) { fclose(e->file); e->file = NULL; }
    while (e->depth) closedir(e->dirs[--e->depth].handle);
}
static void ai_destroy(AiEngine *e)
{
    ai_close_io(e);
    free(e->docs);
    free(e->words);
    free(e->passages);
    memset(e, 0, sizeof(*e));
}
static void ai_refresh(AiEngine *e, const char *root)
{
    char saved[AI_PATH];
    snprintf(saved, sizeof(saved), "%s", root);
    ai_destroy(e);
    snprintf(e->root, sizeof(e->root), "%s", saved);
    e->docs = calloc(AI_DOCS, sizeof(*e->docs));
    e->words = calloc(AI_WORD_SLOTS, sizeof(*e->words));
    if (!e->docs || !e->words) { ai_error(e, saved, "index allocation failed"); return; }
    DIR *dir = opendir(saved);
    if (!dir) { ai_error(e, saved, strerror(errno)); return; }
    e->dirs[0].handle = dir;
    snprintf(e->dirs[0].path, AI_PATH, "%s", saved);
    e->depth = 1;
    e->phase = AI_INDEX;
}
/* O_NOFOLLOW and regular-file checks exclude symlink loops, FIFOs and devices. */
static FILE *ai_open_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    FILE *f = fdopen(fd, "rb");
    if (!f) close(fd);
    return f;
}
static int ai_add_passage(AiEngine *e)
{
    char text[AI_CHUNK + 1];
    size_t n = 0;
    long offset = e->offset;
    unsigned int first = e->line, last = first;
    int c = EOF, previous = 0, malformed = 0;
    /* At most 640 bytes per step; long paragraphs split at whitespace. */
    while (n < AI_CHUNK && (c = fgetc(e->file)) != EOF) {
        text[n++] = (char)c;
        ++e->offset;
        ++e->total_bytes;
        last = e->line;
        if (c == '\n') ++e->line;
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127) malformed = 1;
        if ((c == '\n' && previous == '\n') || (n >= 480 && isspace((unsigned char)c))) break;
        if (c != '\r') previous = c;
    }
    text[n] = '\0';
    if (malformed) {
        ai_error(e, e->docs[e->current_doc].path, "unsupported control byte; remaining content skipped");
        fclose(e->file);
        e->file = NULL;
        return 1;
    }
    if (n) {
        if (e->count == e->capacity) {
            size_t capacity = e->capacity ? e->capacity * 2 : 64;
            if (capacity > AI_PASSAGES) capacity = AI_PASSAGES;
            AiPassage *p = realloc(e->passages, capacity * sizeof(*p));
            if (!p) {
                ai_error(e, e->root, "passage allocation failed");
                ai_close_io(e);
                e->phase = AI_READY;
                return 1;
            }
            e->passages = p;
            e->capacity = capacity;
        }
        AiPassage *p = &e->passages[e->count];
        memset(p, 0, sizeof(*p));
        ai_tokenize_ex(text, &p->bag, e, 0);
        p->features = ai_features(text);
        p->relations = (unsigned char)ai_relations(text, p->relation, 0);
        if (p->bag.count) {
            p->offset = offset;
            p->bytes = (unsigned short)n;
            p->doc = (unsigned short)e->current_doc;
            p->first_line = first;
            p->last_line = last;
            p->checksum = ai_hash(text, n);
            p->heading = p->bag.length < 8 && strpbrk(text, ".!?:;=") == NULL;
            e->average += p->bag.length;
            ++e->count;
        }
    }
    if (e->count >= AI_PASSAGES || e->total_bytes >= AI_TOTAL_BYTES) {
        e->limited = 1;
        ai_close_io(e);
        e->phase = AI_READY;
    } else if (c == EOF || e->offset >= AI_FILE_BYTES) {
        if (ferror(e->file)) ai_error(e, e->docs[e->current_doc].path, "read failed");
        if (e->offset >= AI_FILE_BYTES) e->limited = 1;
        fclose(e->file);
        e->file = NULL;
    }
    return 1;
}
static void ai_index_step(AiEngine *e)
{
    if (e->file) { (void)ai_add_passage(e); return; }
    if (!e->depth) { e->phase = AI_READY; return; }
    AiDir *dir = &e->dirs[e->depth - 1];
    errno = 0;
    struct dirent *item = readdir(dir->handle);
    if (!item) {
        if (errno) ai_error(e, dir->path, strerror(errno));
        closedir(dir->handle);
        --e->depth;
        return;
    }
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) return;
    char path[AI_PATH];
    struct stat st;
    if (!ai_path(path, dir->path, item->d_name)) { ai_error(e, dir->path, "child path too long"); return; }
    if (lstat(path, &st)) { ai_error(e, path, strerror(errno)); return; }
    if (S_ISDIR(st.st_mode)) {
        if (e->depth == AI_DEPTH) { e->limited = 1; return; }
        DIR *child = opendir(path);
        if (!child) { ai_error(e, path, strerror(errno)); return; }
        e->dirs[e->depth].handle = child;
        snprintf(e->dirs[e->depth++].path, AI_PATH, "%s", path);
    } else if (S_ISREG(st.st_mode)) {
        size_t n = strlen(path);
        if (n < 4 || path[n - 4] != '.' || tolower((unsigned char)path[n - 3]) != 't' ||
            tolower((unsigned char)path[n - 2]) != 'x' || tolower((unsigned char)path[n - 1]) != 't') return;
        if (e->documents == AI_DOCS) { e->limited = 1; return; }
        e->file = ai_open_file(path);
        if (!e->file) { ai_error(e, path, strerror(errno)); return; }
        if (fstat(fileno(e->file), &st)) {
            ai_error(e, path, strerror(errno));
            fclose(e->file);
            e->file = NULL;
            return;
        }
        e->current_doc = e->documents++;
        AiDoc *doc = &e->docs[e->current_doc];
        doc->device = st.st_dev;
        doc->inode = st.st_ino;
        doc->size = st.st_size;
        doc->modified = st.st_mtim;
        snprintf(e->docs[e->current_doc].path, AI_PATH, "%s", path);
        e->line = 1;
        e->offset = 0;
    }
}
static int ai_question_type(const char *question)
{
    char first[16];
    size_t n = 0;
    while (question[n] && !isspace((unsigned char)question[n]) && n < sizeof(first) - 1) {
        first[n] = (char)tolower((unsigned char)question[n]);
        ++n;
    }
    first[n] = '\0';
    static const char *types[] = {"what", "who", "where", "when", "why", "how"};
    for (int i = 0; i < 6; ++i) if (!strcmp(first, types[i])) return i + 1;
    return 0;
}
static void ai_ask(AiEngine *e, const char *question)
{
    if (e->phase != AI_READY) return;
    char lower[256];
    size_t n = 0;
    for (; question[n] && n < sizeof(lower) - 1; ++n) lower[n] = (char)tolower((unsigned char)question[n]);
    lower[n] = '\0';
    e->corrections[0] = '\0';
    e->question_type = ai_question_type(lower);
    e->intent = e->question_type == 5 ? AI_CAUSE : AI_GENERIC;
    if (strstr(lower, "what is ") || strstr(lower, "what are ") || strstr(lower, " mean")) e->intent = AI_DEFINITION;
    if (!strncmp(lower, "how do i ", 9) || !strncmp(lower, "how can i ", 10) ||
        !strncmp(lower, "how to ", 7)) e->intent = AI_PROCEDURE;
    if (!strncmp(lower, "is ", 3) || !strncmp(lower, "are ", 4)) e->intent = AI_IDENTITY;
    e->identity = 0;
    if (e->intent == AI_IDENTITY) (void)ai_relations(lower, &e->identity, 1);
    e->permission = !strncmp(lower, "can ", 4) || !strncmp(lower, "may ", 4);
    AiBag bag;
    ai_tokenize_ex(question, &bag, e, 1);
    e->query_count = bag.count < AI_QUERY ? bag.count : AI_QUERY;
    memcpy(e->query, bag.term, (size_t)e->query_count * sizeof(*e->query));
    e->query_pairs = bag.pairs < AI_QUERY ? bag.pairs : AI_QUERY;
    memcpy(e->query_pair, bag.pair, (size_t)e->query_pairs * sizeof(*e->query_pair));
    AiBag equipment;
    ai_tokenize("equipment", &equipment);
    if (ai_frequency(&bag, equipment.term[0])) e->intent = AI_EQUIPMENT;
    if (strstr(lower, "simpler") || strstr(lower, "simply") || strstr(lower, "easier") ||
        strstr(lower, "don't understand") || strstr(lower, "dont understand")) {
        e->intent = AI_SIMPLER;
        e->permission = 0;
        e->query_count = e->subject_count;
        memcpy(e->query, e->subject, (size_t)e->query_count * sizeof(*e->query));
        e->query_pairs = 0;
    }
    memset(e->df, 0, sizeof(e->df));
    snprintf(e->question, sizeof(e->question), "%s", question);
    e->use_context = e->category[0] && (e->query_count <= 2 || e->intent == AI_SIMPLER);
    e->cursor = 0;
    e->hit_count = e->result_count = 0;
    e->phase = e->query_count && e->count ? AI_DF : AI_READY;
    if (e->phase == AI_READY) e->subject_count = 0;
}
static void ai_category(AiEngine *e, int doc, char *out)
{
    const char *path = e->docs[doc].path + strlen(e->root);
    if (*path == '/') ++path;
    snprintf(out, AI_PATH, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    else out[0] = '\0';
}
static void ai_rank(AiEngine *e, int index)
{
    AiPassage *p = &e->passages[index];
    unsigned char f = p->features;
    if (p->heading) return;
    if (e->intent == AI_PROCEDURE && !(f & AI_F_STEPS)) return;
    if (e->intent == AI_CAUSE && !(f & AI_F_CAUSE)) return;
    if (e->intent == AI_SIMPLER && !(f & AI_F_SIMPLE)) return;
    if (e->intent == AI_EQUIPMENT && !(f & AI_F_EQUIPMENT)) return;
    /* Identity claims require an explicit relation; lexical overlap is insufficient. */
    if (e->intent == AI_IDENTITY) {
        int supports = 0;
        for (int i = 0; i < p->relations; ++i) supports |= e->identity && p->relation[i] == e->identity;
        if (!supports) return;
    }
    if (e->permission && !(f & AI_F_PERMISSION)) return;
    double score = 0;
    int matched = 0;
    double avg = e->average / (double)e->count;
    for (int i = 0; i < e->query_count; ++i) {
        int tf = ai_frequency(&p->bag, e->query[i]);
        if (!tf) continue;
        ++matched;
        double idf = log(1.0 + ((double)e->count - e->df[i] + 0.5) / (e->df[i] + 0.5));
        score += idf * tf * 2.2 / (tf + 1.2 * (0.25 + 0.75 * p->bag.length / avg));
    }
    /* Conservative coverage gate: context alone never creates a match. */
    if (!matched || (e->query_count <= 3 && matched != e->query_count) ||
        matched * 3 < e->query_count * 2) return;
    char category[AI_PATH];
    ai_category(e, p->doc, category);
    if (e->use_context && !strcmp(category, e->category)) score *= 1.25;
    /* Structural cues favor instructions, causes, locations and dates. */
    static const char *cues[] = {"", "definition", "person", "location", "date", "because", "step"};
    if (e->question_type) {
        AiBag cue;
        ai_tokenize(cues[e->question_type], &cue);
        if (cue.count && ai_frequency(&p->bag, cue.term[0])) score *= 1.2;
    }
    int pairs = 0;
    for (int q = 0; q < e->query_pairs; ++q) {
        for (int t = 0; t < p->bag.pairs; ++t) {
            if (e->query_pair[q] == p->bag.pair[t]) { ++pairs; break; }
        }
    }
    score *= 1.0 + 4.0 * pairs;
    if (e->intent == AI_DEFINITION && (f & AI_F_DEFINE)) score *= 4.0;
    if ((f & AI_F_SIMPLE) && e->intent != AI_SIMPLER) score *= 0.65;
    if (e->intent == AI_PROCEDURE && (f & AI_F_COMPLETE)) score *= 3.0;
    if (p->heading) score *= 0.15;
    int at = 0;
    while (at < e->hit_count) {
        AiHit *h = &e->hits[at];
        int tie = strcmp(e->docs[p->doc].path, e->docs[e->passages[h->passage].doc].path);
        if (score > h->score || (score == h->score && (tie < 0 ||
            (!tie && p->offset < e->passages[h->passage].offset)))) break;
        ++at;
    }
    int max = AI_RESULTS * 4;
    if (at >= max) return;
    if (e->hit_count < max) ++e->hit_count;
    for (int i = e->hit_count - 1; i > at; --i) e->hits[i] = e->hits[i - 1];
    e->hits[at] = (AiHit){index, score};
}
static int ai_same_file(FILE *file, const AiDoc *doc)
{
    struct stat st;
    return fstat(fileno(file), &st) == 0 && st.st_dev == doc->device &&
        st.st_ino == doc->inode && st.st_size == doc->size &&
        st.st_mtim.tv_sec == doc->modified.tv_sec && st.st_mtim.tv_nsec == doc->modified.tv_nsec;
}
static int ai_read_passage(AiEngine *e, int index, char *text)
{
    AiPassage *p = &e->passages[index];
    FILE *file = ai_open_file(e->docs[p->doc].path);
    if (!file) { ai_error(e, e->docs[p->doc].path, strerror(errno)); return 0; }
    int ok = ai_same_file(file, &e->docs[p->doc]) && fseek(file, p->offset, SEEK_SET) == 0 &&
        fread(text, 1, p->bytes, file) == p->bytes && ai_same_file(file, &e->docs[p->doc]);
    if (fclose(file)) ok = 0;
    text[p->bytes] = '\0';
    if (!ok || ai_hash(text, p->bytes) != p->checksum) {
        ai_error(e, e->docs[p->doc].path, "source changed or read failed; refresh knowledge");
        return 0;
    }
    return 1;
}
static void ai_finish(AiEngine *e)
{
    for (int i = 0; i < e->hit_count && e->result_count < AI_RESULTS; ++i) {
        int index = e->hits[i].passage;
        char text[AI_CHUNK + 1];
        if (e->hits[i].score < e->hits[0].score * 0.22 || !ai_read_passage(e, index, text)) continue;
        /* Compare normalized term sets as well as exact bytes for duplicates. */
        int duplicate = 0;
        AiBag *bag = &e->passages[index].bag;
        for (int j = 0; j < e->result_count; ++j) {
            AiBag *other = &e->passages[e->result[j]].bag;
            int common = 0;
            for (int t = 0; t < bag->count; ++t) common += ai_frequency(other, bag->term[t]) != 0;
            if (common * 10 >= 9 * bag->count && common * 10 >= 9 * other->count) duplicate = 1;
        }
        if (duplicate) continue;
        int r = e->result_count++;
        e->result[r] = index;
        memcpy(e->excerpts[r], text, strlen(text) + 1);
    }
    /* Keep complementary excerpts in source order within each document. */
    int priority[AI_DOCS];
    for (int i = 0; i < AI_DOCS; ++i) priority[i] = AI_RESULTS;
    for (int i = 0; i < e->result_count; ++i) {
        int doc = e->passages[e->result[i]].doc;
        if (priority[doc] == AI_RESULTS) priority[doc] = i;
    }
    for (int i = 1; e->intent == AI_PROCEDURE && i < e->result_count; ++i) {
        for (int j = i; j > 0; --j) {
            AiPassage *a = &e->passages[e->result[j - 1]], *b = &e->passages[e->result[j]];
            if (priority[a->doc] < priority[b->doc] ||
                (a->doc == b->doc && a->offset <= b->offset)) break;
            int index = e->result[j];
            e->result[j] = e->result[j - 1];
            e->result[j - 1] = index;
            char text[AI_CHUNK + 1];
            memcpy(text, e->excerpts[j], sizeof(text));
            memcpy(e->excerpts[j], e->excerpts[j - 1], sizeof(text));
            memcpy(e->excerpts[j - 1], text, sizeof(text));
        }
    }
    if (e->result_count) {
        ai_category(e, e->passages[e->result[0]].doc, e->category);
        e->subject_count = e->query_count;
        memcpy(e->subject, e->query, (size_t)e->subject_count * sizeof(*e->subject));
    } else e->subject_count = 0;
    e->phase = AI_READY;
}
static int ai_step(AiEngine *e)
{
    if (e->phase == AI_READY) return 0;
    if (e->phase == AI_INDEX) { ai_index_step(e); return 1; }
    size_t end = e->cursor + 32;
    if (end > e->count) end = e->count;
    for (; e->cursor < end; ++e->cursor) {
        if (e->phase == AI_DF) {
            for (int q = 0; q < e->query_count; ++q)
                e->df[q] += ai_frequency(&e->passages[e->cursor].bag, e->query[q]) != 0;
        } else ai_rank(e, (int)e->cursor);
    }
    if (e->cursor == e->count) {
        if (e->phase == AI_DF) { e->phase = AI_SCORE; e->cursor = 0; }
        else ai_finish(e);
    }
    return 1;
}
#endif
