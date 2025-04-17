#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/* Buffer size for reading data */
#define BUF_SIZE 8192

/* CRC-32 (IEEE 802.3) polynomial */
#define POLY 0xEDB88320

/* Table of CRCs for each byte value */
static uint32_t crc_table[256];

/* Initialize the CRC table. */
static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ POLY;
            else
                crc >>= 1;
        }
        crc_table[i] = crc;
    }
}

/* Update CRC with a buffer of data. */
static uint32_t update_crc32(uint32_t crc, const uint8_t *buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ buf[i]) & 0xFF];
    }
    return ~crc;
}

/* Compute CRC-32 of the open file stream. */
static int compute_crc(FILE *f, uint32_t *out_crc) {
    uint8_t buf[BUF_SIZE];
    size_t n;
    uint32_t crc = 0xFFFFFFFF;

    while ((n = fread(buf, 1, BUF_SIZE, f)) > 0) {
        crc = update_crc32(crc, buf, n);
    }
    if (ferror(f)) {
        return -1;
    }

    *out_crc = crc;
    return 0;
}

/* Print usage information to stderr. */
static void print_usage() {
    fprintf(stderr,
        "Usage:\n"
        "  crc32 [file]                 calculate and print CRC-32 of file\n"
        "  crc32 [file] [checksum]      verify CRC-32 against provided hex checksum\n"
        "  crc32 -help                  display this help\n");
}

int main(int argc, char **argv) {
    uint32_t crc;
    FILE *f;

    /* No arguments: display help */
    if (argc == 1) {
        print_usage();
        return 0;
    }

    /* Single argument */
    if (argc == 2) {
        /* Help request */
        if (strcmp(argv[1], "-help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        /* Calculate checksum */
        f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "Error opening '%s': %s\n", argv[1], strerror(errno));
            return 1;
        }
        init_crc32();
        if (compute_crc(f, &crc) < 0) {
            fprintf(stderr, "Read error on '%s'\n", argv[1]);
            fclose(f);
            return 1;
        }
        fclose(f);
        printf("%08X  %s\n", crc, argv[1]);
        return 0;
    }

    /* Two arguments: file + checksum verification */
    if (argc == 3) {
        f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "Error opening '%s': %s\n", argv[1], strerror(errno));
            return 1;
        }
        init_crc32();
        if (compute_crc(f, &crc) < 0) {
            fprintf(stderr, "Read error on '%s'\n", argv[1]);
            fclose(f);
            return 1;
        }
        fclose(f);
        uint32_t expected = (uint32_t)strtoul(argv[2], NULL, 16);
        if (crc == expected) {
            printf("CRC32 matched: %08X\n", crc);
            return 0;
        } else {
            printf("CRC32 mismatch: computed %08X, expected %08X\n", crc, expected);
            return 1;
        }
    }

    /* More than two arguments: display help */
    print_usage(argv[0]);
    return 1;
}
