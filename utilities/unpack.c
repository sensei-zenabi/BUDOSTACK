/*
 * unpack.c - Unpacks a supported archive to a directory alongside the archive.
 *
 * Design:
 * - Uses the standard library only.
 * - Validates command-line arguments and prints usage instructions if incorrect or "-help" is given.
 * - Determines a target directory by stripping the archive extension.
 * - Detects supported archive formats (.zip, .7z, and tar family variants).
 * - Ensures the target directory exists before extraction.
 * - Constructs the appropriate command to invoke the system archiver.
 * - Uses system() to call the external extraction command.
 * - Compiled as plain C (C11, no additional libraries).
 *
 * Compile with: gcc -std=c11 -o unpack unpack.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

enum archive_type {
    ARCHIVE_UNSUPPORTED = 0,
    ARCHIVE_ZIP,
    ARCHIVE_7Z,
    ARCHIVE_TAR_FAMILY
};

struct archive_suffix {
    const char *suffix;
    enum archive_type type;
};

static int ends_with_case_insensitive(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len) {
        return 0;
    }

    const char *str_pos = str + str_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        unsigned char a = (unsigned char)str_pos[i];
        unsigned char b = (unsigned char)suffix[i];
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }

    return 1;
}

static enum archive_type detect_archive_type(const char *basename, size_t *suffix_len) {
    static const struct archive_suffix suffixes[] = {
        { ".tar.gz", ARCHIVE_TAR_FAMILY },
        { ".tar.bz2", ARCHIVE_TAR_FAMILY },
        { ".tar.xz", ARCHIVE_TAR_FAMILY },
        { ".tar.zst", ARCHIVE_TAR_FAMILY },
        { ".tar", ARCHIVE_TAR_FAMILY },
        { ".tgz", ARCHIVE_TAR_FAMILY },
        { ".tbz2", ARCHIVE_TAR_FAMILY },
        { ".txz", ARCHIVE_TAR_FAMILY },
        { ".zip", ARCHIVE_ZIP },
        { ".7z", ARCHIVE_7Z }
    };

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (ends_with_case_insensitive(basename, suffixes[i].suffix)) {
            *suffix_len = strlen(suffixes[i].suffix);
            return suffixes[i].type;
        }
    }

    const char *dot = strrchr(basename, '.');
    if (dot != NULL && dot != basename) {
        *suffix_len = (size_t)(basename + strlen(basename) - dot);
    } else {
        *suffix_len = 0;
    }

    return ARCHIVE_UNSUPPORTED;
}

static int ensure_directory_exists(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }

        errno = ENOTDIR;
        return -1;
    }

    if (errno != ENOENT) {
        return -1;
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-help") == 0)) {
        printf("Usage: %s <archive_file>\n", argv[0]);
        printf("Unpacks the archive into a matching directory next to it.\n");
        printf("Supported formats: .zip, .7z, .tar, .tar.gz, .tgz, .tar.bz2, .tar.xz, .tar.zst\n");
        printf("Archive paths containing spaces may be provided without quotes.\n");
        return 1;
    }

    char archive_path_buf[1024];
    const char *archive_path = NULL;

    if (argc == 2) {
        archive_path = argv[1];
    } else {
        size_t remaining = sizeof(archive_path_buf);
        char *cursor = archive_path_buf;

        for (int i = 1; i < argc; i++) {
            const char *part = argv[i];
            size_t part_len = strlen(part);
            size_t needed = part_len + (i + 1 < argc ? 1 : 0);

            if (needed >= remaining) {
                fprintf(stderr, "Error: archive path too long\n");
                return 1;
            }

            memcpy(cursor, part, part_len);
            cursor += part_len;
            remaining -= part_len;

            if (i + 1 < argc) {
                *cursor = ' ';
                cursor++;
                remaining--;
            }
        }

        *cursor = '\0';
        archive_path = archive_path_buf;
    }
    const char *basename = strrchr(archive_path, '/');
    size_t prefix_len = 0;
    if (basename != NULL) {
        prefix_len = (size_t)(basename - archive_path) + 1;
        basename++;
    } else {
        basename = archive_path;
    }

    if (*basename == '\0') {
        fprintf(stderr, "Error: invalid archive name\n");
        return 1;
    }

    size_t suffix_len = 0;
    enum archive_type type = detect_archive_type(basename, &suffix_len);

    size_t name_len = strlen(basename);
    size_t stem_len = suffix_len > 0 && suffix_len < name_len ? name_len - suffix_len : name_len;
    if (stem_len == 0) {
        fprintf(stderr, "Error: could not determine output directory\n");
        return 1;
    }

    if (type == ARCHIVE_UNSUPPORTED) {
        fprintf(stderr, "Error: unsupported archive type for '%s'\n", basename);
        return 1;
    }

    char output_dir[1024];
    if (prefix_len + stem_len >= sizeof(output_dir)) {
        fprintf(stderr, "Error: output directory path too long\n");
        return 1;
    }

    memcpy(output_dir, archive_path, prefix_len);
    memcpy(output_dir + prefix_len, basename, stem_len);
    output_dir[prefix_len + stem_len] = '\0';

    if (ensure_directory_exists(output_dir) != 0) {
        perror("mkdir");
        return 1;
    }

    char command[1024];
    int n = -1;

    switch (type) {
    case ARCHIVE_ZIP:
        n = snprintf(command, sizeof(command), "unzip -d \"%s\" \"%s\"", output_dir, archive_path);
        break;
    case ARCHIVE_7Z:
        n = snprintf(command, sizeof(command), "7z x -o\"%s\" \"%s\"", output_dir, archive_path);
        break;
    case ARCHIVE_TAR_FAMILY:
        n = snprintf(command, sizeof(command), "tar -xf \"%s\" -C \"%s\"", archive_path, output_dir);
        break;
    case ARCHIVE_UNSUPPORTED:
        fprintf(stderr, "Error: unsupported archive type for '%s'\n", basename);
        return 1;
    }

    if (n < 0 || (size_t)n >= sizeof(command)) {
        fprintf(stderr, "Error: command buffer overflow\n");
        return 1;
    }
    
    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "Error: command failed with code %d\n", ret);
        return ret;
    }
    
    return 0;
}
