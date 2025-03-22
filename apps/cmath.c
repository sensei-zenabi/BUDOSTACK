/*
 * cmath.c - An extended command-line math interpreter.
 *
 * Design Principles:
 * - Implements a REPL (read-eval-print loop) that supports multiple modes:
 *     1. Scalar Mode: Basic arithmetic calculations and variable assignments.
 *     2. Matrix Mode: Basic linear algebra operations.
 *     3. Complex Mode: Complex arithmetic.
 *     4. DSOLVE Mode: Differential equation solving using Euler’s method.
 *     5. Symbolic Mode: Basic symbolic differentiation with respect to x.
 *
 * - Uses a recursive descent parser for scalar arithmetic.
 * - Each mode has its own set of commands; the "help" command provides detailed instructions.
 * - If a macro file is passed on the command line (e.g., "cmath mymacro.m"), the program
 *   reads commands from that file instead of interactive input.
 *
 * - All code is written in plain C (compiled with -std=c11) using only standard libraries.
 * - No header files are created; everything is contained in this single file.
 *
 * References:
 *   - Recursive descent parsing techniques (e.g., "Compilers: Principles, Techniques, and Tools")
 *   - Standard C library documentation for <math.h>, <ctype.h>, <string.h>, and <complex.h>
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
#include <complex.h>

/* ================= Scalar Mode (Basic Arithmetic) ================= */

/* --- Global Declarations for Scalar Mode --- */

#define MAX_VARS 100

struct Variable {
    char name[32];
    double value;
};

struct Variable variables[MAX_VARS];
int var_count = 0;

/* Pointer to current position in the input string during parsing */
const char *p;

/* Global error flag (set to non-zero on parsing/evaluation errors) */
int error_flag = 0;

/* Forward declarations for scalar mode parser functions */
void skip_whitespace(void);
double parse_expression(void);
double parse_term(void);
double parse_factor(void);
double parse_primary(void);
double call_function(const char *func, double arg);
double *get_variable(const char *name);
void set_variable(const char *name, double value);

/* New: List command - prints all stored variables */
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

/* Skip spaces and tabs */
void skip_whitespace(void) {
    while (*p == ' ' || *p == '\t')
        p++;
}

/* Lookup a variable in the symbol table */
double *get_variable(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0)
            return &variables[i].value;
    }
    return NULL;
}

/* Create or update a variable in the symbol table */
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
 *   Supports functions: sin, cos, tan, log, sqrt.
 */
double call_function(const char *func, double arg) {
    if (strcmp(func, "sin") == 0)
        return sin(arg);
    if (strcmp(func, "cos") == 0)
        return cos(arg);
    if (strcmp(func, "tan") == 0)
        return tan(arg);
    if (strcmp(func, "log") == 0)
        return log(arg);
    if (strcmp(func, "sqrt") == 0)
        return sqrt(arg);
    printf("Error: Unknown function '%s'\n", func);
    error_flag = 1;
    return 0;
}

/* ================= Matrix Mode (Basic Linear Algebra) ================= */

#define MAX_MATRICES 10

typedef struct {
    char name[32];
    int rows;
    int cols;
    double *data; /* Dynamically allocated array of size rows*cols */
} Matrix;

Matrix matrices[MAX_MATRICES];
int matrix_count = 0;

/* Find matrix by name */
Matrix *find_matrix(const char *name) {
    for (int i = 0; i < matrix_count; i++) {
        if (strcmp(matrices[i].name, name) == 0)
            return &matrices[i];
    }
    return NULL;
}

/* Create a new matrix */
void create_matrix(const char *name, int rows, int cols) {
    if (matrix_count >= MAX_MATRICES) {
        printf("Error: Matrix storage full.\n");
        return;
    }
    if (find_matrix(name) != NULL) {
        printf("Error: Matrix '%s' already exists.\n", name);
        return;
    }
    Matrix *m = &matrices[matrix_count++];
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->rows = rows;
    m->cols = cols;
    m->data = malloc(sizeof(double) * rows * cols);
    if (!m->data) {
        printf("Error: Memory allocation failed.\n");
        exit(1);
    }
    /* Initialize to zero */
    for (int i = 0; i < rows * cols; i++)
        m->data[i] = 0;
    printf("Matrix '%s' created (%dx%d).\n", name, rows, cols);
}

