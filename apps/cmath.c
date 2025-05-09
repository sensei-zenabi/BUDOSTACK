#define _POSIX_C_SOURCE 200809L

/*
 * cmath.c - Extended Math Interpreter with Basic Matrix/Array Functionality
 *
 * Design Principles and Modifications:
 * - Retains the original REPL for scalar arithmetic but now uses a unified Value type
 *   (scalar or matrix).
 * - Introduces matrix literal parsing using Octave-like syntax:
 *       [1, 2, 3; 4, 5, 6]
 * - Implements basic arithmetic operations:
 *     - Standard operators: '+' and '-' (element-wise for matrices)
 *     - '*' and '/' perform standard matrix multiplication (with dimension checking)
 *     - New element-wise operators: ".*", "./", ".^" for element-by-element operations.
 * - Functions (sin, cos, etc.) are applied element-wise if passed a matrix.
 * - Variable usage now makes a deep copy when returning a stored matrix so that printing
 *   temporary results does not free the variable’s memory.
 * - Supports assignment and script ('.m') files.
 * - REMAINS a single-file implementation in plain C (compiled with -std=c11, POSIX compliant).
 *
 * New Feature:
 * - Implements a command history with up and down arrow key navigation in interactive mode.
 *   This is achieved by switching the terminal into non-canonical mode using termios,
 *   capturing individual keystrokes, and handling the escape sequences corresponding to
 *   the arrow keys.
 *
 * To compile:
 *     gcc -std=c11 -o cmath cmath.c -lm
 *
 * To run interactively:
 *     ./cmath
 *
 * To run a script (e.g., mymacro.m):
 *     ./cmath mymacro.m
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>

#define MAX_VARS 100
#define MAX_MATRIX_ROWS 100
#define MAX_MATRIX_COLS 100

/* --- Command History and Line Editing --- */
#define MAX_HISTORY 100

/* Global history buffer for interactive command input */
char *history[MAX_HISTORY];
int history_count = 0;

/* Structure to save original terminal settings */
struct termios orig_termios;

/* Disable raw mode and restore original terminal settings */
void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Enable raw mode (non-canonical, no echo) for character–by–character input */
void enable_raw_mode(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/*
 * get_line:
 *  Reads a line of input with a simple line editor.
 *  Supports backspace and history navigation with up/down arrow keys.
 *  Arrow up (ESC [ A) loads the previous command; arrow down (ESC [ B) loads the next.
 *  The prompt "math> " is reprinted after each history recall.
 */
char *get_line(void) {
    static char buffer[256];
    int pos = 0;
    int history_index = history_count; // start at the end of history
    memset(buffer, 0, sizeof(buffer));
    
    enable_raw_mode();
    write(STDOUT_FILENO, "math> ", 6);
    
    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            break;
        if (c == '\r' || c == '\n') {
            write(STDOUT_FILENO, "\r\n", 2);
            buffer[pos] = '\0';
            break;
        } else if (c == 127 || c == 8) { // Handle backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                /* Erase the character from the terminal */
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (c == 27) { // Start of an escape sequence (likely arrow keys)
            char seq[2];
            if (read(STDIN_FILENO, seq, 2) != 2)
                continue;
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Up arrow: navigate to previous command
                    if (history_index > 0) {
                        history_index--;
                        /* Clear current line: ESC[2K clears entire line, \r returns cursor */
                        char clear_seq[] = "\33[2K\r";
                        write(STDOUT_FILENO, clear_seq, strlen(clear_seq));
                        write(STDOUT_FILENO, "math> ", 6);
                        /* Load command from history */
                        int len = strlen(history[history_index]);
                        if (len > 255) len = 255;
                        strcpy(buffer, history[history_index]);
                        pos = len;
                        write(STDOUT_FILENO, buffer, pos);
                    }
                } else if (seq[1] == 'B') { // Down arrow: navigate to next command
                    if (history_index < history_count - 1) {
                        history_index++;
                        char clear_seq[] = "\33[2K\r";
                        write(STDOUT_FILENO, clear_seq, strlen(clear_seq));
                        write(STDOUT_FILENO, "math> ", 6);
                        int len = strlen(history[history_index]);
                        if (len > 255) len = 255;
                        strcpy(buffer, history[history_index]);
                        pos = len;
                        write(STDOUT_FILENO, buffer, pos);
                    } else {
                        /* At the end of history, clear the line */
                        history_index = history_count;
                        char clear_seq[] = "\33[2K\r";
                        write(STDOUT_FILENO, clear_seq, strlen(clear_seq));
                        write(STDOUT_FILENO, "math> ", 6);
                        pos = 0;
                        buffer[0] = '\0';
                    }
                }
            }
        } else if (c >= 32 && c <= 126) { // Printable characters
            if (pos < 255) {
                buffer[pos++] = c;
                write(STDOUT_FILENO, &c, 1);
            }
        }
    }
    disable_raw_mode();
    return buffer;
}

