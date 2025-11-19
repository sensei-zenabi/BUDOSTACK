#ifndef BUDOSTACK_PATHS_H
#define BUDOSTACK_PATHS_H

#include <stddef.h>

int budostack_compute_root_directory(const char *argv0, char *out_path, size_t out_size);
int budostack_build_path(char *dest, size_t dest_size, const char *base, const char *suffix);
int budostack_resolve_resource_path(const char *root_dir,
                                    const char *argument,
                                    char *out_path,
                                    size_t out_size);

#endif
