#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Provide strdup/strndup implementations for POSIX.1-2001 */
static char *strdup(const char *s) {
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len + 1);
    return d;
}

static char *strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

/* Trim leading/trailing whitespace in place */
static char *trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* --- Expression parser/evaluator setup --- */

typedef enum { TOK_END, TOK_NUM, TOK_IDENT, TOK_OP, TOK_LPAREN, TOK_RPAREN } TokenType;
typedef struct {
    TokenType type;
    double num;
    char *text;    /* for identifiers */
    char op[3];    /* for operators like &&, >=, etc. */
} Token;

static const char *filter_str;
static size_t filter_pos;
static Token curtok;

/* Advance to next token in filter_str */
static void next_token(void) {
    size_t i = filter_pos;
    while (filter_str[i] && isspace((unsigned char)filter_str[i])) i++;
    if (filter_str[i] == '\0') {
        curtok.type = TOK_END;
        filter_pos = i;
        return;
    }
    char c = filter_str[i];
    /* number? */
    if (isdigit((unsigned char)c) || c == '.') {
        char *endptr;
        errno = 0;
        double v = strtod(&filter_str[i], &endptr);
        if (errno) {
            fprintf(stderr, "Invalid number in filter\n");
            exit(EXIT_FAILURE);
        }
        curtok.type = TOK_NUM;
        curtok.num  = v;
        filter_pos  = endptr - filter_str;
        return;
    }
    /* identifier? */
    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = i;
        while (filter_str[i] &&
               (isalnum((unsigned char)filter_str[i]) || filter_str[i] == '_'))
            i++;
        size_t len = i - start;
        curtok.text = strndup(&filter_str[start], len);
        curtok.type = TOK_IDENT;
        filter_pos  = i;
        return;
    }
    /* two-char operators */
    if ((c == '&' && filter_str[i+1] == '&') ||
        (c == '|' && filter_str[i+1] == '|') ||
        (c == '=' && filter_str[i+1] == '=') ||
        (c == '!' && filter_str[i+1] == '=') ||
        (c == '>' && filter_str[i+1] == '=') ||
        (c == '<' && filter_str[i+1] == '=')) {
        curtok.type = TOK_OP;
        curtok.op[0] = filter_str[i];
        curtok.op[1] = filter_str[i+1];
        curtok.op[2] = '\0';
        filter_pos = i + 2;
        return;
    }
    /* single-char ops or parens */
    if (c == '>' || c == '<' || c == '!') {
        curtok.type = TOK_OP;
        curtok.op[0] = c;
        curtok.op[1] = '\0';
        filter_pos = i + 1;
        return;
    }
    if (c == '(') {
        curtok.type = TOK_LPAREN;
        filter_pos = i + 1;
        return;
    }
    if (c == ')') {
        curtok.type = TOK_RPAREN;
        filter_pos = i + 1;
        return;
    }
    fprintf(stderr, "Unknown character '%c' in filter expression\n", c);
    exit(EXIT_FAILURE);
}

/* AST node */
typedef enum { NODE_NUM, NODE_IDENT, NODE_OP } NodeType;
typedef struct AST {
    NodeType type;
    double num;       /* for NODE_NUM */
    char *ident;      /* for NODE_IDENT */
    char op[3];       /* for NODE_OP */
    struct AST *left, *right;
} AST;

/* Forward decls */
static AST *parse_expr(void);
static int eval_ast(AST *n, double *row, size_t ncols, char **headers);

/* Parse functions (recursive descent) */

static AST *parse_term(void) {
    if (curtok.type == TOK_NUM) {
        AST *n = malloc(sizeof *n);
        n->type = NODE_NUM;
        n->num  = curtok.num;
        n->ident = NULL;
        n->left = n->right = NULL;
        next_token();
        return n;
    }
    if (curtok.type == TOK_IDENT) {
        AST *n = malloc(sizeof *n);
        n->type = NODE_IDENT;
        n->ident = curtok.text;  /* owns strdup */
        n->left = n->right = NULL;
        next_token();
        return n;
    }
    if (curtok.type == TOK_LPAREN) {
        next_token();
        AST *n = parse_expr();
        if (curtok.type != TOK_RPAREN) {
            fprintf(stderr, "Expected ')' in filter\n");
            exit(EXIT_FAILURE);
        }
        next_token();
        return n;
    }
    fprintf(stderr, "Unexpected token in filter\n");
    exit(EXIT_FAILURE);
}