/* Set element (r,c) of matrix */
void set_matrix_element(const char *name, int r, int c, double value) {
    Matrix *m = find_matrix(name);
    if (!m) {
        printf("Error: Matrix '%s' not found.\n", name);
        return;
    }
    if (r < 0 || r >= m->rows || c < 0 || c >= m->cols) {
        printf("Error: Index out of bounds.\n");
        return;
    }
    m->data[r * m->cols + c] = value;
}

/* Print a matrix */
void print_matrix(const char *name) {
    Matrix *m = find_matrix(name);
    if (!m) {
        printf("Error: Matrix '%s' not found.\n", name);
        return;
    }
    printf("Matrix '%s' (%dx%d):\n", m->name, m->rows, m->cols);
    for (int i = 0; i < m->rows; i++) {
        for (int j = 0; j < m->cols; j++) {
            printf("%g ", m->data[i * m->cols + j]);
        }
        printf("\n");
    }
}

/* Add two matrices and print the result */
void add_matrices(const char *name1, const char *name2) {
    Matrix *a = find_matrix(name1);
    Matrix *b = find_matrix(name2);
    if (!a || !b) {
        printf("Error: One or both matrices not found.\n");
        return;
    }
    if (a->rows != b->rows || a->cols != b->cols) {
        printf("Error: Dimension mismatch for addition.\n");
        return;
    }
    printf("Result of %s + %s:\n", name1, name2);
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < a->cols; j++) {
            double sum = a->data[i * a->cols + j] + b->data[i * b->cols + j];
            printf("%g ", sum);
        }
        printf("\n");
    }
}

/* Subtract two matrices and print the result */
void sub_matrices(const char *name1, const char *name2) {
    Matrix *a = find_matrix(name1);
    Matrix *b = find_matrix(name2);
    if (!a || !b) {
        printf("Error: One or both matrices not found.\n");
        return;
    }
    if (a->rows != b->rows || a->cols != b->cols) {
        printf("Error: Dimension mismatch for subtraction.\n");
        return;
    }
    printf("Result of %s - %s:\n", name1, name2);
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < a->cols; j++) {
            double diff = a->data[i * a->cols + j] - b->data[i * b->cols + j];
            printf("%g ", diff);
        }
        printf("\n");
    }
}

/* Multiply two matrices and print the result */
void mul_matrices(const char *name1, const char *name2) {
    Matrix *a = find_matrix(name1);
    Matrix *b = find_matrix(name2);
    if (!a || !b) {
        printf("Error: One or both matrices not found.\n");
        return;
    }
    if (a->cols != b->rows) {
        printf("Error: Dimension mismatch for multiplication.\n");
        return;
    }
    printf("Result of %s * %s:\n", name1, name2);
    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            double sum = 0;
            for (int k = 0; k < a->cols; k++)
                sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
            printf("%g ", sum);
        }
        printf("\n");
    }
}

