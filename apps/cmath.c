/*
 * cmath.c - A basic command-line math interpreter with variable handling.
 *
 * Design Principles:
 * - Implements a REPL (read-eval-print loop) for scalar arithmetic.
 * - Supports variable assignment and usage.
 * - Uses a recursive descent parser that endures arbitrary whitespace between characters and numbers.
 * - Maximizes coverage of common math operations useful for engineering by including a broad set of functions.
 * - All code is written in plain C (compiled with -std=c11) using standard POSIX libraries.
 *
 * To compile:
 *     gcc -std=c11 -o cmath cmath.c -lm
 *
 * To run interactively:
 *     ./cmath
 *
 * To run a macro file (e.g., mymacro.m):
 *     ./cmath mymacro.m
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ================= Basic Scalar Mode (Arithmetic) ================= */

#define MAX_VARS 100

struct Variable {
    char name[32];
    double value;
};

struct Variable variables[MAX_VARS];
int var_count = 0;

/* Global pointer for input string parsing */
const char *p;
/* Global error flag */
int error_flag = 0;

/* Function declarations */
void skip_whitespace(void);
double parse_expression(void);
double parse_term(void);
double parse_factor(void);
double parse_primary(void);
double call_function(const char *func, double arg);
double *get_variable(const char *name);
void set_variable(const char *name, double value);
void list_variables(void);
void print_help(void);

/* Skip spaces and tabs */
void skip_whitespace(void) {
    while (*p == ' ' || *p == '\t')
        p++;
}

/* Lookup a variable by name */
double *get_variable(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0)
            return &variables[i].value;
    }
    return NULL;
}

/* Create or update a variable */
void set_variable(const char *name, double value) {
    double *var = get_variable(name);
    if (var != NULL) {
        *var = value;
        return;
    }
    if (var_count < MAX_VARS) {
        strncpy(variables[var_count].name, name, sizeof(variables[var_count].name) - 1);
        variables[var_count].name[sizeof(variables[var_count].name) - 1] = '\0';
        variables[var_count].value = value;
        var_count++;
    } else {
        printf("Error: Variable limit reached\n");
    }
}

/* List all stored variables */
void list_variables(void) {
    if (var_count == 0) {
        printf("No variables stored.\n");
        return;
    }
    printf("Stored variables:\n");
    for (int i = 0; i < var_count; i++) {
        printf("  %s = %g\n", variables[i].name, variables[i].value);
    }
}

/*
 * parse_expression:
 *   expression -> term { ('+' | '-') term }
 */
double parse_expression(void) {
    double value = parse_term();
    skip_whitespace();
    while (*p == '+' || *p == '-') {
        char op = *p;
        p++;
        skip_whitespace();
        double term = parse_term();
        if (op == '+')
            value += term;
        else
            value -= term;
        skip_whitespace();
    }
    return value;
}

/*
 * parse_term:
 *   term -> factor { ('*' | '/') factor }
 */
double parse_term(void) {
    double value = parse_factor();
    skip_whitespace();
    while (*p == '*' || *p == '/') {
        char op = *p;
        p++;
        skip_whitespace();
        double factor = parse_factor();
        if (op == '*')
            value *= factor;
        else {
            if (factor == 0) {
                printf("Error: Division by zero\n");
                error_flag = 1;
                return 0;
            }
            value /= factor;
        }
        skip_whitespace();
    }
    return value;
}

/*
 * parse_factor:
 *   factor -> primary { '^' factor }
 */
double parse_factor(void) {
    double value = parse_primary();
    skip_whitespace();
    while (*p == '^') {
        p++; // skip '^'
        skip_whitespace();
        double exponent = parse_factor();
        value = pow(value, exponent);
        skip_whitespace();
    }
    return value;
}

/*
 * parse_primary:
 *   primary -> number | variable | function '(' expression ')' | '(' expression ')' | unary +/- primary
 */
double parse_primary(void) {
    skip_whitespace();
    double value = 0;
    if (*p == '(') {
        p++; // skip '('
        value = parse_expression();
        skip_whitespace();
        if (*p == ')') {
            p++; // skip ')'
        } else {
            printf("Error: Expected ')'\n");
            error_flag = 1;
        }
        return value;
    } else if (isdigit(*p) || *p == '.') {
        char *endptr;
        value = strtod(p, &endptr);
        p = endptr;
        return value;
    } else if (isalpha(*p)) {
        /* Parse an identifier (variable or function name) */
        char ident[32];
        int i = 0;
        while ((isalnum(*p) || *p == '_') && i < (int)(sizeof(ident) - 1)) {
            ident[i++] = *p;
            p++;
        }
        ident[i] = '\0';
        skip_whitespace();
        if (*p == '(') {
            /* Function call */
            p++;  // skip '('
            skip_whitespace();
            double arg = parse_expression();
            skip_whitespace();
            if (*p == ')') {
                p++; // skip ')'
            } else {
                printf("Error: Expected ')' after function argument\n");
                error_flag = 1;
                return 0;
            }
            return call_function(ident, arg);
        } else {
            /* Variable usage */
            double *var_val = get_variable(ident);
            if (var_val == NULL) {
                printf("Error: Unknown variable '%s'\n", ident);
                error_flag = 1;
                return 0;
            }
            return *var_val;
        }
    } else if (*p == '-') {
        p++;
        return -parse_primary();
    } else if (*p == '+') {
        p++;
        return parse_primary();
    } else {
        printf("Error: Unexpected character '%c'\n", *p);
        error_flag = 1;
        return 0;
    }
}