/* --- Unified Value Type --- */
typedef enum {
    VAL_SCALAR,
    VAL_MATRIX
} ValueType;

typedef struct {
    ValueType type;
    union {
        double scalar;
        struct {
            int rows;
            int cols;
            double *data;  // Dynamically allocated array of size rows * cols
        } matrix;
    };
} Value;

/* --- Variable Table --- */
struct Variable {
    char name[32];
    Value value;
};

struct Variable variables[MAX_VARS];
int var_count = 0;

/* Global pointer for input string parsing */
const char *p;
/* Global error flag */
int error_flag = 0;

/* --- Function Prototypes --- */
void skip_whitespace(void);
Value parse_expression(void);
Value parse_term(void);
Value parse_factor(void);
Value parse_primary(void);
Value parse_matrix_literal(void);
Value call_function(const char *func, Value arg);

Value deep_copy_value(Value v);

void set_variable(const char *name, Value val);
struct Variable *get_variable_by_name(const char *name);
void list_variables(void);
void print_help(void);
void print_value(Value val);
void free_value(Value *val);

Value add_values(Value a, Value b);
Value subtract_values(Value a, Value b);
Value multiply_values(Value a, Value b);
Value divide_values(Value a, Value b);
Value power_values(Value a, Value b);

Value elementwise_multiply_values(Value a, Value b);
Value elementwise_divide_values(Value a, Value b);
Value elementwise_pow_values(Value a, Value b);

/* --- Utility Functions --- */

/* Skip spaces and tabs */
void skip_whitespace(void) {
    while (*p == ' ' || *p == '\t')
        p++;
}

/* Retrieve variable by name */
struct Variable *get_variable_by_name(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0)
            return &variables[i];
    }
    return NULL;
}

/* Deep copy a Value (for matrices only; scalars are copied by value) */
Value deep_copy_value(Value v) {
    if (v.type == VAL_MATRIX) {
        Value copy;
        copy.type = VAL_MATRIX;
        copy.matrix.rows = v.matrix.rows;
        copy.matrix.cols = v.matrix.cols;
        int size = v.matrix.rows * v.matrix.cols;
        copy.matrix.data = malloc(size * sizeof(double));
        if (!copy.matrix.data) {
            printf("Error: Memory allocation failed in deep_copy_value\n");
            exit(1);
        }
        memcpy(copy.matrix.data, v.matrix.data, size * sizeof(double));
        return copy;
    }
    return v;
}

/* Free allocated memory for a Value (if it is a matrix) */
void free_value(Value *v) {
    if (v->type == VAL_MATRIX && v->matrix.data != NULL) {
        free(v->matrix.data);
        v->matrix.data = NULL;
    }
}

