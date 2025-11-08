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

typedef struct {
    const char *name;
    double (*func)(double);
} UnaryFunction;

typedef struct {
    const char *name;
    double (*func)(double, double);
} BinaryFunction;

typedef struct {
    const char *name;
    double (*func)(double, double, double);
} TernaryFunction;

typedef struct {
    const char *name;
    double value;
} MathConstant;

static double ldexp_wrapper(double value, double exponent);
static double scalbn_wrapper(double value, double exponent);
static double scalbln_wrapper(double value, double exponent);

#define UNARY_FUNCTION_LIST                                                           \
    X(abs, fabs)                                                                       \
    X(acos, acos)                                                                      \
    X(acosh, acosh)                                                                    \
    X(asin, asin)                                                                      \
    X(asinh, asinh)                                                                    \
    X(atan, atan)                                                                      \
    X(atanh, atanh)                                                                    \
    X(cbrt, cbrt)                                                                      \
    X(ceil, ceil)                                                                      \
    X(cos, cos)                                                                        \
    X(cosh, cosh)                                                                      \
    X(erf, erf)                                                                        \
    X(erfc, erfc)                                                                      \
    X(exp, exp)                                                                        \
    X(exp2, exp2)                                                                      \
    X(expm1, expm1)                                                                    \
    X(fabs, fabs)                                                                      \
    X(floor, floor)                                                                    \
    X(gamma, tgamma)                                                                   \
    X(ln, log)                                                                         \
    X(lgamma, lgamma)                                                                  \
    X(log, log)                                                                        \
    X(log10, log10)                                                                    \
    X(log1p, log1p)                                                                    \
    X(log2, log2)                                                                      \
    X(tgamma, tgamma)                                                                  \
    X(round, round)                                                                    \
    X(sin, sin)                                                                        \
    X(sinh, sinh)                                                                      \
    X(sqrt, sqrt)                                                                      \
    X(tan, tan)                                                                        \
    X(tanh, tanh)                                                                      \
    X(trunc, trunc)

#define BINARY_FUNCTION_LIST                                                          \
    X(atan2, atan2)                                                                   \
    X(copysign, copysign)                                                             \
    X(fdim, fdim)                                                                     \
    X(fmax, fmax)                                                                     \
    X(fmin, fmin)                                                                     \
    X(fmod, fmod)                                                                     \
    X(hypot, hypot)                                                                   \
    X(pow, pow)                                                                       \
    X(remainder, remainder)

#define BINARY_WRAPPER_LIST                                                           \
    X(ldexp, ldexp_wrapper)                                                           \
    X(scalbn, scalbn_wrapper)                                                         \
    X(scalbln, scalbln_wrapper)

#define TERNARY_FUNCTION_LIST                                                         \
    X(fma, fma)

#define CONSTANT_LIST                                                                 \
    X(e, BUDOSTACK_E)                                                                 \
    X(inf, INFINITY)                                                                  \
    X(infinity, INFINITY)                                                             \
    X(nan, NAN)                                                                       \
    X(pi, BUDOSTACK_PI)                                                               \
    X(tau, 2.0 * BUDOSTACK_PI)

static const UnaryFunction unary_functions[] = {
#define X(name, func) {#name, func},
    UNARY_FUNCTION_LIST
#undef X
};

static const char *const unary_function_names[] = {
#define X(name, func) #name,
    UNARY_FUNCTION_LIST
#undef X
};

static const BinaryFunction binary_functions[] = {
#define X(name, func) {#name, func},
    BINARY_FUNCTION_LIST
#undef X
};

static const char *const binary_function_names[] = {
#define X(name, func) #name,
    BINARY_FUNCTION_LIST
#undef X
};

static const BinaryFunction binary_wrappers[] = {
#define X(name, func) {#name, func},
    BINARY_WRAPPER_LIST
#undef X
};

static const char *const binary_wrapper_names[] = {
#define X(name, func) #name,
    BINARY_WRAPPER_LIST
#undef X
};

static const TernaryFunction ternary_functions[] = {
#define X(name, func) {#name, func},
    TERNARY_FUNCTION_LIST
#undef X
};

static const char *const ternary_function_names[] = {
#define X(name, func) #name,
    TERNARY_FUNCTION_LIST
#undef X
};

static const MathConstant constants[] = {
#define X(name, value) {#name, value},
    CONSTANT_LIST
#undef X
};