/*
 * call_function:
 *   Supports functions: sin, cos, tan, asin, acos, atan, log, log10, sqrt, exp, abs, sinh, cosh, tanh, floor, ceil.
 */
double call_function(const char *func, double arg) {
    if (strcmp(func, "sin") == 0)
        return sin(arg);
    if (strcmp(func, "cos") == 0)
        return cos(arg);
    if (strcmp(func, "tan") == 0)
        return tan(arg);
    if (strcmp(func, "asin") == 0)
        return asin(arg);
    if (strcmp(func, "acos") == 0)
        return acos(arg);
    if (strcmp(func, "atan") == 0)
        return atan(arg);
    if (strcmp(func, "log") == 0)
        return log(arg);
    if (strcmp(func, "log10") == 0)
        return log10(arg);
    if (strcmp(func, "sqrt") == 0)
        return sqrt(arg);
    if (strcmp(func, "exp") == 0)
        return exp(arg);
    if (strcmp(func, "abs") == 0)
        return fabs(arg);
    if (strcmp(func, "sinh") == 0)
        return sinh(arg);
    if (strcmp(func, "cosh") == 0)
        return cosh(arg);
    if (strcmp(func, "tanh") == 0)
        return tanh(arg);
    if (strcmp(func, "floor") == 0)
        return floor(arg);
    if (strcmp(func, "ceil") == 0)
        return ceil(arg);
    printf("Error: Unknown function '%s'\n", func);
    error_flag = 1;
    return 0;
}

/* Print help message with supported commands and functions */
void print_help(void) {
    printf("=== CMath Help Menu ===\n\n");
    printf("Supported Commands:\n");
    printf("  help          : Show this help menu\n");
    printf("  list          : List all stored variables\n");
    printf("  exit, quit    : Exit the math terminal\n\n");
    printf("Usage:\n");
    printf("  Enter arithmetic expressions to evaluate them (e.g., 2 + 3 * 4, sin(0.5)).\n");
    printf("  Assign variables using the syntax: variable = expression (e.g., x = 3.14).\n");
    printf("  Use stored variables in expressions (e.g., sin(x) + x^2).\n\n");
    printf("Supported Operations:\n");
    printf("  Addition:       +\n");
    printf("  Subtraction:    -\n");
    printf("  Multiplication: *\n");
    printf("  Division:       /\n");
    printf("  Exponentiation: ^\n\n");
    printf("Supported Functions:\n");
    printf("  sin, cos, tan, asin, acos, atan,\n");
    printf("  log (natural logarithm), log10 (base-10 logarithm),\n");
    printf("  sqrt, exp,\n");
    printf("  abs (absolute value),\n");
    printf("  sinh, cosh, tanh,\n");
    printf("  floor, ceil\n\n");
    printf("Examples:\n");
    printf("  2 + 3 * 4            -> Evaluates to 14\n");
    printf("  x = 3.14             -> Assigns 3.14 to variable x\n");
    printf("  sin(0.5) + x         -> Uses sine function and variable x\n");
    printf("  3 + 4 * 2 / (1 - 5)^2  -> Follows standard operator precedence\n");
}

/* ================= Main REPL Loop ================= */

int main(int argc, char *argv[]) {
    char line[256];
    int interactive = 1;

    /* If a macro file is provided, use it as input */
    if (argc > 1) {
        if (freopen(argv[1], "r", stdin) == NULL) {
            perror("Error opening macro file");
            exit(1);
        }
        interactive = 0;
    }

    printf("Welcome to CMath - Basic Math Interpreter with Variable Handling.\n");
    printf("Type 'help' for instructions, 'exit' or 'quit' to leave.\n");

    while (1) {
        if (interactive)
            printf("math> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
            break;
        if (strcmp(line, "help") == 0) {
            print_help();
            continue;
        }
        if (strcmp(line, "list") == 0) {
            list_variables();
            continue;
        }

        /* Process scalar arithmetic or assignment */
        p = line;
        skip_whitespace();
        char ident[32];
        const char *start = p;
        if (isalpha(*p)) {
            int i = 0;
            while ((isalnum(*p) || *p == '_') && i < (int)(sizeof(ident) - 1)) {
                ident[i++] = *p;
                p++;
            }
            ident[i] = '\0';
            skip_whitespace();
            if (*p == '=') {
                p++; // skip '='
                skip_whitespace();
                double result = parse_expression();
                if (error_flag) {
                    error_flag = 0;
                    continue;
                }
                set_variable(ident, result);
                printf("%s = %g\n", ident, result);
                continue;
            } else {
                /* Not an assignment; reset pointer to start */
                p = start;
            }
        }
        double result = parse_expression();
        if (error_flag) {
            error_flag = 0;
            continue;
        }
        printf("%g\n", result);
    }
    printf("Goodbye.\n");
    return 0;
}