static AST *parse_cmp(void) {
    AST *n = parse_term();
    if (curtok.type == TOK_OP &&
        (strcmp(curtok.op, ">") == 0  ||
         strcmp(curtok.op, "<") == 0  ||
         strcmp(curtok.op, ">=") == 0 ||
         strcmp(curtok.op, "<=") == 0 ||
         strcmp(curtok.op, "==") == 0 ||
         strcmp(curtok.op, "!=") == 0)) {
        AST *r = malloc(sizeof *r);
        r->type = NODE_OP;
        strcpy(r->op, curtok.op);
        r->left  = n;
        next_token();
        r->right = parse_term();
        return r;
    }
    return n;
}

static AST *parse_and(void) {
    AST *n = parse_cmp();
    while (curtok.type == TOK_OP && strcmp(curtok.op, "&&") == 0) {
        AST *r = malloc(sizeof *r);
        r->type = NODE_OP;
        strcpy(r->op, "&&");
        r->left  = n;
        next_token();
        r->right = parse_cmp();
        n = r;
    }
    return n;
}

static AST *parse_expr(void) {
    AST *n = parse_and();
    while (curtok.type == TOK_OP && strcmp(curtok.op, "||") == 0) {
        AST *r = malloc(sizeof *r);
        r->type = NODE_OP;
        strcpy(r->op, "||");
        r->left  = n;
        next_token();
        r->right = parse_and();
        n = r;
    }
    return n;
}

/* Evaluate AST against one row (row[i] holds column i) */
static double get_ident_value(const char *id, double *row, size_t ncols, char **headers) {
    if (strncmp(id, "col", 3) == 0) {
        int idx = atoi(id + 3) - 1;
        if (idx < 0 || (size_t)idx >= ncols) {
            fprintf(stderr, "Column index out of range: %s\n", id);
            exit(EXIT_FAILURE);
        }
        return row[idx];
    }
    if (headers) {
        for (size_t i = 0; i < ncols; i++) {
            if (strcmp(headers[i], id) == 0)
                return row[i];
        }
        fprintf(stderr, "Unknown column name: %s\n", id);
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "No header, unknown identifier: %s\n", id);
    exit(EXIT_FAILURE);
}

static int eval_ast(AST *n, double *row, size_t ncols, char **headers) {
    switch (n->type) {
    case NODE_NUM:
        return n->num != 0.0;
    case NODE_IDENT: {
        double v = get_ident_value(n->ident, row, ncols, headers);
        return v != 0.0;
    }
    case NODE_OP: {
        if (strcmp(n->op, "&&") == 0)
            return eval_ast(n->left, row,ncols,headers) &&
                   eval_ast(n->right,row,ncols,headers);
        if (strcmp(n->op, "||") == 0)
            return eval_ast(n->left, row,ncols,headers) ||
                   eval_ast(n->right,row,ncols,headers);
        double l = eval_ast(n->left,  row,ncols,headers);
        double r = eval_ast(n->right, row,ncols,headers);
        if (strcmp(n->op, ">")  == 0) return l > r;
        if (strcmp(n->op, "<")  == 0) return l < r;
        if (strcmp(n->op, ">=") == 0) return l >= r;
        if (strcmp(n->op, "<=") == 0) return l <= r;
        if (strcmp(n->op, "==") == 0) return l == r;
        if (strcmp(n->op, "!=") == 0) return l != r;
    }
    /* fall through */
    default:
        fprintf(stderr, "Invalid operator in AST\n");
        exit(EXIT_FAILURE);
    }
}