/* Set or update a variable */
void set_variable(const char *name, Value val) {
    struct Variable *var = get_variable_by_name(name);
    if (var != NULL) {
        free_value(&var->value);
        var->value = val;
        return;
    }
    if (var_count < MAX_VARS) {
        strncpy(variables[var_count].name, name, sizeof(variables[var_count].name) - 1);
        variables[var_count].name[sizeof(variables[var_count].name) - 1] = '\0';
        variables[var_count].value = val;
        var_count++;
    } else {
        printf("Error: Variable limit reached\n");
        if (val.type == VAL_MATRIX)
            free(val.matrix.data);
    }
}

/* Print a Value (scalar or matrix) */
void print_value(Value val) {
    if (val.type == VAL_SCALAR) {
        printf("%g\n", val.scalar);
    } else if (val.type == VAL_MATRIX) {
        int rows = val.matrix.rows;
        int cols = val.matrix.cols;
        printf("[");
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++){
                printf("%g", val.matrix.data[i * cols + j]);
                if (j < cols - 1)
                    printf(" ");
            }
            if (i < rows - 1)
                printf(";\n ");
        }
        printf("]\n");
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
        printf("  %s = ", variables[i].name);
        print_value(variables[i].value);
    }
}

/* Print help message with supported commands and functions */
void print_help(void) {
    printf("=== CMath Help Menu ===\n\n");
    printf("Supported Commands:\n");
    printf("  help          : Show this help menu\n");
    printf("  list          : List all stored variables\n");
    printf("  exit, quit    : Exit the math terminal\n\n");
    printf("Usage:\n");
    printf("  Enter arithmetic expressions to evaluate them.\n");
    printf("  Assignment: variable = expression (e.g., x = 3.14 or A = [1,2;3,4]).\n");
    printf("  Matrix literals: use [ ] with commas separating columns and semicolons separating rows.\n\n");
    printf("Supported Operations:\n");
    printf("  Addition:       +\n");
    printf("  Subtraction:    -\n");
    printf("  Multiplication: * (matrix multiplication) and .* (element-wise multiplication)\n");
    printf("  Division:       / (matrix division by scalar) and ./ (element-wise division)\n");
    printf("  Exponentiation: ^ (scalars only) and .^ (element-wise exponentiation)\n\n");
    printf("Supported Functions (applied element-wise on matrices):\n");
    printf("  sin, cos, tan, asin, acos, atan,\n");
    printf("  log (natural log), log10 (base-10 log),\n");
    printf("  sqrt, exp,\n");
    printf("  abs (absolute value),\n");
    printf("  sinh, cosh, tanh,\n");
    printf("  floor, ceil\n\n");
    printf("Examples:\n");
    printf("  2 + 3 * 4            -> Evaluates to 14\n");
    printf("  x = 3.14             -> Assigns 3.14 to variable x\n");
    printf("  A = [1, 2, 3; 4, 5, 6] -> Creates a 2x3 matrix A\n");
    printf("  A .* 10              -> Element-wise multiplication (each element multiplied by 10)\n");
    printf("  sin(A)               -> Applies sine element-wise to matrix A\n");
}

/* --- Parsing Functions --- */

/*
 * parse_expression:
 *   expression -> term { ('+' | '-') term }
 */
Value parse_expression(void) {
    Value value = parse_term();
    skip_whitespace();
    while (*p == '+' || *p == '-') {
        char op = *p;
        p++;
        skip_whitespace();
        Value term = parse_term();
        if (op == '+')
            value = add_values(value, term);
        else
            value = subtract_values(value, term);
        skip_whitespace();
    }
    return value;
}

/*
 * parse_term:
 *   term -> factor { (".*" | "./" | "*" | "/" ) factor }
 */
Value parse_term(void) {
    Value value = parse_factor();
    skip_whitespace();
    while (1) {
        if (strncmp(p, ".*", 2) == 0) {
            p += 2;
            skip_whitespace();
            Value factor = parse_factor();
            value = elementwise_multiply_values(value, factor);
        } else if (strncmp(p, "./", 2) == 0) {
            p += 2;
            skip_whitespace();
            Value factor = parse_factor();
            value = elementwise_divide_values(value, factor);
        } else if (*p == '*') {
            p++;
            skip_whitespace();
            Value factor = parse_factor();
            value = multiply_values(value, factor);
        } else if (*p == '/') {
            p++;
            skip_whitespace();
            Value factor = parse_factor();
            value = divide_values(value, factor);
        } else {
            break;
        }
        skip_whitespace();
    }
    return value;
}

