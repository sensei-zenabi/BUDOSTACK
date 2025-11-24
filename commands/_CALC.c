#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <float.h>
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
    X(abs, "abs", fabs)                                                               \
    X(acos, "acos", acos)                                                             \
    X(acosh, "acosh", acosh)                                                          \
    X(asin, "asin", asin)                                                             \
    X(asinh, "asinh", asinh)                                                          \
    X(atan, "atan", atan)                                                             \
    X(atanh, "atanh", atanh)                                                          \
    X(cbrt, "cbrt", cbrt)                                                             \
    X(ceil, "ceil(x[, digits])", ceil)                                                \
    X(cos, "cos", cos)                                                                \
    X(cosh, "cosh", cosh)                                                             \
    X(erf, "erf", erf)                                                                \
    X(erfc, "erfc", erfc)                                                             \
    X(exp, "exp", exp)                                                                \
    X(exp2, "exp2", exp2)                                                             \
    X(expm1, "expm1", expm1)                                                          \
    X(fabs, "fabs", fabs)                                                             \
    X(floor, "floor(x[, digits])", floor)                                             \
    X(gamma, "gamma", tgamma)                                                         \
    X(ln, "ln(x[, base])", log)                                                       \
    X(lgamma, "lgamma", lgamma)                                                       \
    X(log, "log(x[, base])", log)                                                     \
    X(log10, "log10", log10)                                                          \
    X(log1p, "log1p", log1p)                                                          \
    X(log2, "log2", log2)                                                             \
    X(tgamma, "tgamma", tgamma)                                                       \
    X(round, "round(x[, digits])", round)                                             \
    X(sin, "sin", sin)                                                                \
    X(sinh, "sinh", sinh)                                                             \
    X(sqrt, "sqrt", sqrt)                                                             \
    X(tan, "tan", tan)                                                                \
    X(tanh, "tanh", tanh)                                                             \
    X(trunc, "trunc(x[, digits])", trunc)

#define BINARY_FUNCTION_LIST                                                          \
    X(atan2, "atan2(x, y)", atan2)                                                   \
    X(copysign, "copysign(x, y)", copysign)                                         \
    X(fdim, "fdim(x, y)", fdim)                                                     \
    X(fmax, "fmax(x, y[, ...])", fmax)                                              \
    X(fmin, "fmin(x, y[, ...])", fmin)                                              \
    X(fmod, "fmod(x, y)", fmod)                                                     \
    X(hypot, "hypot(x, y[, ...])", hypot)                                           \
    X(pow, "pow(x, y)", pow)                                                         \
    X(remainder, "remainder(x, y)", remainder)

#define BINARY_WRAPPER_LIST                                                           \
    X(ldexp, "ldexp(x, exp)", ldexp_wrapper)                                         \
    X(scalbn, "scalbn(x, exp)", scalbn_wrapper)                                     \
    X(scalbln, "scalbln(x, exp)", scalbln_wrapper)

#define TERNARY_FUNCTION_LIST                                                         \
    X(fma, "fma(x, y, z)", fma)

#define CONSTANT_LIST                                                                 \
    X(e, "e", BUDOSTACK_E)                                                           \
    X(inf, "inf", INFINITY)                                                          \
    X(infinity, "infinity", INFINITY)                                                \
    X(nan, "nan", NAN)                                                               \
    X(pi, "pi", BUDOSTACK_PI)                                                        \
    X(tau, "tau", 2.0 * BUDOSTACK_PI)

static const UnaryFunction unary_functions[] = {
#define X(name, display, func) {#name, func},
    UNARY_FUNCTION_LIST
#undef X
};

static const char *const unary_function_displays[] = {
#define X(name, display, func) display,
    UNARY_FUNCTION_LIST
#undef X
};

static const BinaryFunction binary_functions[] = {
#define X(name, display, func) {#name, func},
    BINARY_FUNCTION_LIST
#undef X
};

static const char *const binary_function_displays[] = {
#define X(name, display, func) display,
    BINARY_FUNCTION_LIST
#undef X
};

