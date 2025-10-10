#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BUDOSTACK_PI
#define BUDOSTACK_PI 3.14159265358979323846264338327950288
#endif

#ifndef BUDOSTACK_E
#define BUDOSTACK_E 2.71828182845904523536028747135266250
#endif

typedef struct {
    const char *input;
    size_t pos;
} Parser;

static void skip_spaces(Parser *parser);
static int peek_char(const Parser *parser);
static int match_char(Parser *parser, char expected);
static double parse_expression(Parser *parser, int *error);
static double parse_term(Parser *parser, int *error);
static double parse_power(Parser *parser, int *error);
static double parse_unary(Parser *parser, int *error);
static double parse_primary(Parser *parser, int *error);
static double parse_function(Parser *parser, const char *name, int *error);
static void print_help(void);

static void skip_spaces(Parser *parser) {
    while (parser->input[parser->pos] != '\0' &&
           isspace((unsigned char)parser->input[parser->pos]) != 0) {
        parser->pos++;
    }
}

static int peek_char(const Parser *parser) {
    return parser->input[parser->pos];
}

static int match_char(Parser *parser, char expected) {
    if (peek_char(parser) == expected) {
        parser->pos++;
        return 1;
    }
    return 0;
}

static double parse_expression(Parser *parser, int *error) {
    double value = parse_term(parser, error);
    while (*error == 0) {
        skip_spaces(parser);
        char op = (char)peek_char(parser);
        if (op == '+' || op == '-') {
            parser->pos++;
            double rhs = parse_term(parser, error);
            if (*error != 0) {
                return 0.0;
            }
            if (op == '+') {
                value += rhs;
            } else {
                value -= rhs;
            }
        } else {
            break;
        }
    }
    return value;
}

static double parse_term(Parser *parser, int *error) {
    double value = parse_power(parser, error);
    while (*error == 0) {
        skip_spaces(parser);
        char op = (char)peek_char(parser);
        if (op == '*' || op == '/') {
            parser->pos++;
            double rhs = parse_power(parser, error);
            if (*error != 0) {
                return 0.0;
            }
            if (op == '*') {
                value *= rhs;
            } else {
                if (rhs == 0.0) {
                    fprintf(stderr, "division by zero\n");
                    *error = 1;
                    return 0.0;
                }
                value /= rhs;
            }
        } else {
            break;
        }
    }
    return value;
}

static double parse_power(Parser *parser, int *error) {
    double base = parse_unary(parser, error);
    if (*error != 0) {
        return 0.0;
    }
    skip_spaces(parser);
    if (match_char(parser, '^') != 0) {
        double exponent = parse_power(parser, error);
        if (*error != 0) {
            return 0.0;
        }
        errno = 0;
        double result = pow(base, exponent);
        if (errno != 0 || isfinite(result) == 0) {
            fprintf(stderr, "invalid exponentiation\n");
            *error = 1;
            return 0.0;
        }
        return result;
    }
    return base;
}

static double parse_unary(Parser *parser, int *error) {
    skip_spaces(parser);
    char op = (char)peek_char(parser);
    if (op == '+') {
        parser->pos++;
        return parse_unary(parser, error);
    }
    if (op == '-') {
        parser->pos++;
        double value = parse_unary(parser, error);
        return -value;
    }
    return parse_primary(parser, error);
}

static double parse_primary(Parser *parser, int *error) {
    skip_spaces(parser);
    char current = (char)peek_char(parser);
    if (current == '(') {
        parser->pos++;
        double value = parse_expression(parser, error);
        if (*error != 0) {
            return 0.0;
        }
        skip_spaces(parser);
        if (match_char(parser, ')') == 0) {
            fprintf(stderr, "missing closing parenthesis\n");
            *error = 1;
            return 0.0;
        }
        return value;
    }
    if (isdigit((unsigned char)current) != 0 || current == '.') {
        const char *start = parser->input + parser->pos;
        errno = 0;
        char *end = NULL;
        double value = strtod(start, &end);
        if (end == start || errno != 0) {
            fprintf(stderr, "invalid number near position %zu\n", parser->pos + 1);
            *error = 1;
            return 0.0;
        }
        parser->pos = (size_t)(end - parser->input);
        return value;
    }
    if (isalpha((unsigned char)current) != 0) {
        size_t start = parser->pos;
        while (isalnum((unsigned char)parser->input[parser->pos]) != 0) {
            parser->pos++;
        }
        size_t length = parser->pos - start;
        char *name = malloc(length + 1);
        if (name == NULL) {
            fprintf(stderr, "memory allocation failure\n");
            *error = 1;
            return 0.0;
        }
        memcpy(name, parser->input + start, length);
        name[length] = '\0';
        double value = parse_function(parser, name, error);
        free(name);
        return value;
    }
    if (current == '\0') {
        fprintf(stderr, "unexpected end of input\n");
    } else {
        fprintf(stderr, "unexpected character '%c' at position %zu\n", current, parser->pos + 1);
    }
    *error = 1;
    return 0.0;
}