/*
 * parse_factor:
 *   factor -> primary { (".^" | "^") factor }
 */
Value parse_factor(void) {
    Value value = parse_primary();
    skip_whitespace();
    while (1) {
        if (strncmp(p, ".^", 2) == 0) {
            p += 2;
            skip_whitespace();
            Value exponent = parse_factor();
            value = elementwise_pow_values(value, exponent);
        } else if (*p == '^') {
            p++;
            skip_whitespace();
            Value exponent = parse_factor();
            value = power_values(value, exponent);
        } else {
            break;
        }
        skip_whitespace();
    }
    return value;
}

/*
 * parse_primary:
 *   primary -> number | variable | function '(' expression ')' | matrix_literal | '(' expression ')' | unary +/- primary
 */
Value parse_primary(void) {
    skip_whitespace();
    if (*p == '(') {
        p++;
        Value value = parse_expression();
        skip_whitespace();
        if (*p == ')') {
            p++;
        } else {
            printf("Error: Expected ')'\n");
            error_flag = 1;
        }
        return value;
    } else if (*p == '[') {
        return parse_matrix_literal();
    } else if (isdigit(*p) || *p == '.' || (((*p == '-' || *p == '+')) && isdigit(*(p+1)))) {
        char *endptr;
        double num = strtod(p, &endptr);
        if (p == endptr) {
            printf("Error: Invalid number format\n");
            error_flag = 1;
        }
        p = endptr;
        Value ret;
        ret.type = VAL_SCALAR;
        ret.scalar = num;
        return ret;
    } else if (isalpha(*p)) {
        char ident[32];
        int i = 0;
        while ((isalnum(*p) || *p == '_') && i < (int)(sizeof(ident) - 1)) {
            ident[i++] = *p;
            p++;
        }
        ident[i] = '\0';
        skip_whitespace();
        if (*p == '(') {
            p++;
            skip_whitespace();
            Value arg = parse_expression();
            skip_whitespace();
            if (*p == ')') {
                p++;
            } else {
                printf("Error: Expected ')' after function argument\n");
                error_flag = 1;
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            return call_function(ident, arg);
        } else {
            struct Variable *var = get_variable_by_name(ident);
            if (var == NULL) {
                printf("Error: Unknown variable '%s'\n", ident);
                error_flag = 1;
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            if (var->value.type == VAL_MATRIX)
                return deep_copy_value(var->value);
            else
                return var->value;
        }
    } else if (*p == '-') {
        p++;
        Value v = parse_primary();
        Value zero = { .type = VAL_SCALAR, .scalar = 0 };
        return subtract_values(zero, v);
    } else if (*p == '+') {
        p++;
        return parse_primary();
    } else {
        printf("Error: Unexpected character '%c'\n", *p);
        error_flag = 1;
        Value err = { .type = VAL_SCALAR, .scalar = 0 };
        return err;
    }
}

/*
 * parse_matrix_literal:
 *   Parses a matrix literal of the form: [num, num, ...; num, num, ...]
 */
Value parse_matrix_literal(void) {
    p++;
    double temp[MAX_MATRIX_ROWS][MAX_MATRIX_COLS];
    int row_count = 0;
    int col_count = -1;

    while (1) {
        skip_whitespace();
        if (*p == ']') {
            p++;
            break;
        }
        int col = 0;
        while (1) {
            skip_whitespace();
            if (!isdigit(*p) && *p != '.' && *p != '-' && *p != '+') {
                printf("Error: Expected number in matrix literal\n");
                error_flag = 1;
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            char *endptr;
            double num = strtod(p, &endptr);
            if (p == endptr) {
                printf("Error: Invalid number in matrix literal\n");
                error_flag = 1;
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            if (row_count >= MAX_MATRIX_ROWS || col >= MAX_MATRIX_COLS) {
                printf("Error: Matrix literal exceeds maximum dimensions\n");
                error_flag = 1;
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            temp[row_count][col] = num;
            col++;
            p = endptr;
            skip_whitespace();
            if (*p == ',') {
                p++;
                continue;
            } else {
                break;
            }
        }
        if (col_count == -1) {
            col_count = col;
        } else if (col != col_count) {
            printf("Error: Inconsistent number of columns in matrix literal\n");
            error_flag = 1;
        }
        row_count++;
        skip_whitespace();
        if (*p == ';') {
            p++;
            continue;
        } else if (*p == ']') {
            p++;
            break;
        } else {
            printf("Error: Expected ';' or ']' in matrix literal\n");
            error_flag = 1;
            break;
        }
    }

    if (row_count == 0 || col_count <= 0) {
        /* Return an empty 0×0 matrix */
        Value val;
        val.type = VAL_MATRIX;
        val.matrix.rows = 0;
        val.matrix.cols = 0;
        val.matrix.data = NULL;
        return val;
    }

    Value val;
    val.type = VAL_MATRIX;
    val.matrix.rows = row_count;
    val.matrix.cols = col_count;
    int size = row_count * col_count;
    val.matrix.data = malloc(size * sizeof(double));
    if (!val.matrix.data) {
        printf("Error: Memory allocation failed for matrix\n");
        exit(1);
    }
    for (int i = 0; i < row_count; i++) {
        for (int j = 0; j < col_count; j++) {
            val.matrix.data[i * col_count + j] = temp[i][j];
        }
    }
    return val;
}

/* --- Function Call Implementation --- */
Value call_function(const char *func, Value arg) {
    if (arg.type == VAL_SCALAR) {
        double a = arg.scalar, res;
        if (strcmp(func, "sin") == 0)
            res = sin(a);
        else if (strcmp(func, "cos") == 0)
            res = cos(a);
        else if (strcmp(func, "tan") == 0)
            res = tan(a);
        else if (strcmp(func, "asin") == 0)
            res = asin(a);
        else if (strcmp(func, "acos") == 0)
            res = acos(a);
        else if (strcmp(func, "atan") == 0)
            res = atan(a);
        else if (strcmp(func, "log") == 0)
            res = log(a);
        else if (strcmp(func, "log10") == 0)
            res = log10(a);
        else if (strcmp(func, "sqrt") == 0)
            res = sqrt(a);
        else if (strcmp(func, "exp") == 0)
            res = exp(a);
        else if (strcmp(func, "abs") == 0)
            res = fabs(a);
        else if (strcmp(func, "sinh") == 0)
            res = sinh(a);
        else if (strcmp(func, "cosh") == 0)
            res = cosh(a);
        else if (strcmp(func, "tanh") == 0)
            res = tanh(a);
        else if (strcmp(func, "floor") == 0)
            res = floor(a);
        else if (strcmp(func, "ceil") == 0)
            res = ceil(a);
        else {
            printf("Error: Unknown function '%s'\n", func);
            error_flag = 1;
            Value err = { .type = VAL_SCALAR, .scalar = 0 };
            return err;
        }
        Value ret = { .type = VAL_SCALAR, .scalar = res };
        return ret;
    } else {
        /* Matrix argument: apply element-wise */
        int rows = arg.matrix.rows, cols = arg.matrix.cols;
        Value ret = { .type = VAL_MATRIX, .matrix = { rows, cols, NULL } };
        int size = rows * cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) {
            printf("Error: Memory allocation failed\n");
            exit(1);
        }
        for (int i = 0; i < size; i++) {
            double a = arg.matrix.data[i], res;
            if (strcmp(func, "sin") == 0)
                res = sin(a);
            else if (strcmp(func, "cos") == 0)
                res = cos(a);
            else if (strcmp(func, "tan") == 0)
                res = tan(a);
            else if (strcmp(func, "asin") == 0)
                res = asin(a);
            else if (strcmp(func, "acos") == 0)
                res = acos(a);
            else if (strcmp(func, "atan") == 0)
                res = atan(a);
            else if (strcmp(func, "log") == 0)
                res = log(a);
            else if (strcmp(func, "log10") == 0)
                res = log10(a);
            else if (strcmp(func, "sqrt") == 0)
                res = sqrt(a);
            else if (strcmp(func, "exp") == 0)
                res = exp(a);
            else if (strcmp(func, "abs") == 0)
                res = fabs(a);
            else if (strcmp(func, "sinh") == 0)
                res = sinh(a);
            else if (strcmp(func, "cosh") == 0)
                res = cosh(a);
            else if (strcmp(func, "tanh") == 0)
                res = tanh(a);
            else if (strcmp(func, "floor") == 0)
                res = floor(a);
            else if (strcmp(func, "ceil") == 0)
                res = ceil(a);
            else {
                printf("Error: Unknown function '%s'\n", func);
                error_flag = 1;
                free(ret.matrix.data);
                Value err = { .type = VAL_SCALAR, .scalar = 0 };
                return err;
            }
            ret.matrix.data[i] = res;
        }
        return ret;
    }
}

/* --- Arithmetic Operation Implementations --- */

/* Addition: supports scalar+scalar, matrix+matrix (element-wise), and scalar-matrix expansion */
Value add_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        ret.type = VAL_SCALAR;
        ret.scalar = a.scalar + b.scalar;
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) {
            printf("Error: Matrix dimension mismatch in addition\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] + b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = b.matrix.rows;
        ret.matrix.cols = b.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.scalar + b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] + b.scalar;
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

Value subtract_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        ret.type = VAL_SCALAR;
        ret.scalar = a.scalar - b.scalar;
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) {
            printf("Error: Matrix dimension mismatch in subtraction\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] - b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = b.matrix.rows;
        ret.matrix.cols = b.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.scalar - b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] - b.scalar;
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

Value multiply_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        ret.type = VAL_SCALAR;
        ret.scalar = a.scalar * b.scalar;
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.cols != b.matrix.rows) {
            printf("Error: Matrix dimensions do not match for multiplication\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int m = a.matrix.rows, n = a.matrix.cols, p_ = b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = m;
        ret.matrix.cols = p_;
        ret.matrix.data = malloc(m * p_ * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < p_; j++) {
                double sum = 0;
                for (int k = 0; k < n; k++)
                    sum += a.matrix.data[i * n + k] * b.matrix.data[k * p_ + j];
                ret.matrix.data[i * p_ + j] = sum;
            }
        }
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = b.matrix.rows;
        ret.matrix.cols = b.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.scalar * b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] * b.scalar;
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

Value divide_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        if (b.scalar == 0) {
            printf("Error: Division by zero\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        ret.type = VAL_SCALAR;
        ret.scalar = a.scalar / b.scalar;
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        if (b.scalar == 0) {
            printf("Error: Division by zero (matrix divided by scalar)\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] / b.scalar;
        return ret;
    } else {
        printf("Error: Division is only supported scalar/scalar or matrix/scalar\n");
        error_flag = 1;
        return (Value){ .type = VAL_SCALAR, .scalar = 0 };
    }
}

Value power_values(Value a, Value b) {
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        return (Value){ .type = VAL_SCALAR, .scalar = pow(a.scalar, b.scalar) };
    } else {
        printf("Error: Exponentiation (^) is only supported for scalars\n");
        error_flag = 1;
        return (Value){ .type = VAL_SCALAR, .scalar = 0 };
    }
}

/* --- Element-wise Operation Implementations --- */

Value elementwise_multiply_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        return (Value){ .type = VAL_SCALAR, .scalar = a.scalar * b.scalar };
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) {
            printf("Error: Matrix dimension mismatch in element-wise multiplication\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] * b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = b.matrix.rows;
        ret.matrix.cols = b.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.scalar * b.matrix.data[i];
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] * b.scalar;
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

Value elementwise_divide_values(Value a, Value b) {
    Value ret;
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        if (b.scalar == 0) {
            printf("Error: Division by zero\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        return (Value){ .type = VAL_SCALAR, .scalar = a.scalar / b.scalar };
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) {
            printf("Error: Matrix dimension mismatch in element-wise division\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++) {
            if (b.matrix.data[i] == 0) {
                printf("Error: Division by zero in element-wise division\n");
                error_flag = 1;
                free(ret.matrix.data);
                return (Value){ .type = VAL_SCALAR, .scalar = 0 };
            }
            ret.matrix.data[i] = a.matrix.data[i] / b.matrix.data[i];
        }
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = b.matrix.rows;
        ret.matrix.cols = b.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++) {
            if (b.matrix.data[i] == 0) {
                printf("Error: Division by zero in element-wise division\n");
                error_flag = 1;
                free(ret.matrix.data);
                return (Value){ .type = VAL_SCALAR, .scalar = 0 };
            }
            ret.matrix.data[i] = a.scalar / b.matrix.data[i];
        }
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        if (b.scalar == 0) {
            printf("Error: Division by zero\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        ret.type = VAL_MATRIX;
        ret.matrix.rows = a.matrix.rows;
        ret.matrix.cols = a.matrix.cols;
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = a.matrix.data[i] / b.scalar;
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

Value elementwise_pow_values(Value a, Value b) {
    if (a.type == VAL_SCALAR && b.type == VAL_SCALAR) {
        return (Value){ .type = VAL_SCALAR, .scalar = pow(a.scalar, b.scalar) };
    } else if (a.type == VAL_MATRIX && b.type == VAL_MATRIX) {
        if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) {
            printf("Error: Matrix dimension mismatch in element-wise exponentiation\n");
            error_flag = 1;
            return (Value){ .type = VAL_SCALAR, .scalar = 0 };
        }
        int size = a.matrix.rows * a.matrix.cols;
        Value ret = { .type = VAL_MATRIX, .matrix = { a.matrix.rows, a.matrix.cols, NULL } };
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = pow(a.matrix.data[i], b.matrix.data[i]);
        return ret;
    } else if (a.type == VAL_SCALAR && b.type == VAL_MATRIX) {
        int size = b.matrix.rows * b.matrix.cols;
        Value ret = { .type = VAL_MATRIX, .matrix = { b.matrix.rows, b.matrix.cols, NULL } };
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = pow(a.scalar, b.matrix.data[i]);
        return ret;
    } else if (a.type == VAL_MATRIX && b.type == VAL_SCALAR) {
        int size = a.matrix.rows * a.matrix.cols;
        Value ret = { .type = VAL_MATRIX, .matrix = { a.matrix.rows, a.matrix.cols, NULL } };
        ret.matrix.data = malloc(size * sizeof(double));
        if (!ret.matrix.data) { printf("Error: Memory allocation failed\n"); exit(1); }
        for (int i = 0; i < size; i++)
            ret.matrix.data[i] = pow(a.matrix.data[i], b.scalar);
        return ret;
    }
    return (Value){ .type = VAL_SCALAR, .scalar = 0 };
}

/* --- Main REPL Loop --- */
int main(int argc, char *argv[]) {
    int interactive = 1;
    char line[256]; // used for script input
    
    /* If a script file is provided, use it as input */
    if (argc > 1) {
        if (freopen(argv[1], "r", stdin) == NULL) {
            perror("Error opening script file");
            exit(1);
        }
        interactive = 0;
    }

    printf("Welcome to Extended CMath - Math Interpreter with Basic Matrix Support.\n");
    printf("Type 'help' for instructions, 'exit' or 'quit' to leave.\n");

    while (1) {
        char *input;
        if (interactive) {
            input = get_line();
            if (!input)
                break;
        } else {
            if (!fgets(line, sizeof(line), stdin))
                break;
            line[strcspn(line, "\n")] = '\0';
            input = line;
        }
        
        if (strlen(input) == 0)
            continue;
        
        /* --- Check for trailing semicolon --- */
        int suppress_output = 0;
        {
            int len = strlen(input);
            int i = len - 1;
            while (i >= 0 && isspace((unsigned char)input[i]))
                i--;
            if (i >= 0 && input[i] == ';') {
                suppress_output = 1;
                input[i] = '\0';  // Remove the trailing semicolon.
            }
        }
        
        /* --- Handle "print" command --- */
        if (strncmp(input, "print", 5) == 0 && (input[5] == ' ' || input[5] == '\t' || input[5] == '\0')) {
            const char *str_ptr = input + 5;
            while (*str_ptr && isspace((unsigned char)*str_ptr))
                str_ptr++;
            if (*str_ptr == '"') {
                str_ptr++;
                char *end_quote = strchr(str_ptr, '"');
                if (end_quote == NULL) {
                    printf("Error: Unterminated string literal in print command\n");
                } else {
                    int len_str = end_quote - str_ptr;
                    char *msg = malloc(len_str + 1);
                    if (msg) {
                        strncpy(msg, str_ptr, len_str);
                        msg[len_str] = '\0';
                        if (!suppress_output)
                            printf("%s\n", msg);
                        free(msg);
                    }
                }
            } else {
                printf("Error: Expected string literal after print command\n");
            }
            continue;
        }
        
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0)
            break;
        if (strcmp(input, "help") == 0) {
            print_help();
            if (interactive && strlen(input) > 0) {
                if (history_count < MAX_HISTORY) {
                    history[history_count++] = strdup(input);
                } else {
                    free(history[0]);
                    memmove(history, history+1, sizeof(char*)*(MAX_HISTORY-1));
                    history[MAX_HISTORY-1] = strdup(input);
                }
            }
            continue;
        }
        if (strcmp(input, "list") == 0) {
            list_variables();
            if (interactive && strlen(input) > 0) {
                if (history_count < MAX_HISTORY) {
                    history[history_count++] = strdup(input);
                } else {
                    free(history[0]);
                    memmove(history, history+1, sizeof(char*)*(MAX_HISTORY-1));
                    history[MAX_HISTORY-1] = strdup(input);
                }
            }
            continue;
        }

        /* Process assignment or expression evaluation */
        p = input;
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
                Value result = parse_expression();
                if (error_flag) {
                    error_flag = 0;
                    continue;
                }
                /* Store variable (ownership of result.data transfers here) */
                set_variable(ident, result);
                if (!suppress_output) {
                    printf("%s = ", ident);
                    print_value(result);
                }
                if (interactive && strlen(input) > 0) {
                    if (history_count < MAX_HISTORY) {
                        history[history_count++] = strdup(input);
                    } else {
                        free(history[0]);
                        memmove(history, history+1, sizeof(char*)*(MAX_HISTORY-1));
                        history[MAX_HISTORY-1] = strdup(input);
                    }
                }
                continue;
            } else {
                p = start;
            }
        }
        
        /* Expression evaluation */
        Value result = parse_expression();
        if (error_flag) {
            error_flag = 0;
            continue;
        }
        if (!suppress_output) {
            print_value(result);
        }
        if (result.type == VAL_MATRIX)
            free_value(&result);
        
        if (interactive && strlen(input) > 0) {
            if (history_count < MAX_HISTORY) {
                history[history_count++] = strdup(input);
            } else {
                free(history[0]);
                memmove(history, history+1, sizeof(char*)*(MAX_HISTORY-1));
                history[MAX_HISTORY-1] = strdup(input);
            }
        }
    }
    printf("Goodbye.\n");
    return 0;
}