/* Matrix mode REPL */
void matrix_mode(void) {
    char line[256];
    printf("Entered MATRIX mode. Commands:\n");
    printf("  new <name> <rows> <cols>    - Create a new matrix\n");
    printf("  set <name> <r> <c> <value>    - Set element at row r, col c\n");
    printf("  add <name1> <name2>           - Add two matrices\n");
    printf("  sub <name1> <name2>           - Subtract second from first\n");
    printf("  mul <name1> <name2>           - Multiply two matrices\n");
    printf("  print <name>              - Print a matrix\n");
    printf("  back                      - Return to main mode\n");

    while (1) {
        printf("matrix> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "back") == 0)
            break;
        if (line[0] == '\0')
            continue;

        char command[32], arg1[32], arg2[32];
        int r, c, rows, cols;
        double value;
        if (sscanf(line, "%31s", command) != 1)
            continue;
        if (strcmp(command, "new") == 0) {
            if (sscanf(line, "%*s %31s %d %d", arg1, &rows, &cols) == 3)
                create_matrix(arg1, rows, cols);
            else
                printf("Usage: new <name> <rows> <cols>\n");
        } else if (strcmp(command, "set") == 0) {
            if (sscanf(line, "%*s %31s %d %d %lf", arg1, &r, &c, &value) == 4)
                set_matrix_element(arg1, r, c, value);
            else
                printf("Usage: set <name> <row> <col> <value>\n");
        } else if (strcmp(command, "add") == 0) {
            if (sscanf(line, "%*s %31s %31s", arg1, arg2) == 2)
                add_matrices(arg1, arg2);
            else
                printf("Usage: add <name1> <name2>\n");
        } else if (strcmp(command, "sub") == 0) {
            if (sscanf(line, "%*s %31s %31s", arg1, arg2) == 2)
                sub_matrices(arg1, arg2);
            else
                printf("Usage: sub <name1> <name2>\n");
        } else if (strcmp(command, "mul") == 0) {
            if (sscanf(line, "%*s %31s %31s", arg1, arg2) == 2)
                mul_matrices(arg1, arg2);
            else
                printf("Usage: mul <name1> <name2>\n");
        } else if (strcmp(command, "print") == 0) {
            if (sscanf(line, "%*s %31s", arg1) == 1)
                print_matrix(arg1);
            else
                printf("Usage: print <name>\n");
        } else {
            printf("Unknown matrix command: %s\n", command);
        }
    }
    for (int i = 0; i < matrix_count; i++) {
        free(matrices[i].data);
    }
    matrix_count = 0;
}

/* ================= Complex Mode (Complex Arithmetic) ================= */

/*
 * parse_complex:
 *   Very simple parser for complex numbers.
 *   Accepts input in forms like:
 *      3+4i,  3-4i, 4i, 3
 */
double complex parse_complex(const char *s) {
    double real = 0, imag = 0;
    const char *i_ptr = strchr(s, 'i');
    if (!i_ptr) {
        real = strtod(s, NULL);
        return real + 0*I;
    } else {
        if (strchr(s, '+') || strchr(s, '-')) {
            sscanf(s, "%lf%lf", &real, &imag);
        } else {
            imag = strtod(s, NULL);
        }
        return real + imag * I;
    }
}

/* Print a complex number in a+bi format */
void print_complex(double complex c) {
    double a = creal(c), b = cimag(c);
    if (b >= 0)
        printf("%g+%gi\n", a, b);
    else
        printf("%g%gi\n", a, b);
}

/* Complex mode REPL */
void complex_mode(void) {
    char line[256];
    printf("Entered COMPLEX mode. Use format a+bi (e.g., 3+4i).\n");
    printf("Supported commands:\n");
    printf("  calc <expression>  - Evaluate a complex expression\n");
    printf("  add <c1> <c2>      - Add two complex numbers\n");
    printf("  sub <c1> <c2>      - Subtract complex numbers\n");
    printf("  mul <c1> <c2>      - Multiply complex numbers\n");
    printf("  div <c1> <c2>      - Divide complex numbers\n");
    printf("  back               - Return to main mode\n");

    while (1) {
        printf("complex> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "back") == 0)
            break;
        if (line[0] == '\0')
            continue;

        char command[32];
        char arg1[64], arg2[64];
        if (sscanf(line, "%31s", command) != 1)
            continue;
        if (strcmp(command, "calc") == 0) {
            char *expr = line + strlen("calc");
            double complex c = parse_complex(expr);
            printf("Result: ");
            print_complex(c);
        } else if (strcmp(command, "add") == 0) {
            if (sscanf(line, "%*s %63s %63s", arg1, arg2) == 2) {
                double complex c1 = parse_complex(arg1);
                double complex c2 = parse_complex(arg2);
                printf("Result: ");
                print_complex(c1 + c2);
            } else {
                printf("Usage: add <c1> <c2>\n");
            }
        } else if (strcmp(command, "sub") == 0) {
            if (sscanf(line, "%*s %63s %63s", arg1, arg2) == 2) {
                double complex c1 = parse_complex(arg1);
                double complex c2 = parse_complex(arg2);
                printf("Result: ");
                print_complex(c1 - c2);
            } else {
                printf("Usage: sub <c1> <c2>\n");
            }
        } else if (strcmp(command, "mul") == 0) {
            if (sscanf(line, "%*s %63s %63s", arg1, arg2) == 2) {
                double complex c1 = parse_complex(arg1);
                double complex c2 = parse_complex(arg2);
                printf("Result: ");
                print_complex(c1 * c2);
            } else {
                printf("Usage: mul <c1> <c2>\n");
            }
        } else if (strcmp(command, "div") == 0) {
            if (sscanf(line, "%*s %63s %63s", arg1, arg2) == 2) {
                double complex c1 = parse_complex(arg1);
                double complex c2 = parse_complex(arg2);
                if (c2 == 0) {
                    printf("Error: Division by zero.\n");
                } else {
                    printf("Result: ");
                    print_complex(c1 / c2);
                }
            } else {
                printf("Usage: div <c1> <c2>\n");
            }
        } else {
            printf("Unknown complex command: %s\n", command);
        }
    }
}

/* ================= Differential Equation Solver (Euler's Method) ================= */

/*
 * dsolve_mode:
 *   Prompts for a derivative function f(t,y), initial conditions (t0, y0),
 *   final time tf, and step size h. Uses Euler’s method to solve the ODE.
 */
void dsolve_mode(void) {
    char dexpr[256];
    char line[256];
    double t0, y0, tf, h;
    printf("Entered DSOLVE mode (Euler integration for dy/dt = f(t,y)).\n");
    printf("Enter derivative function f(t,y): ");
    if (!fgets(dexpr, sizeof(dexpr), stdin))
        return;
    dexpr[strcspn(dexpr, "\n")] = '\0';
    printf("Enter initial time t0: ");
    fgets(line, sizeof(line), stdin);
    t0 = atof(line);
    printf("Enter initial value y0: ");
    fgets(line, sizeof(line), stdin);
    y0 = atof(line);
    printf("Enter final time tf: ");
    fgets(line, sizeof(line), stdin);
    tf = atof(line);
    printf("Enter step size h: ");
    fgets(line, sizeof(line), stdin);
    h = atof(line);

    printf("Solving ODE:\n");
    printf("  dy/dt = %s,  t0 = %g,  y0 = %g,  tf = %g,  h = %g\n", dexpr, t0, y0, tf, h);
    printf("t\t\ty\n");
    double t = t0, y = y0;
    while (t <= tf) {
        printf("%g\t%g\n", t, y);
        set_variable("t", t);
        set_variable("y", y);
        p = dexpr;
        double f = parse_expression();
        if (error_flag) {
            error_flag = 0;
            printf("Error evaluating derivative at t=%g\n", t);
            break;
        }
        y = y + h * f;
        t = t + h;
    }
}

/* ================= Symbolic Differentiation Mode ================= */

/*
 * Symbolic mode builds a simple expression tree for functions of x,
 * then computes and prints the derivative with respect to x.
 */

typedef enum {
    NODE_NUM,
    NODE_VAR,
    NODE_ADD,
    NODE_SUB,
    NODE_MUL,
    NODE_DIV,
    NODE_POW,
    NODE_FUNC
} NodeType;

typedef struct Node {
    NodeType type;
    double value;      /* For NODE_NUM */
    char var;          /* For NODE_VAR (only 'x' supported) */
    char func[16];     /* For NODE_FUNC */
    struct Node *left;
    struct Node *right;
} Node;

Node *sym_parse_expression(const char **s);
Node *sym_parse_term(const char **s);
Node *sym_parse_factor(const char **s);
Node *sym_parse_primary(const char **s);
void sym_skip_whitespace(const char **s);

void sym_skip_whitespace(const char **s) {
    while (**s == ' ' || **s == '\t')
        (*s)++;
}

Node *new_node(NodeType type) {
    Node *node = malloc(sizeof(Node));
    node->type = type;
    node->value = 0;
    node->var = '\0';
    node->func[0] = '\0';
    node->left = node->right = NULL;
    return node;
}

Node *sym_parse_number(const char **s) {
    sym_skip_whitespace(s);
    char *endptr;
    double num = strtod(*s, &endptr);
    if (*s == endptr) return NULL;
    *s = endptr;
    Node *node = new_node(NODE_NUM);
    node->value = num;
    return node;
}

void sym_parse_identifier(const char **s, char *buf, int bufsize) {
    sym_skip_whitespace(s);
    int i = 0;
    while ((isalnum(**s) || **s == '_') && i < bufsize - 1) {
        buf[i++] = **s;
        (*s)++;
    }
    buf[i] = '\0';
}

/*
 * sym_parse_primary:
 *   primary -> number | variable | function '(' expression ')' | '(' expression ')' | unary +/- primary
 */
Node *sym_parse_primary(const char **s) {
    sym_skip_whitespace(s);
    if (**s == '(') {
        (*s)++;
        Node *node = sym_parse_expression(s);
        sym_skip_whitespace(s);
        if (**s == ')')
            (*s)++;
        return node;
    } else if (isdigit(**s) || **s == '.') {
        return sym_parse_number(s);
    } else if (isalpha(**s)) {
        char ident[32];
        sym_parse_identifier(s, ident, sizeof(ident));
        sym_skip_whitespace(s);
        if (**s == '(') {
            (*s)++;
            Node *arg = sym_parse_expression(s);
            sym_skip_whitespace(s);
            if (**s == ')')
                (*s)++;
            Node *node = new_node(NODE_FUNC);
            strncpy(node->func, ident, sizeof(node->func)-1);
            node->left = arg;
            return node;
        } else {
            Node *node = new_node(NODE_VAR);
            node->var = ident[0];
            return node;
        }
    } else if (**s == '-') {
        (*s)++;
        Node *node = new_node(NODE_MUL);
        node->left = new_node(NODE_NUM);
        node->left->value = -1;
        node->right = sym_parse_primary(s);
        return node;
    } else if (**s == '+') {
        (*s)++;
        return sym_parse_primary(s);
    }
    return NULL;
}

Node *sym_parse_factor(const char **s) {
    Node *node = sym_parse_primary(s);
    sym_skip_whitespace(s);
    while (**s == '^') {
        (*s)++;
        Node *exponent = sym_parse_factor(s);
        Node *new_node_ptr = new_node(NODE_POW);
        new_node_ptr->left = node;
        new_node_ptr->right = exponent;
        node = new_node_ptr;
        sym_skip_whitespace(s);
    }
    return node;
}

Node *sym_parse_term(const char **s) {
    Node *node = sym_parse_factor(s);
    sym_skip_whitespace(s);
    while (**s == '*' || **s == '/') {
        char op = **s;
        (*s)++;
        Node *rhs = sym_parse_factor(s);
        Node *new_node_ptr = new_node(op == '*' ? NODE_MUL : NODE_DIV);
        new_node_ptr->left = node;
        new_node_ptr->right = rhs;
        node = new_node_ptr;
        sym_skip_whitespace(s);
    }
    return node;
}

Node *sym_parse_expression(const char **s) {
    Node *node = sym_parse_term(s);
    sym_skip_whitespace(s);
    while (**s == '+' || **s == '-') {
        char op = **s;
        (*s)++;
        Node *rhs = sym_parse_term(s);
        Node *new_node_ptr = new_node(op == '+' ? NODE_ADD : NODE_SUB);
        new_node_ptr->left = node;
        new_node_ptr->right = rhs;
        node = new_node_ptr;
        sym_skip_whitespace(s);
    }
    return node;
}

void free_expr(Node *node) {
    if (!node) return;
    free_expr(node->left);
    free_expr(node->right);
    free(node);
}

/*
 * differentiate:
 *   Recursively differentiates the expression tree with respect to x.
 */
Node *differentiate(Node *node) {
    if (!node) return NULL;
    Node *result = NULL;
    switch (node->type) {
        case NODE_NUM:
            result = new_node(NODE_NUM);
            result->value = 0;
            break;
        case NODE_VAR:
            result = new_node(NODE_NUM);
            result->value = (node->var == 'x') ? 1 : 0;
            break;
        case NODE_ADD:
        case NODE_SUB:
            result = new_node(node->type);
            result->left = differentiate(node->left);
            result->right = differentiate(node->right);
            break;
        case NODE_MUL: {
            Node *left_d = differentiate(node->left);
            Node *right_d = differentiate(node->right);
            Node *term1 = new_node(NODE_MUL);
            term1->left = left_d;
            term1->right = node->right;
            Node *term2 = new_node(NODE_MUL);
            term2->left = node->left;
            term2->right = right_d;
            result = new_node(NODE_ADD);
            result->left = term1;
            result->right = term2;
            break;
        }
        case NODE_DIV: {
            Node *left_d = differentiate(node->left);
            Node *right_d = differentiate(node->right);
            Node *term1 = new_node(NODE_MUL);
            term1->left = left_d;
            term1->right = node->right;
            Node *term2 = new_node(NODE_MUL);
            term2->left = node->left;
            term2->right = right_d;
            Node *num = new_node(NODE_SUB);
            num->left = term1;
            num->right = term2;
            Node *denom = new_node(NODE_POW);
            denom->left = node->right;
            Node *two = new_node(NODE_NUM);
            two->value = 2;
            denom->right = two;
            result = new_node(NODE_DIV);
            result->left = num;
            result->right = denom;
            break;
        }
        case NODE_POW: {
            if (node->right->type == NODE_NUM) {
                double n = node->right->value;
                Node *new_exp = new_node(NODE_NUM);
                new_exp->value = n - 1;
                Node *f_pow = new_node(NODE_POW);
                f_pow->left = node->left;
                f_pow->right = new_exp;
                Node *mult1 = new_node(NODE_MUL);
                Node *n_node = new_node(NODE_NUM);
                n_node->value = n;
                mult1->left = n_node;
                mult1->right = f_pow;
                Node *f_d = differentiate(node->left);
                result = new_node(NODE_MUL);
                result->left = mult1;
                result->right = f_d;
            } else {
                result = new_node(NODE_NUM);
                result->value = 0;
            }
            break;
        }
        case NODE_FUNC: {
            if (strcmp(node->func, "sin") == 0) {
                Node *cos_node = new_node(NODE_FUNC);
                strncpy(cos_node->func, "cos", sizeof(cos_node->func)-1);
                cos_node->left = node->left;
                Node *f_d = differentiate(node->left);
                result = new_node(NODE_MUL);
                result->left = cos_node;
                result->right = f_d;
            } else if (strcmp(node->func, "cos") == 0) {
                Node *sin_node = new_node(NODE_FUNC);
                strncpy(sin_node->func, "sin", sizeof(sin_node->func)-1);
                sin_node->left = node->left;
                Node *f_d = differentiate(node->left);
                Node *neg = new_node(NODE_NUM);
                neg->value = -1;
                Node *mult1 = new_node(NODE_MUL);
                mult1->left = neg;
                mult1->right = sin_node;
                result = new_node(NODE_MUL);
                result->left = mult1;
                result->right = f_d;
            } else if (strcmp(node->func, "log") == 0) {
                Node *f_d = differentiate(node->left);
                result = new_node(NODE_DIV);
                result->left = f_d;
                result->right = node->left;
            } else {
                result = new_node(NODE_NUM);
                result->value = 0;
            }
            break;
        }
        default:
            result = new_node(NODE_NUM);
            result->value = 0;
    }
    return result;
}

void print_expr(Node *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_NUM:
            printf("%g", node->value);
            break;
        case NODE_VAR:
            printf("%c", node->var);
            break;
        case NODE_ADD:
            printf("(");
            print_expr(node->left);
            printf(" + ");
            print_expr(node->right);
            printf(")");
            break;
        case NODE_SUB:
            printf("(");
            print_expr(node->left);
            printf(" - ");
            print_expr(node->right);
            printf(")");
            break;
        case NODE_MUL:
            printf("(");
            print_expr(node->left);
            printf(" * ");
            print_expr(node->right);
            printf(")");
            break;
        case NODE_DIV:
            printf("(");
            print_expr(node->left);
            printf(" / ");
            print_expr(node->right);
            printf(")");
            break;
        case NODE_POW:
            printf("(");
            print_expr(node->left);
            printf("^");
            print_expr(node->right);
            printf(")");
            break;
        case NODE_FUNC:
            printf("%s(", node->func);
            print_expr(node->left);
            printf(")");
            break;
        default:
            break;
    }
}

/* Symbolic mode REPL */
void symbolic_mode(void) {
    char line[256];
    printf("Entered SYMBOLIC mode (differentiate with respect to x).\n");
    printf("Enter an expression in x (e.g., sin(x) + x^2):\n");
    if (!fgets(line, sizeof(line), stdin))
        return;
    line[strcspn(line, "\n")] = '\0';
    const char *s = line;
    Node *expr = sym_parse_expression(&s);
    if (!expr) {
        printf("Error parsing expression.\n");
        return;
    }
    printf("Parsed expression: ");
    print_expr(expr);
    printf("\n");
    Node *dexpr = differentiate(expr);
    printf("Derivative: ");
    print_expr(dexpr);
    printf("\n");
    free_expr(expr);
    free_expr(dexpr);
}

/* ================= Main REPL Loop ================= */

/*
 * print_help:
 *   Prints detailed instructions for using the math terminal.
 */
void print_help(void) {
    printf("=== CMath Help Menu ===\n\n");
    printf("This math terminal supports various calculation modes:\n\n");
    printf("1. Scalar Mode (Basic Arithmetic):\n");
    printf("   - Enter arithmetic expressions (e.g., 2+3*4, sin(0.5)).\n");
    printf("   - Assign variables using: x = 3.14\n");
    printf("   - Use variables in expressions.\n");
    printf("   - Type 'list' to display all stored variables and their values.\n\n");
    printf("2. Matrix Mode (Linear Algebra):\n");
    printf("   - Type 'matrix' to enter matrix mode.\n");
    printf("   - Commands:\n");
    printf("       new <name> <rows> <cols>    : Create a new matrix.\n");
    printf("       set <name> <row> <col> <value> : Set a matrix element.\n");
    printf("       add <name1> <name2>         : Add two matrices.\n");
    printf("       sub <name1> <name2>         : Subtract two matrices.\n");
    printf("       mul <name1> <name2>         : Multiply two matrices.\n");
    printf("       print <name>              : Display a matrix.\n");
    printf("       back                      : Return to main mode.\n\n");
    printf("3. Complex Mode (Complex Arithmetic):\n");
    printf("   - Type 'complex' to enter complex mode.\n");
    printf("   - Use format a+bi (e.g., 3+4i).\n");
    printf("   - Commands:\n");
    printf("       calc <expression>  : Evaluate a complex expression.\n");
    printf("       add <c1> <c2>      : Add two complex numbers.\n");
    printf("       sub <c1> <c2>      : Subtract complex numbers.\n");
    printf("       mul <c1> <c2>      : Multiply complex numbers.\n");
    printf("       div <c1> <c2>      : Divide complex numbers.\n");
    printf("       back               : Return to main mode.\n\n");
    printf("4. DSOLVE Mode (Differential Equations):\n");
    printf("   - Type 'dsolve' to solve an ODE using Euler's method.\n");
    printf("   - Follow prompts to enter f(t,y), t0, y0, tf, and h.\n\n");
    printf("5. Symbolic Mode (Differentiation):\n");
    printf("   - Type 'symbolic' to differentiate an expression with respect to x.\n");
    printf("   - Enter an expression (e.g., sin(x) + x^2).\n\n");
    printf("Main Commands:\n");
    printf("  help          : Show this help menu\n");
    printf("  list          : List all stored variables and their values\n");
    printf("  exit, quit    : Exit the math terminal\n");
    printf("  <expression>  : Evaluate a scalar arithmetic expression\n\n");
    printf("Macro Mode:\n");
    printf("  To run a stored macro, pass a filename as an argument:\n");
    printf("      ./cmath mymacro.m\n");
    printf("  The file should contain commands just as if they were typed at the prompt.\n\n");
    printf("Examples in Scalar Mode:\n");
    printf("  2 + 3 * 4      -> Evaluates the expression\n");
    printf("  x = 3.14       -> Assigns 3.14 to variable x\n");
    printf("  sin(0.5) + x   -> Uses the sine function and variable x\n\n");
}

/* Main REPL */
int main(int argc, char *argv[]) {
    char line[256];
    int interactive = 1;

    /* If a macro file is provided as argument, redirect stdin to that file and disable prompt */
    if (argc > 1) {
        if (freopen(argv[1], "r", stdin) == NULL) {
            perror("Error opening macro file");
            exit(1);
        }
        interactive = 0;
    }

    printf("Welcome to the Extended C Math Terminal.\n");
    printf("Type 'help' to list supported commands.\n");
    printf("Type 'exit' or 'quit' to leave.\n");

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
        if (strcmp(line, "matrix") == 0) {
            matrix_mode();
            continue;
        }
        if (strcmp(line, "complex") == 0) {
            complex_mode();
            continue;
        }
        if (strcmp(line, "dsolve") == 0) {
            dsolve_mode();
            continue;
        }
        if (strcmp(line, "symbolic") == 0) {
            symbolic_mode();
            continue;
        }
        if (line[0] == '\0')
            continue;

        /* Process scalar arithmetic or assignment */
        p = line;
        skip_whitespace();
        const char *temp = p;
        if (isalpha(*temp)) {
            char ident[32];
            int i = 0;
            while ((isalnum(*temp) || *temp == '_') && i < (int)(sizeof(ident) - 1)) {
                ident[i++] = *temp;
                temp++;
            }
            ident[i] = '\0';
            skip_whitespace();
            if (*temp == '=') {
                p += strlen(ident);
                skip_whitespace();
                if (*p != '=') {
                    printf("Error: Expected '=' in assignment.\n");
                    continue;
                }
                p++; /* skip '=' */
                skip_whitespace();
                double result = parse_expression();
                if (error_flag) {
                    error_flag = 0;
                    continue;
                }
                set_variable(ident, result);
                printf("%s = %g\n", ident, result);
                continue;
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
