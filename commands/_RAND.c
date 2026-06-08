#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int parse_long(const char *text, const char *label, long *out_value) {
    char *endptr = NULL;

    if (text == NULL || label == NULL || out_value == NULL) {
        fprintf(stderr, "_RAND: internal parser error\n");
        return -1;
    }

    errno = 0;
    long parsed = strtol(text, &endptr, 10);

    if (errno != 0 || endptr == text || *endptr != '\0') {
        fprintf(stderr, "_RAND: invalid integer for %s: '%s'\n", label, text);
        return -1;
    }

    *out_value = parsed;
    return 0;
}

static unsigned long bounded_random(unsigned long range) {
    unsigned long random_range = (unsigned long)RAND_MAX + 1UL;
    unsigned long limit = random_range - (random_range % range);
    unsigned long value = 0UL;

    if (limit == 0UL) {
        return (unsigned long)(rand() % (int)range);
    }

    do {
        value = (unsigned long)rand();
    } while (value >= limit);

    return value % range;
}

int main(int argc, char *argv[]) {
    long min = 0L;
    long max = LONG_MAX;

    if (argc == 1) {
        min = 0L;
        max = 100L;
    } else if (argc == 3) {
        if (parse_long(argv[1], "min", &min) != 0)
            return EXIT_FAILURE;
        if (parse_long(argv[2], "max", &max) != 0)
            return EXIT_FAILURE;
    } else {
        fprintf(stderr, "Usage: _RAND [min max]\n");
        return EXIT_FAILURE;
    }

    if (min > max) {
        fprintf(stderr, "_RAND: min must be less than or equal to max\n");
        return EXIT_FAILURE;
    }

    unsigned long seed = (unsigned long)time(NULL) ^ (unsigned long)getpid();
    srand((unsigned int)seed);

    if (min == max) {
        printf("%ld\n", min);
        return EXIT_SUCCESS;
    }

    unsigned long span = (unsigned long)(max - min) + 1UL;
    long result = min + (long)bounded_random(span);

    printf("%ld\n", result);
    return EXIT_SUCCESS;
}