/* Free AST */
static void free_ast(AST *n) {
    if (!n) return;
    free_ast(n->left);
    free_ast(n->right);
    if (n->type == NODE_IDENT) free(n->ident);
    free(n);
}

/* Print usage */
static void print_usage(void) {
    fprintf(stderr,
        "Usage: csvfilter <filter_expr> <input.csv> [output.csv]\n"
        "Example: csvfilter \"col1 > 2\" data.csv out.csv\n"
    );
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage();
        return EXIT_FAILURE;
    }

    /* copy and strip any quote characters */
    char *expr = strdup(argv[1]);
    if (!expr) { perror("strdup"); return EXIT_FAILURE; }
    char *p = expr, *q = expr;
    while (*p) {
        if (*p != '"') *q++ = *p;
        p++;
    }
    *q = '\0';
    filter_str = expr;
    filter_pos = 0;

    /* parse the filter */
    next_token();
    AST *root = parse_expr();
    if (curtok.type != TOK_END) {
        fprintf(stderr, "Unexpected input after filter\n");
        return EXIT_FAILURE;
    }

    /* open files */
    const char *infile  = argv[2];
    const char *outfile = (argc > 3 ? argv[3] : NULL);
    FILE *fin = fopen(infile, "r");
    if (!fin) { perror("fopen input"); return EXIT_FAILURE; }
    FILE *fout = outfile ? fopen(outfile, "w") : stdout;
    if (outfile && !fout) { perror("fopen output"); fclose(fin); return EXIT_FAILURE; }

    char line[16384];
    char *header_line = NULL;
    char **headers = NULL;
    size_t ncols = 0;
    int header_done = 0;

    /* process lines exactly as before… */
    while (fgets(line, sizeof(line), fin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        if (!header_done) {
            /* detect header */
            ncols = 1;
            for (char *t = line; *t; t++) if (*t == ';') ncols++;
            char *tmp = strdup(line);
            int any_nonnum = 0;
            char *save = NULL, *tok = strtok_r(tmp, ";", &save);
            while (tok) {
                char *t2 = trim(tok);
                char *endptr;
                errno = 0;
                strtod(t2, &endptr);
                if (errno || *endptr != '\0') {
                    any_nonnum = 1;
                    break;
                }
                tok = strtok_r(NULL, ";", &save);
            }
            free(tmp);

            if (any_nonnum) {
                header_done = 1;
                header_line = strdup(line);
                headers = calloc(ncols, sizeof(char*));
                size_t i = 0;
                save = NULL;
                tmp  = strdup(line);
                tok  = strtok_r(tmp, ";", &save);
                while (tok && i < ncols) {
                    headers[i++] = strdup(trim(tok));
                    tok = strtok_r(NULL, ";", &save);
                }
                free(tmp);
                fputs(header_line, fout);
                fputc('\n', fout);
                continue;
            } else {
                header_done = 1;
            }
        }

        /* split, convert, eval, and print… exactly as before */
        double *row = malloc(ncols * sizeof(double));
        int bad = 0;
        char *tmp = strdup(line);
        char *save = NULL, *tok = strtok_r(tmp, ";", &save);
        size_t i = 0;
        while (tok && i < ncols) {
            char *t2 = trim(tok);
            char *endptr;
            errno = 0;
            double v = strtod(t2, &endptr);
            if (errno || *endptr != '\0') { bad = 1; break; }
            row[i++] = v;
            tok = strtok_r(NULL, ";", &save);
        }
        free(tmp);
        if (bad || i != ncols) { free(row); continue; }

        if (eval_ast(root, row, ncols, headers)) {
            fputs(line, fout);
            fputc('\n', fout);
        }
        free(row);
    }

    /* cleanup */
    fclose(fin);
    if (outfile) fclose(fout);
    free_ast(root);
    if (header_line) {
        free(header_line);
        for (size_t i = 0; i < ncols; i++) free(headers[i]);
        free(headers);
    }
    return EXIT_SUCCESS;
}
