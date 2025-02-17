/*
 * runtask.c - A simple script engine for executing terminal commands.
 *
 * Description:
 *   This script engine reads a task file (e.g., mytask.task) and interprets its
 *   commands. Supported commands include:
 *
 *     - Labels: "<number>," to mark a line for GOTO operations.
 *     - WAIT X: Delays execution for X milliseconds.
 *     - PRINT "X": Prints the string X to the terminal.
 *     - RUN X: Executes an external command (e.g., ./myapp).
 *     - GOTO X: Jumps to the line marked with label X.
 *     - BREAK: Exits the current script execution.
 *     - VAR X: Defines an integer variable X.
 *     - X = N: Assigns the integer N to variable X.
 *     - X++ / X--: Increments or decrements variable X by one.
 *     - EXIT: Ends script execution (prints a newline and returns control to the terminal).
 *
 *   Additionally, conditional logic is supported with the following syntax:
 *
 *     IF <cond> THEN       : Begins an IF block. <cond> can be a simple token (variable or integer)
 *                             or a comparison expression (using operators ==, !=, <, <=, >, >=).
 *     ELSE_IF <cond>       : Optional additional condition if previous IF/ELSE_IF is false.
 *     END_IF               : Ends an IF block.
 *
 *   The engine supports both simple conditions and binary comparison expressions.
 *
 * Compilation:
 *   gcc -std=c11 -o runtask runtask.c
 *
 * Usage:
 *   ./runtask mytask.task
 *   ./runtask -help   (to display the help message)
 *
 * Future Enhancements:
 *   This file is structured modularly to allow for easy extension. In the future,
 *   you might add support for more complex expressions, nested IF blocks, functions,
 *   error reporting, and additional commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// ---------------------------
// Helper: Delay Functions
// ---------------------------
void delay(double seconds) {
    clock_t start_time = clock();
    while ((double)(clock() - start_time) / CLOCKS_PER_SEC < seconds) {
        // Busy waiting
    }
}

void delay_ms(int ms) {
    delay(ms / 1000.0);
}

// ---------------------------
// Help Message Function
// ---------------------------
void print_help() {
    printf("Script Engine Help:\n");
    printf("Usage: runtask scriptfile\n\n");
    printf("Available Commands:\n");
    printf("  <number>,         : Label for GOTO (e.g. 10,)\n");
    printf("  WAIT X            : Waits for X milliseconds\n");
    printf("  PRINT \"X\"         : Prints string X\n");
    printf("  RUN X             : Executes executable X (e.g. ./myapp)\n");
    printf("  GOTO X            : Jumps to the line containing label X\n");
    printf("  BREAK             : Breaks out of a GOTO loop\n");
    printf("  VAR X             : Defines integer variable X\n");
    printf("  X = N             : Assigns integer N to variable X\n");
    printf("  X++               : Increments variable X by one\n");
    printf("  X--               : Decrements variable X by one\n");
    printf("  EXIT              : Ends script execution, outputs newline, returns to terminal\n");
    printf("\n");
    printf("Conditional Syntax:\n");
    printf("  IF <cond> THEN    : Begins an IF block. <cond> can be:\n");
    printf("                     - A simple token (variable name or integer literal), or\n");
    printf("                     - A comparison expression using operators: ==, !=, <, <=, >, >=\n");
    printf("                     Example: IF X > 5 THEN\n");
    printf("  ELSE_IF <cond>    : Optional additional condition if the previous IF/ELSE_IF was false.\n");
    printf("  END_IF            : Ends an IF block.\n");
    printf("\n");
}

// ---------------------------
// Data Structures for Variables and Labels
// ---------------------------
#define MAX_VARS 100
#define MAX_LABELS 100
#define MAX_LINE_LENGTH 256
#define MAX_LINES 1024

typedef struct {
    char name[32];
    int value;
} Variable;

typedef struct {
    int label;       // Numeric label, e.g., 10
    int lineIndex;   // Corresponding line in the script array
} Label;

Variable variables[MAX_VARS];
int varCount = 0;

Label labels[MAX_LABELS];
int labelCount = 0;

// ---------------------------
// Utility Functions
// ---------------------------
char* trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0)
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

Variable* get_variable(const char *name) {
    for (int i = 0; i < varCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return &variables[i];
        }
    }
    return NULL;
}

Variable* create_variable(const char *name) {
    if (varCount >= MAX_VARS) {
        printf("Variable limit reached.\n");
        return NULL;
    }
    strncpy(variables[varCount].name, name, sizeof(variables[varCount].name) - 1);
    variables[varCount].name[sizeof(variables[varCount].name) - 1] = '\0';
    variables[varCount].value = 0;
    return &variables[varCount++];
}

// Evaluate a simple condition token (if no operator is found).
int evaluate_condition(const char *token) {
    int val = 0;
    if (isdigit(token[0]) || (token[0]=='-' && isdigit(token[1]))) {
        val = atoi(token);
    } else {
        Variable *var = get_variable(token);
        if (var) {
            val = var->value;
        }
    }
    return (val != 0);
}

// ---------------------------
// Expression Evaluation for IF Conditions
// ---------------------------
int evaluate_expression(const char *expr) {
    char exprCopy[256];
    strncpy(exprCopy, expr, sizeof(exprCopy) - 1);
    exprCopy[sizeof(exprCopy)-1] = '\0';
    char *trimmed = trim(exprCopy);

    // Look for comparison operators in order of length
    char *op = NULL;
    int opLen = 0;
    if ((op = strstr(trimmed, "==")) != NULL) { opLen = 2; }
    else if ((op = strstr(trimmed, "!=")) != NULL) { opLen = 2; }
    else if ((op = strstr(trimmed, ">=")) != NULL) { opLen = 2; }
    else if ((op = strstr(trimmed, "<=")) != NULL) { opLen = 2; }
    else if ((op = strstr(trimmed, ">")) != NULL)  { opLen = 1; }
    else if ((op = strstr(trimmed, "<")) != NULL)  { opLen = 1; }
    else {
        return evaluate_condition(trimmed);
    }
    
    // Split expression into left and right tokens.
    char left[128], right[128];
    int leftLen = op - trimmed;
    strncpy(left, trimmed, leftLen);
    left[leftLen] = '\0';
    strncpy(right, op + opLen, sizeof(right) - 1);
    right[sizeof(right)-1] = '\0';
    char *leftTrim = trim(left);
    char *rightTrim = trim(right);

    int leftVal = 0, rightVal = 0;
    if (isdigit(leftTrim[0]) || (leftTrim[0]=='-' && isdigit(leftTrim[1])))
        leftVal = atoi(leftTrim);
    else {
        Variable *var = get_variable(leftTrim);
        if (var) leftVal = var->value;
        else { printf("Variable %s not defined in expression.\n", leftTrim); }
    }
    if (isdigit(rightTrim[0]) || (rightTrim[0]=='-' && isdigit(rightTrim[1])))
        rightVal = atoi(rightTrim);
    else {
        Variable *var = get_variable(rightTrim);
        if (var) rightVal = var->value;
        else { printf("Variable %s not defined in expression.\n", rightTrim); }
    }
    
    if (strncmp(op, "==", opLen) == 0) return (leftVal == rightVal);
    if (strncmp(op, "!=", opLen) == 0) return (leftVal != rightVal);
    if (strncmp(op, ">=", opLen) == 0) return (leftVal >= rightVal);
    if (strncmp(op, "<=", opLen) == 0) return (leftVal <= rightVal);
    if (strncmp(op, ">", opLen) == 0)  return (leftVal > rightVal);
    if (strncmp(op, "<", opLen) == 0)  return (leftVal < rightVal);
    
    return 0;
}

// ---------------------------
// Script Execution Functions
// ---------------------------
char *scriptLines[MAX_LINES];
int totalLines = 0;

void preprocess_labels() {
    for (int i = 0; i < totalLines; i++) {
        char *line = trim(scriptLines[i]);
        if (isdigit(line[0])) {
            int lbl;
            if (sscanf(line, "%d,", &lbl) == 1) {
                if (labelCount < MAX_LABELS) {
                    labels[labelCount].label = lbl;
                    labels[labelCount].lineIndex = i;
                    labelCount++;
                }
            }
        }
    }
}

int find_label_line(int label) {
    for (int i = 0; i < labelCount; i++) {
        if (labels[i].label == label)
            return labels[i].lineIndex;
    }
    return -1;
}

int execute_line(int index); // Forward declaration

int process_if_block(int startIndex) {
    int conditionMet = 0;
    int i = startIndex;
    char lineBuf[MAX_LINE_LENGTH];
    char conditionExpr[256];
    
    strncpy(lineBuf, trim(scriptLines[i]), sizeof(lineBuf)-1);
    lineBuf[sizeof(lineBuf)-1] = '\0';
    if (sscanf(lineBuf, "IF %255[^\n]", conditionExpr) == 1) {
        char *thenPos = strstr(conditionExpr, "THEN");
        if (thenPos) *thenPos = '\0';
        if (evaluate_expression(trim(conditionExpr)))
            conditionMet = 1;
    }
    i++;
    
    while (i < totalLines) {
        char *currLine = trim(scriptLines[i]);
        if (strncmp(currLine, "END_IF", 6) == 0) {
            return i;
        } else if (strncmp(currLine, "ELSE_IF", 7) == 0) {
            if (conditionMet) {
                i++;
                while (i < totalLines && strncmp(trim(scriptLines[i]), "END_IF", 6) != 0)
                    i++;
                return i;
            } else {
                strncpy(lineBuf, currLine, sizeof(lineBuf)-1);
                lineBuf[sizeof(lineBuf)-1] = '\0';
                if (sscanf(lineBuf, "ELSE_IF %255[^\n]", conditionExpr) == 1) {
                    char *thenPos = strstr(conditionExpr, "THEN");
                    if (thenPos) *thenPos = '\0';
                    if (evaluate_expression(trim(conditionExpr))) {
                        conditionMet = 1;
                    }
                }
            }
        } else {
            if (conditionMet) {
                execute_line(i);
            }
        }
        i++;
    }
    return i;
}

int execute_line(int index) {
    char lineBuf[MAX_LINE_LENGTH];
    strncpy(lineBuf, scriptLines[index], MAX_LINE_LENGTH - 1);
    lineBuf[MAX_LINE_LENGTH - 1] = '\0';
    char *line = trim(lineBuf);
    
    if (line[0] == '\0' || isdigit(line[0]))
        return index + 1;
    
    if (strncmp(line, "WAIT", 4) == 0) {
        int ms = 0;
        if (sscanf(line, "WAIT %d", &ms) == 1)
            delay_ms(ms);
    }
    else if (strncmp(line, "PRINT", 5) == 0) {
        char str[256] = "";
        char *start = strchr(line, '\"');
        if (start) {
            start++;
            char *end = strchr(start, '\"');
            if (end) {
                size_t len = end - start;
                if (len >= sizeof(str)) len = sizeof(str)-1;
                strncpy(str, start, len);
                str[len] = '\0';
            }
        }
        printf("%s", str);
        fflush(stdout);
    }
    else if (strncmp(line, "RUN", 3) == 0) {
        char cmd[256];
        if (sscanf(line, "RUN %255s", cmd) == 1)
            system(cmd);
    }
    else if (strncmp(line, "GOTO", 4) == 0) {
        int lbl;
        if (sscanf(line, "GOTO %d", &lbl) == 1) {
            int target = find_label_line(lbl);
            if (target >= 0)
                return target;
            else
                printf("Label %d not found.\n", lbl);
        }
    }
    else if (strncmp(line, "BREAK", 5) == 0) {
        return totalLines;
    }
    else if (strncmp(line, "VAR", 3) == 0) {
        char varName[32];
        if (sscanf(line, "VAR %31s", varName) == 1) {
            if (!get_variable(varName))
                create_variable(varName);
        }
    }
    else if (strchr(line, '=') != NULL) {
        char varName[32];
        int newVal;
        if (sscanf(line, "%31s = %d", varName, &newVal) == 2) {
            Variable *var = get_variable(varName);
            if (var)
                var->value = newVal;
            else
                printf("Variable %s not defined.\n", varName);
        }
    }
    else if (strstr(line, "++") != NULL) {
        char varName[32];
        if (sscanf(line, "%31[^+]", varName) == 1) {
            Variable *var = get_variable(trim(varName));
            if (var) var->value++;
            else printf("Variable %s not defined.\n", varName);
        }
    }
    else if (strstr(line, "--") != NULL) {
        char varName[32];
        if (sscanf(line, "%31[^-]", varName) == 1) {
            Variable *var = get_variable(trim(varName));
            if (var) var->value--;
            else printf("Variable %s not defined.\n", varName);
        }
    }
    else if (strncmp(line, "EXIT", 4) == 0) {
        printf("\n");
        return totalLines;
    }
    else if (strncmp(line, "IF", 2) == 0) {
        int newIndex = process_if_block(index);
        return newIndex + 1;
    }
    
    return index + 1;
}

// ---------------------------
// Main Function: Script Engine
// ---------------------------
int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }
    
    if (argc < 2) {
        printf("Usage: %s scriptfile\n", argv[0]);
        return 1;
    }
    
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("Error opening script file");
        return 1;
    }
    
    char buffer[MAX_LINE_LENGTH];
    totalLines = 0;
    while (fgets(buffer, sizeof(buffer), fp) && totalLines < MAX_LINES) {
        scriptLines[totalLines] = strdup(buffer);
        totalLines++;
    }
    fclose(fp);
    
    preprocess_labels();
    
    int currentLine = 0;
    while (currentLine < totalLines)
        currentLine = execute_line(currentLine);
    
    for (int i = 0; i < totalLines; i++)
        free(scriptLines[i]);
    
    return 0;
}