static const char *const constant_names[] = {
#define X(name, value) #name,
    CONSTANT_LIST
#undef X
};

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
static void print_wrapped_list(const char *title, const char *const *items, size_t count);

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
        return pow(base, exponent);
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
        double *arguments = NULL;
        size_t count = 0;
        size_t capacity = 0;
        while (1) {
            skip_spaces(parser);
            if (match_char(parser, ')') != 0) {
                break;
            }
            double value = parse_expression(parser, error);
            if (*error != 0) {
                free(arguments);
                return 0.0;
            }
            if (count == capacity) {
                size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
                double *tmp = realloc(arguments, new_capacity * sizeof(*tmp));
                if (tmp == NULL) {
                    free(arguments);
                    fprintf(stderr, "memory allocation failure\n");
                    *error = 1;
                    return 0.0;
                }
                arguments = tmp;
                capacity = new_capacity;
            }
            arguments[count++] = value;
            skip_spaces(parser);
            if (match_char(parser, ')') != 0) {
                break;
            }
            if (match_char(parser, ',') == 0) {
                free(arguments);
                fprintf(stderr, "expected ',' in argument list for %s\n", name);
                *error = 1;
                return 0.0;
            }
        }
        double result = 0.0;
        int handled = 0;
        if (count == 1) {
            for (size_t i = 0; i < sizeof(unary_functions) / sizeof(unary_functions[0]); ++i) {
                if (strcmp(name, unary_functions[i].name) == 0) {
                    result = unary_functions[i].func(arguments[0]);
                    handled = 1;
                    break;
                }
            }
        } else if (count == 2) {
            for (size_t i = 0; i < sizeof(binary_functions) / sizeof(binary_functions[0]); ++i) {
                if (strcmp(name, binary_functions[i].name) == 0) {
                    result = binary_functions[i].func(arguments[0], arguments[1]);
                    handled = 1;
                    break;
                }
            }
            if (handled == 0) {
                for (size_t i = 0; i < sizeof(binary_wrappers) / sizeof(binary_wrappers[0]); ++i) {
                    if (strcmp(name, binary_wrappers[i].name) == 0) {
                        result = binary_wrappers[i].func(arguments[0], arguments[1]);
                        handled = 1;
                        break;
                    }
                }
            }
        } else if (count == 3) {
            for (size_t i = 0; i < sizeof(ternary_functions) / sizeof(ternary_functions[0]); ++i) {
                if (strcmp(name, ternary_functions[i].name) == 0) {
                    result = ternary_functions[i].func(arguments[0], arguments[1], arguments[2]);
                    handled = 1;
                    break;
                }
            }
        }
        free(arguments);
        if (handled != 0) {
            return result;
        }
        fprintf(stderr, "unknown function '%s' with %zu argument(s)\n", name, count);
        *error = 1;
        return 0.0;
    }
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i) {
        if (strcmp(name, constants[i].name) == 0) {
            return constants[i].value;
        }
    }
    fprintf(stderr, "unknown identifier '%s'\n", name);
    *error = 1;
    return 0.0;
}

static void print_help(void) {
    puts("BUDOSTACK Calculator");
    puts("Usage: _CALC <expression>");
    puts("");
    puts("Operators:");
    puts("    +, -, *, /, ^, parentheses, unary +/-");
    puts("");
    print_wrapped_list("Unary functions", unary_function_names,
                       sizeof(unary_function_names) / sizeof(unary_function_names[0]));
    print_wrapped_list("Binary functions", binary_function_names,
                       sizeof(binary_function_names) / sizeof(binary_function_names[0]));
    print_wrapped_list("Binary functions (integer exponent helpers)", binary_wrapper_names,
                       sizeof(binary_wrapper_names) / sizeof(binary_wrapper_names[0]));
    print_wrapped_list("Ternary functions", ternary_function_names,
                       sizeof(ternary_function_names) / sizeof(ternary_function_names[0]));
    print_wrapped_list("Constants", constant_names,
                       sizeof(constant_names) / sizeof(constant_names[0]));
    puts("");
    puts("Example: _CALC (1*2 + 3) / 2 + 2^2 + sqrt(5) + sin(pi)");
}

static void print_wrapped_list(const char *title, const char *const *items, size_t count) {
    if (count == 0) {
        return;
    }
    printf("%s:\n", title);
    for (size_t i = 0; i < count; ++i) {
        if (i % 6 == 0) {
            printf("    %s", items[i]);
        } else {
            printf(", %s", items[i]);
        }
    }
    putchar('\n');
    putchar('\n');
}

static double ldexp_wrapper(double value, double exponent) {
    return ldexp(value, (int)exponent);
}

static double scalbn_wrapper(double value, double exponent) {
    return scalbn(value, (int)exponent);
}

static double scalbln_wrapper(double value, double exponent) {
    return scalbln(value, (long)exponent);
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
