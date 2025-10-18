#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int is_only_whitespace(const char *s) {
    if (!s)
        return 1;

    while (*s != '\0') {
        if (!isspace((unsigned char)*s))
            return 0;
        ++s;
    }

    return 1;
}

int main(void) {
    long long commit_count = 0;
    int have_value = 0;
    FILE *pipe = popen("git rev-list --count HEAD 2>/dev/null", "r");

    if (pipe != NULL) {
        char buffer[128];

        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            char *endptr = NULL;

            errno = 0;
            long long value = strtoll(buffer, &endptr, 10);

            if (errno == 0 && endptr != buffer) {
                if (endptr != NULL && *endptr != '\0' && !is_only_whitespace(endptr)) {
                    have_value = 0;
                } else if (value >= 0) {
                    commit_count = value;
                    have_value = 1;
                }
            }
        }

        pclose(pipe);
    }

    if (!have_value)
        commit_count = 0;

    printf("%lld\n", commit_count);

    return 0;
}