static const BinaryFunction binary_wrappers[] = {
#define X(name, display, func) {#name, func},
    BINARY_WRAPPER_LIST
#undef X
};

static const char *const binary_wrapper_displays[] = {
#define X(name, display, func) display,
    BINARY_WRAPPER_LIST
#undef X
};

static const TernaryFunction ternary_functions[] = {
#define X(name, display, func) {#name, func},
    TERNARY_FUNCTION_LIST
#undef X
};

static const char *const ternary_function_displays[] = {
#define X(name, display, func) display,
    TERNARY_FUNCTION_LIST
#undef X
};

static const MathConstant constants[] = {
#define X(name, display, value) {#name, value},
    CONSTANT_LIST
#undef X
};

static const char *const constant_displays[] = {
#define X(name, display, value) display,
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
static int evaluate_extended_function(const char *name, const double *arguments, size_t count,
                                      double *result, int *error);
static int handle_precision_function(const char *name, const double *arguments, size_t count,
                                     double *result, int *error);
static double apply_precision_modifier(const char *func_name, double value, double digits,
                                       double (*rounder)(double), int *error);
static int convert_precision_argument(const char *func_name, double digits, long *precision,
                                      int *error);

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
        if (op == '*' || op == 'x' || op == 'X' || op == '/') {
            parser->pos++;
            double rhs = parse_power(parser, error);
            if (*error != 0) {
                return 0.0;
            }
            if (op == '/') {
                value /= rhs;
            } else {
                value *= rhs;
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
        if (handled == 0) {
            handled = evaluate_extended_function(name, arguments, count, &result, error);
        }
        free(arguments);
        if (handled != 0) {
            if (*error == 0) {
                return result;
            }
            return 0.0;
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
    puts("    +, -, *, x, /, ^, parentheses, unary +/-");
    puts("");
    print_wrapped_list("Unary functions", unary_function_displays,
                       sizeof(unary_function_displays) / sizeof(unary_function_displays[0]));
    print_wrapped_list("Binary functions", binary_function_displays,
                       sizeof(binary_function_displays) / sizeof(binary_function_displays[0]));
    print_wrapped_list("Binary functions (integer exponent helpers)", binary_wrapper_displays,
                       sizeof(binary_wrapper_displays) / sizeof(binary_wrapper_displays[0]));
    print_wrapped_list("Ternary functions", ternary_function_displays,
                       sizeof(ternary_function_displays) / sizeof(ternary_function_displays[0]));
    print_wrapped_list("Constants", constant_displays,
                       sizeof(constant_displays) / sizeof(constant_displays[0]));
    puts("Notes:");
    puts("    - round, ceil, floor, and trunc accept an optional second argument for decimal digits.");
    puts("    - log and ln accept an optional base argument.");
    puts("    - fmin, fmax, and hypot accept two or more arguments.");
    puts("");
    puts("Example: _CALC (1*2 + 3) / 2 + 2^2 + sqrt(5) + sin(pi)");
    puts("Tip: use 'x' as a multiplication operator if your shell expands '*'.");
}

static void print_wrapped_list(const char *title, const char *const *items, size_t count) {
    if (count == 0) {
        return;
    }
    printf("%s:\n", title);
    const size_t wrap_width = 80;
    size_t line_length = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *name = items[i];
        size_t name_length = strlen(name);
        size_t separator_length = (line_length == 0) ? 4 : 2;
        size_t projected_length = (line_length == 0) ? separator_length + name_length
                                                     : line_length + separator_length + name_length;
        if (line_length != 0 && projected_length > wrap_width) {
            putchar('\n');
            line_length = 0;
            separator_length = 4;
            projected_length = separator_length + name_length;
        }
        if (line_length == 0) {
            printf("    %s", name);
            line_length = separator_length + name_length;
        } else {
            printf(", %s", name);
            line_length = projected_length;
        }
    }
    putchar('\n');
    putchar('\n');
}

static int evaluate_extended_function(const char *name, const double *arguments, size_t count,
                                      double *result, int *error) {
    *result = 0.0;
    if (handle_precision_function(name, arguments, count, result, error) != 0) {
        return 1;
    }
    if ((strcmp(name, "log") == 0 || strcmp(name, "ln") == 0) && count != 1) {
        if (count == 2) {
            double base = arguments[1];
            if (!isfinite(base) || base <= 0.0 || fabs(base - 1.0) <= DBL_EPSILON) {
                fprintf(stderr, "%s base must be positive and not equal to 1\n", name);
                *error = 1;
            } else {
                double denominator = log(base);
                if (denominator == 0.0) {
                    fprintf(stderr, "%s base results in undefined logarithm\n", name);
                    *error = 1;
                } else {
                    *result = log(arguments[0]) / denominator;
                }
            }
        } else {
            fprintf(stderr, "%s expects one or two arguments\n", name);
            *error = 1;
        }
        return 1;
    }
    if (strcmp(name, "fmax") == 0 || strcmp(name, "fmin") == 0) {
        if (count < 2) {
            fprintf(stderr, "%s requires at least two arguments\n", name);
            *error = 1;
        } else {
            double value = arguments[0];
            if (strcmp(name, "fmax") == 0) {
                for (size_t i = 1; i < count; ++i) {
                    value = fmax(value, arguments[i]);
                }
            } else {
                for (size_t i = 1; i < count; ++i) {
                    value = fmin(value, arguments[i]);
                }
            }
            *result = value;
        }
        return 1;
    }
    if (strcmp(name, "hypot") == 0) {
        if (count < 2) {
            fprintf(stderr, "hypot requires at least two arguments\n");
            *error = 1;
        } else {
            double value = arguments[0];
            for (size_t i = 1; i < count; ++i) {
                value = hypot(value, arguments[i]);
            }
            *result = value;
        }
        return 1;
    }
    return 0;
}

static int handle_precision_function(const char *name, const double *arguments, size_t count,
                                     double *result, int *error) {
    double (*rounder)(double) = NULL;
    if (strcmp(name, "round") == 0) {
        rounder = round;
    } else if (strcmp(name, "ceil") == 0) {
        rounder = ceil;
    } else if (strcmp(name, "floor") == 0) {
        rounder = floor;
    } else if (strcmp(name, "trunc") == 0) {
        rounder = trunc;
    } else {
        return 0;
    }
    if (count == 1) {
        return 0;
    }
    if (count == 2) {
        *result = apply_precision_modifier(name, arguments[0], arguments[1], rounder, error);
    } else {
        fprintf(stderr, "%s expects one or two arguments\n", name);
        *error = 1;
    }
    return 1;
}

static double apply_precision_modifier(const char *func_name, double value, double digits,
                                       double (*rounder)(double), int *error) {
    long precision = 0;
    if (convert_precision_argument(func_name, digits, &precision, error) == 0) {
        return 0.0;
    }
    double scale = pow(10.0, (double)precision);
    if (!isfinite(scale) || scale == 0.0) {
        fprintf(stderr, "%s precision produces an invalid scaling factor\n", func_name);
        *error = 1;
        return 0.0;
    }
    double scaled = value * scale;
    double rounded = rounder(scaled);
    return rounded / scale;
}

static int convert_precision_argument(const char *func_name, double digits, long *precision,
                                      int *error) {
    if (!isfinite(digits)) {
        fprintf(stderr, "%s precision must be finite\n", func_name);
        *error = 1;
        return 0;
    }
    double integral = 0.0;
    double fractional = modf(digits, &integral);
    if (fabs(fractional) > DBL_EPSILON) {
        fprintf(stderr, "%s precision must be an integer value\n", func_name);
        *error = 1;
        return 0;
    }
    const long max_precision = DBL_MAX_10_EXP;
    const long min_precision = -DBL_MAX_10_EXP;
    if (integral > (double)max_precision || integral < (double)min_precision) {
        fprintf(stderr, "%s precision must be between %ld and %ld\n", func_name, min_precision,
                max_precision);
        *error = 1;
        return 0;
    }
    *precision = (long)integral;
    return 1;
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
