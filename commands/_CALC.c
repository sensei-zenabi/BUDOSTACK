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

static double ldexp_wrapper(double value, double exponent);
static double scalbn_wrapper(double value, double exponent);
static double scalbln_wrapper(double value, double exponent);

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
    static const UnaryFunction unary_functions[] = {
        {"abs", fabs},
        {"acos", acos},
        {"acosh", acosh},
        {"asin", asin},
        {"asinh", asinh},
        {"atan", atan},
        {"atanh", atanh},
        {"cbrt", cbrt},
        {"ceil", ceil},
        {"cos", cos},
        {"cosh", cosh},
        {"erf", erf},
        {"erfc", erfc},
        {"exp", exp},
        {"exp2", exp2},
        {"expm1", expm1},
        {"fabs", fabs},
        {"floor", floor},
        {"gamma", tgamma},
        {"ln", log},
        {"lgamma", lgamma},
        {"log", log},
        {"log10", log10},
        {"log1p", log1p},
        {"log2", log2},
        {"tgamma", tgamma},
        {"round", round},
        {"sin", sin},
        {"sinh", sinh},
        {"sqrt", sqrt},
        {"tan", tan},
        {"tanh", tanh},
        {"trunc", trunc}
    };

    static const BinaryFunction binary_functions[] = {
        {"atan2", atan2},
        {"copysign", copysign},
        {"fdim", fdim},
        {"fmax", fmax},
        {"fmin", fmin},
        {"fmod", fmod},
        {"hypot", hypot},
        {"pow", pow},
        {"remainder", remainder}
    };

    static const BinaryFunction binary_wrappers[] = {
        {"ldexp", ldexp_wrapper},
        {"scalbn", scalbn_wrapper},
        {"scalbln", scalbln_wrapper}
    };

    static const TernaryFunction ternary_functions[] = {
        {"fma", fma}
    };

    static const MathConstant constants[] = {
        {"e", BUDOSTACK_E},
        {"inf", INFINITY},
        {"infinity", INFINITY},
        {"nan", NAN},
        {"pi", BUDOSTACK_PI},
        {"tau", 2.0 * BUDOSTACK_PI}
    };

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
    puts("Supports: +, -, *, /, ^, parentheses, unary +/-");
    puts("Functions: standard <math.h> functions including trig, hyperbolic, exponential, logarithmic, rounding, power, and fma");
    puts("Constants: e, pi, tau, inf, infinity, nan");
    puts("Example: _CALC (1*2 + 3) / 2 + 2^2 + sqrt(5) + sin(pi)");
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
