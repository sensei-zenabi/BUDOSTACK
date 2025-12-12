#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../lib/retroprofile.h"

static void usage(void) {
    fprintf(stderr,
            "Usage: _RETROPROFILE <command> [profile]\n"
            "Commands:\n"
            "  list               List available profiles.\n"
            "  show <profile>     Show palette values and a color swatch.\n"
            "  apply <profile>    Emit OSC 4/10/11/12 escapes to set palette and defaults.\n"
            "  reset              Reset palette and defaults (OSC 104/110/111/112).\n"
            "\nProfiles are case-insensitive. Redirect output from 'apply' into your shell\n"
            "if you want to persist the palette, e.g. _RETROPROFILE apply c64 > /tmp/palette && cat /tmp/palette.\n");
}

static void list_profiles(void) {
    const RetroProfile *active = retroprofile_active();
    size_t count = retroprofile_count();
    for (size_t i = 0; i < count; ++i) {
        const RetroProfile *profile = retroprofile_get(i);
        if (profile == NULL)
            continue;
        const char *marker = (profile == active) ? "*" : " ";
        printf("%s %-12s %s\n", marker, profile->key, profile->display_name);
        printf("    %s\n", profile->description);
    }
}

static const RetroProfile *find_profile(const char *key) {
    return retroprofile_find(key);
}

static void show_profile(const RetroProfile *profile) {
    const RetroProfile *active = retroprofile_active();
    const char *status = (profile == active) ? " [active]" : "";
    printf("%s (%s)%s\n", profile->display_name, profile->key, status);
    printf("%s\n\n", profile->description);
    printf("Defaults: foreground #%02X%02X%02X, background #%02X%02X%02X, cursor #%02X%02X%02X\n\n",
           profile->defaults.foreground.r,
           profile->defaults.foreground.g,
           profile->defaults.foreground.b,
           profile->defaults.background.r,
           profile->defaults.background.g,
           profile->defaults.background.b,
           profile->defaults.cursor.r,
           profile->defaults.cursor.g,
           profile->defaults.cursor.b);
    for (int i = 0; i < 16; ++i) {
        const RetroColor *color = &profile->colors[i];
        printf("%2d  #%02X%02X%02X  \x1b[48;2;%d;%d;%dm  \x1b[0m\n",
               i,
               color->r,
               color->g,
               color->b,
               color->r,
               color->g,
               color->b);
    }
}

static void emit_osc(const char *fmt, ...) {
    char buffer[128];
    va_list args;

    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written < 0)
        return;

    fwrite("\033]", 1, 2, stdout);
    if ((size_t)written >= sizeof(buffer)) {
        fwrite(buffer, 1, sizeof(buffer) - 1, stdout);
    } else {
        fwrite(buffer, 1, (size_t)written, stdout);
    }
    fwrite("\033\\", 1, 2, stdout);
}

static void emit_palette_sequence(const RetroProfile *profile) {
    for (int i = 0; i < 16; ++i) {
        const RetroColor *color = &profile->colors[i];
        emit_osc("4;%d;#%02X%02X%02X", i, color->r, color->g, color->b);
    }
    emit_osc("10;#%02X%02X%02X",
             profile->defaults.foreground.r,
             profile->defaults.foreground.g,
             profile->defaults.foreground.b);
    emit_osc("11;#%02X%02X%02X",
             profile->defaults.background.r,
             profile->defaults.background.g,
             profile->defaults.background.b);
    emit_osc("12;#%02X%02X%02X",
             profile->defaults.cursor.r,
             profile->defaults.cursor.g,
             profile->defaults.cursor.b);
    fflush(stdout);
    fprintf(stderr,
            "Applied '%s' palette to terminal (OSC 4/10/11/12). \nUse 'reset' to restore defaults.\n",
            profile->display_name);
}

static void reset_palette(void) {
    emit_osc("104;");
    emit_osc("110;");
    emit_osc("111;");
    emit_osc("112;");
    fflush(stdout);
    fprintf(stderr, "Requested terminal palette/default reset via OSC 104/110/111/112.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "list") == 0) {
        list_profiles();
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "_RETROPROFILE: missing profile for 'show' command.\n");
            return EXIT_FAILURE;
        }
        const RetroProfile *profile = find_profile(argv[2]);
        if (profile == NULL) {
            fprintf(stderr, "_RETROPROFILE: unknown profile '%s'.\n", argv[2]);
            return EXIT_FAILURE;
        }
        show_profile(profile);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "apply") == 0) {
        if (argc < 3) {
            fprintf(stderr, "_RETROPROFILE: missing profile for 'apply' command.\n");
            return EXIT_FAILURE;
        }
        const RetroProfile *profile = find_profile(argv[2]);
        if (profile == NULL) {
            fprintf(stderr, "_RETROPROFILE: unknown profile '%s'.\n", argv[2]);
            return EXIT_FAILURE;
        }
        if (retroprofile_set_active(profile->key) != 0)
            fprintf(stderr, "_RETROPROFILE: warning: failed to persist active profile selection.\n");
        emit_palette_sequence(profile);
        return EXIT_SUCCESS;
    }

    if (strcmp(argv[1], "reset") == 0) {
        if (retroprofile_clear_active() != 0)
            fprintf(stderr, "_RETROPROFILE: warning: failed to clear stored active profile.\n");
        reset_palette();
        return EXIT_SUCCESS;
    }

    usage();
    return EXIT_FAILURE;
}
