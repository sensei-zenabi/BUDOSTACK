#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/kd.h>

static void sleep_ms(unsigned int milliseconds) {
    struct timespec request = {milliseconds / 1000U, (long)(milliseconds % 1000U) * 1000000L};
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
        /* retry until the full duration has elapsed */
    }
}

static int parse_duration(const char *arg, unsigned int *duration_ms) {
    if (arg == NULL || duration_ms == NULL) {
        return -1;
    }

    if (*arg == '\0') {
        fprintf(stderr, "_BEEP: duration is empty\n");
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    unsigned long value = strtoul(arg, &endptr, 10);

    if (errno != 0 || endptr == arg || *endptr != '\0') {
        fprintf(stderr, "_BEEP: invalid duration '%s'\n", arg);
        return -1;
    }

    if (value == 0 || value > UINT_MAX) {
        fprintf(stderr, "_BEEP: duration out of range '%s'\n", arg);
        return -1;
    }

    *duration_ms = (unsigned int)value;
    return 0;
}

static int parse_note(const char *input, double *frequency) {
    if (input == NULL || frequency == NULL) {
        return -1;
    }

    size_t len = strlen(input);
    if (len < 2) {
        fprintf(stderr, "_BEEP: note is too short '%s'\n", input);
        return -1;
    }

    char letter = (char)toupper((unsigned char)input[0]);
    int semitone = 0;

    switch (letter) {
    case 'C':
        semitone = 0;
        break;
    case 'D':
        semitone = 2;
        break;
    case 'E':
        semitone = 4;
        break;
    case 'F':
        semitone = 5;
        break;
    case 'G':
        semitone = 7;
        break;
    case 'A':
        semitone = 9;
        break;
    case 'B':
        semitone = 11;
        break;
    default:
        fprintf(stderr, "_BEEP: unknown note letter '%c'\n", input[0]);
        return -1;
    }

    size_t index = 1;
    int octave_adjust = 0;

    if (index < len && input[index] == '#') {
        ++semitone;
        ++index;
        if (semitone >= 12) {
            semitone -= 12;
            ++octave_adjust;
        }
    } else if (index < len && input[index] == 'b') {
        --semitone;
        ++index;
        if (semitone < 0) {
            semitone += 12;
            --octave_adjust;
        }
    }

    if (index >= len) {
        fprintf(stderr, "_BEEP: octave missing in '%s'\n", input);
        return -1;
    }

    errno = 0;
    char *endptr = NULL;
    long octave = strtol(input + index, &endptr, 10);
    if (errno != 0 || endptr == input + index || *endptr != '\0') {
        fprintf(stderr, "_BEEP: invalid octave in '%s'\n", input);
        return -1;
    }

    octave += octave_adjust;

    if (octave < -1 || octave > 9) {
        fprintf(stderr, "_BEEP: octave out of supported range in '%s'\n", input);
        return -1;
    }

    int midi_note = (int)(12 * (octave + 1) + semitone);
    double exponent = ((double)midi_note - 69.0) / 12.0;
    *frequency = 440.0 * pow(2.0, exponent);
    return 0;
}

static int send_tone_ioctl(int fd, unsigned int divisor, unsigned int duration) {
    unsigned long argument = ((unsigned long)duration << 16) | (unsigned long)(divisor & 0xFFFFU);
    if (ioctl(fd, KDMKTONE, argument) == 0) {
        sleep_ms(duration);
        ioctl(fd, KDMKTONE, 0);
        return 0;
    }
    return -1;
}

static int play_tone(double frequency, unsigned int duration_ms) {
    if (frequency <= 0.0) {
        fprintf(stderr, "_BEEP: invalid frequency %.2f\n", frequency);
        return -1;
    }

    unsigned int divisor = 0U;
    double raw_divisor = 1193180.0 / frequency;
    if (raw_divisor < 1.0) {
        divisor = 1U;
    } else if (raw_divisor > 65535.0) {
        divisor = 65535U;
    } else {
        divisor = (unsigned int)lround(raw_divisor);
        if (divisor == 0U) {
            divisor = 1U;
        }
    }

    unsigned int duration = duration_ms > 0xFFFFU ? 0xFFFFU : duration_ms;

    int fds[6];
    size_t fd_count = 0;
    fds[fd_count++] = STDOUT_FILENO;
    fds[fd_count++] = STDERR_FILENO;
    fds[fd_count++] = STDIN_FILENO;

    int fallback_fd = -1;
    for (size_t i = 0; i < fd_count; ++i) {
        if (fallback_fd < 0 && isatty(fds[i])) {
            fallback_fd = fds[i];
        }
    }

    const char *paths[] = {"/dev/console", "/dev/tty0", "/dev/tty"};
    int extra_fds[3];
    size_t extra_count = 0;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        int fd = open(paths[i], O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            extra_fds[extra_count++] = fd;
            if (fd_count < sizeof(fds) / sizeof(fds[0])) {
                fds[fd_count++] = fd;
            }
            if (fallback_fd < 0 && isatty(fd)) {
                fallback_fd = fd;
            }
        }
    }

    int result = -1;
    for (size_t i = 0; i < fd_count; ++i) {
        if (send_tone_ioctl(fds[i], divisor, duration) == 0) {
            result = 0;
            break;
        }
    }

    if (result != 0) {
        fprintf(stderr, "_BEEP: unable to access PC speaker, using terminal bell as fallback\n");
        int tty_fd = -1;
        ssize_t written = -1;

        if (fallback_fd >= 0) {
            written = write(fallback_fd, "\a", 1);
            if (fallback_fd == STDOUT_FILENO) {
                fflush(stdout);
            } else if (fallback_fd == STDERR_FILENO) {
                fflush(stderr);
            }
        }

        if (written != 1) {
            tty_fd = open("/dev/tty", O_WRONLY | O_CLOEXEC);
            if (tty_fd >= 0) {
                (void)write(tty_fd, "\a", 1);
            }
        }

        if (tty_fd >= 0) {
            close(tty_fd);
        }

        for (size_t i = 0; i < extra_count; ++i) {
            close(extra_fds[i]);
        }
        sleep_ms(duration_ms);
        return 0;
    }

    for (size_t i = 0; i < extra_count; ++i) {
        close(extra_fds[i]);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: _BEEP -<note> -<duration_ms>\n");
        return EXIT_FAILURE;
    }

    const char *note_arg = argv[1];
    const char *duration_arg = argv[2];

    if (note_arg[0] != '-' || note_arg[1] == '\0') {
        fprintf(stderr, "_BEEP: note argument must be in the format -<note>\n");
        return EXIT_FAILURE;
    }

    if (duration_arg[0] != '-' || duration_arg[1] == '\0') {
        fprintf(stderr, "_BEEP: duration argument must be in the format -<duration_ms>\n");
        return EXIT_FAILURE;
    }

    double frequency = 0.0;
    unsigned int duration_ms = 0;

    if (parse_note(note_arg + 1, &frequency) != 0) {
        return EXIT_FAILURE;
    }

    if (parse_duration(duration_arg + 1, &duration_ms) != 0) {
        return EXIT_FAILURE;
    }

    if (play_tone(frequency, duration_ms) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
