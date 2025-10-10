#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define MAX_DICE 100U

static void print_help(void);
static int parse_dice_notation(const char *input, unsigned int *count, unsigned int *sides);
static int is_supported_sides(unsigned int sides);
static unsigned int roll_die(unsigned int sides);

static void print_help(void) {
    printf("Usage: _DICE <dice>\n");
    printf("Roll standard Dungeons & Dragons dice.\n\n");
    printf("Examples:\n");
    printf("  _DICE 1d6   # roll one six-sided die\n");
    printf("  _DICE 2d20  # roll two twenty-sided dice\n");
    printf("\nSupported dice sizes: d4, d6, d8, d10, d12, d20, d100\n");
}

static int parse_dice_notation(const char *input, unsigned int *count, unsigned int *sides) {
    if (input == NULL || count == NULL || sides == NULL) {
        return -1;
    }

    if (*input == '\0') {
        fprintf(stderr, "_DICE: dice notation is empty\n");
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long count_value = strtoul(input, &endptr, 10);

    if (errno != 0 || endptr == input) {
        fprintf(stderr, "_DICE: invalid dice count in '%s'\n", input);
        return -1;
    }

    if (count_value == 0UL || count_value > MAX_DICE) {
        fprintf(stderr, "_DICE: dice count must be between 1 and %u\n", MAX_DICE);
        return -1;
    }

    if (*endptr != 'd' && *endptr != 'D') {
        fprintf(stderr, "_DICE: expected 'd' after dice count in '%s'\n", input);
        return -1;
    }

    const char *sides_str = endptr + 1;
    if (*sides_str == '\0') {
        fprintf(stderr, "_DICE: missing dice sides in '%s'\n", input);
        return -1;
    }

    errno = 0;
    char *sides_end = NULL;
    unsigned long sides_value = strtoul(sides_str, &sides_end, 10);

    if (errno != 0 || sides_end == sides_str || *sides_end != '\0') {
        fprintf(stderr, "_DICE: invalid dice sides in '%s'\n", input);
        return -1;
    }

    if (sides_value == 0UL || sides_value > UINT_MAX) {
        fprintf(stderr, "_DICE: dice sides out of range in '%s'\n", input);
        return -1;
    }

    unsigned int sides_unsigned = (unsigned int)sides_value;

    if (is_supported_sides(sides_unsigned) == 0) {
        fprintf(stderr, "_DICE: unsupported dice d%u\n", sides_unsigned);
        fprintf(stderr, "Supported dice sizes: d4, d6, d8, d10, d12, d20, d100\n");
        return -1;
    }

    *count = (unsigned int)count_value;
    *sides = sides_unsigned;
    return 0;
}

static int is_supported_sides(unsigned int sides) {
    switch (sides) {
    case 4U:
    case 6U:
    case 8U:
    case 10U:
    case 12U:
    case 20U:
    case 100U:
        return 1;
    default:
        return 0;
    }
}

static unsigned int roll_die(unsigned int sides) {
    unsigned long rand_range = (unsigned long)RAND_MAX + 1UL;
    unsigned long limit = rand_range - (rand_range % (unsigned long)sides);
    unsigned long value;

    if (limit == 0UL) {
        /* Fallback for unexpected environments where RAND_MAX < sides. */
        return (unsigned int)(rand() % (int)sides) + 1U;
    }

    do {
        value = (unsigned long)rand();
    } while (value >= limit);

    return (unsigned int)(value % (unsigned long)sides) + 1U;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        print_help();
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: _DICE <dice>\n");
        return 1;
    }

    unsigned int count = 0U;
    unsigned int sides = 0U;

    if (parse_dice_notation(argv[1], &count, &sides) != 0) {
        return 1;
    }

    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    srand(seed);

    unsigned int total = 0U;
    for (unsigned int i = 0U; i < count; ++i) {
        unsigned int roll = roll_die(sides);
        total += roll;
    }

    printf("%u\n", total);
    return 0;
}