static double parse_function(Parser *parser, const char *name, int *error) {
    skip_spaces(parser);
    if (match_char(parser, '(') != 0) {
        double argument = parse_expression(parser, error);
        if (*error != 0) {
            return 0.0;
        }
        skip_spaces(parser);
        if (match_char(parser, ')') == 0) {
            fprintf(stderr, "missing closing parenthesis for %s\n", name);
            *error = 1;
            return 0.0;
        }
        if (strcmp(name, "sqrt") == 0) {
            if (argument < 0.0) {
                fprintf(stderr, "sqrt domain error\n");
                *error = 1;
                return 0.0;
            }
            return sqrt(argument);
        }
        if (strcmp(name, "sin") == 0) {
            return sin(argument);
        }
        if (strcmp(name, "cos") == 0) {
            return cos(argument);
        }
        if (strcmp(name, "tan") == 0) {
            return tan(argument);
        }
        if (strcmp(name, "asin") == 0) {
            if (argument < -1.0 || argument > 1.0) {
                fprintf(stderr, "asin domain error\n");
                *error = 1;
                return 0.0;
            }
            return asin(argument);
        }
        if (strcmp(name, "acos") == 0) {
            if (argument < -1.0 || argument > 1.0) {
                fprintf(stderr, "acos domain error\n");
                *error = 1;
                return 0.0;
            }
            return acos(argument);
        }
        if (strcmp(name, "atan") == 0) {
            return atan(argument);
        }
        if (strcmp(name, "ln") == 0 || strcmp(name, "log") == 0) {
            if (argument <= 0.0) {
                fprintf(stderr, "log domain error\n");
                *error = 1;
                return 0.0;
            }
            return log(argument);
        }
        if (strcmp(name, "log10") == 0) {
            if (argument <= 0.0) {
                fprintf(stderr, "log10 domain error\n");
                *error = 1;
                return 0.0;
            }
            return log10(argument);
        }
        if (strcmp(name, "exp") == 0) {
            return exp(argument);
        }
        if (strcmp(name, "abs") == 0) {
            return fabs(argument);
        }
        if (strcmp(name, "ceil") == 0) {
            return ceil(argument);
        }
        if (strcmp(name, "floor") == 0) {
            return floor(argument);
        }
        fprintf(stderr, "unknown function '%s'\n", name);
        *error = 1;
        return 0.0;
    }
    if (strcmp(name, "pi") == 0) {
        return BUDOSTACK_PI;
    }
    if (strcmp(name, "e") == 0) {
        return BUDOSTACK_E;
    }
    fprintf(stderr, "unknown identifier '%s'\n", name);
    *error = 1;
    return 0.0;
}

static void print_help(void) {
    puts("BUDOSTACK Calculator");
    puts("Usage: _CALC <expression>");
    puts("Supports: +, -, *, /, ^, parentheses, unary +/-");
    puts("Functions: sqrt, sin, cos, tan, asin, acos, atan, ln/log, log10, exp, abs, ceil, floor");
    puts("Constants: pi, e");
    puts("Example: _CALC (1*2 + 3) / 2 + 2^2 + sqrt(5) + sin(pi)");
}

int main(int argc, char **argv) {
    if (argc == 1) {
        print_help();
        return 0;
    }
    size_t length = 0;
    for (int i = 1; i < argc; ++i) {
        length += strlen(argv[i]);
        if (i + 1 < argc) {
            length += 1;
        }
    }
    char *input = malloc(length + 1);
    if (input == NULL) {
        fprintf(stderr, "memory allocation failure\n");
        return 1;
    }
    size_t offset = 0;
    for (int i = 1; i < argc; ++i) {
        size_t arg_len = strlen(argv[i]);
        memcpy(input + offset, argv[i], arg_len);
        offset += arg_len;
        if (i + 1 < argc) {
            input[offset] = ' ';
            offset++;
        }
    }
    input[offset] = '\0';

    Parser parser = {input, 0};
    int error = 0;
    double result = parse_expression(&parser, &error);
    if (error == 0) {
        skip_spaces(&parser);
        if (peek_char(&parser) != '\0') {
            fprintf(stderr, "unexpected trailing characters near position %zu\n", parser.pos + 1);
            error = 1;
        }
    }
    if (error != 0) {
        free(input);
        return 1;
    }
    printf("%.15g\n", result);
    free(input);
    return 0;
}
