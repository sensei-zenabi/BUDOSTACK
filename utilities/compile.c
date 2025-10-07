#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void print_help(const char *progname) {
    printf("Usage:\n");
    printf("  %s <task-file>\n", progname);
    printf("\nExamples:\n");
    printf("  %s demo.task\n", progname);
    printf("  %s demo\n", progname);
    printf("\n");
    printf("Creates a standalone executable that embeds the given TASK script\n");
    printf("from './tasks/'. The resulting binary bundles the runtime so it can\n");
    printf("run outside BUDOSTACK while still using the project's assets.\n");
}

static int has_task_extension(const char *name) {
    size_t len = strlen(name);
    return len > 5 && strcmp(name + len - 5, ".task") == 0;
}

static int ensure_task_extension(char *buffer, size_t size) {
    if (has_task_extension(buffer)) {
        return 0;
    }
    size_t len = strlen(buffer);
    if (len + 5 >= size) {
        return -1;
    }
    memcpy(buffer + len, ".task", 6);
    return 0;
}

static int build_script_path(const char *input, char *path, size_t size) {
    if (!input || !*input) {
        return -1;
    }
    if (input[0] == '/' || input[0] == '.' || strchr(input, '/') != NULL) {
        if (snprintf(path, size, "%s", input) >= (int)size) {
            return -1;
        }
    } else {
        if (snprintf(path, size, "tasks/%s", input) >= (int)size) {
            return -1;
        }
    }
    return 0;
}

static int read_entire_file(const char *path, unsigned char **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("ftell");
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        perror("fseek");
        fclose(fp);
        return -1;
    }

    unsigned char *data = malloc((size_t)file_size);
    if (!data && file_size > 0) {
        perror("malloc");
        fclose(fp);
        return -1;
    }

    size_t read = fread(data, 1, (size_t)file_size, fp);
    if (read != (size_t)file_size) {
        perror("fread");
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    *out_data = data;
    *out_size = (size_t)file_size;
    return 0;
}

static int get_repo_root(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        perror("readlink");
        return -1;
    }
    exe_path[len] = '\0';

    char *slash = strrchr(exe_path, '/');
    if (!slash) {
        fprintf(stderr, "Error: unexpected executable path '%s'\n", exe_path);
        return -1;
    }
    *slash = '\0';

    slash = strrchr(exe_path, '/');
    if (!slash) {
        fprintf(stderr, "Error: could not determine repository root from '%s'\n", exe_path);
        return -1;
    }
    *slash = '\0';

    if (snprintf(buffer, size, "%s", exe_path) >= (int)size) {
        return -1;
    }
    return 0;
}

static int write_c_string_literal(FILE *fp, const char *text) {
    if (fputc('"', fp) == EOF) {
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        unsigned char c = *p;
        if (c == '\\') {
            if (fputs("\\\\", fp) == EOF) return -1;
        } else if (c == '\"') {
            if (fputs("\\\"", fp) == EOF) return -1;
        } else if (c == '\n') {
            if (fputs("\\n", fp) == EOF) return -1;
        } else if (c == '\r') {
            if (fputs("\\r", fp) == EOF) return -1;
        } else if (c == '\t') {
            if (fputs("\\t", fp) == EOF) return -1;
        } else if (c < 32 || c > 126) {
            if (fprintf(fp, "\\x%02X", c) < 0) return -1;
        } else {
            if (fputc((int)c, fp) == EOF) return -1;
        }
    }
    if (fputc('"', fp) == EOF) {
        return -1;
    }
    return 0;
}

static int write_stub(FILE *fp,
                      const unsigned char *data,
                      size_t size,
                      const char *runtask_path,
                      const char *base_dir,
                      const char *script_name) {
    if (fprintf(fp,
                "#define _POSIX_C_SOURCE 200809L\n"
                "#define _XOPEN_SOURCE 700\n"
                "#include <errno.h>\n"
                "#include <limits.h>\n"
                "#include <stddef.h>\n"
                "#include <stdio.h>\n"
                "#include <stdlib.h>\n"
                "#include <string.h>\n"
                "#include <unistd.h>\n"
                "\n"
                "#define main runtask_main\n"
                "#include \"%s\"\n"
                "#undef main\n\n",
                runtask_path) < 0) {
        return -1;
    }

    if (fprintf(fp, "static const unsigned char embedded_script[] = {\n") < 0) {
        return -1;
    }
    for (size_t i = 0; i < size; ++i) {
        if (i % 12 == 0) {
            if (fputs("    ", fp) == EOF) return -1;
        }
        if (fprintf(fp, "0x%02X", data[i]) < 0) return -1;
        if (i + 1 != size) {
            if (fputc(',', fp) == EOF) return -1;
            if (i % 12 != 11) {
                if (fputc(' ', fp) == EOF) return -1;
            }
        }
        if (i % 12 == 11 || i + 1 == size) {
            if (fputc('\n', fp) == EOF) return -1;
        }
    }
    if (fputs("};\n", fp) == EOF) {
        return -1;
    }

    if (fprintf(fp, "static const size_t embedded_script_size = sizeof(embedded_script);\n") < 0) {
        return -1;
    }

    if (fprintf(fp, "static const char embedded_base[] = ") < 0) {
        return -1;
    }
    if (write_c_string_literal(fp, base_dir ? base_dir : "") < 0) {
        return -1;
    }
    if (fputs(";\n", fp) == EOF) {
        return -1;
    }

    if (fprintf(fp, "static const char embedded_name[] = ") < 0) {
        return -1;
    }
    if (write_c_string_literal(fp, script_name ? script_name : "task") < 0) {
        return -1;
    }
    if (fputs(";\n\n", fp) == EOF) {
        return -1;
    }

    const char body[] =
        "static int write_script(char *path, size_t path_size) {\n"
        "    char tmpl[] = \"/tmp/budotask_XXXXXX\";\n"
        "    int fd = mkstemp(tmpl);\n"
        "    if (fd < 0) {\n"
        "        perror(\"mkstemp\");\n"
        "        return -1;\n"
        "    }\n"
        "    FILE *fp = fdopen(fd, \"wb\");\n"
        "    if (!fp) {\n"
        "        perror(\"fdopen\");\n"
        "        close(fd);\n"
        "        unlink(tmpl);\n"
        "        return -1;\n"
        "    }\n"
        "    size_t written = fwrite(embedded_script, 1, embedded_script_size, fp);\n"
        "    if (written != embedded_script_size) {\n"
        "        perror(\"fwrite\");\n"
        "        fclose(fp);\n"
        "        unlink(tmpl);\n"
        "        return -1;\n"
        "    }\n"
        "    if (fclose(fp) != 0) {\n"
        "        perror(\"fclose\");\n"
        "        unlink(tmpl);\n"
        "        return -1;\n"
        "    }\n"
        "    if (snprintf(path, path_size, \"%s\", tmpl) >= (int)path_size) {\n"
        "        fprintf(stderr, \"Error: temporary path too long\\n\");\n"
        "        unlink(tmpl);\n"
        "        return -1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n"
        "int main(int argc, char *argv[]) {\n"
        "    (void)embedded_name;\n"
        "    char script_path[PATH_MAX];\n"
        "    if (write_script(script_path, sizeof(script_path)) != 0) {\n"
        "        return EXIT_FAILURE;\n"
        "    }\n"
        "    if (embedded_base[0] != '\\0') {\n"
        "        if (setenv(\"BUDOSTACK_BASE\", embedded_base, 1) != 0) {\n"
        "            perror(\"setenv\");\n"
        "            unlink(script_path);\n"
        "            return EXIT_FAILURE;\n"
        "        }\n"
        "    }\n"
        "    int rt_argc = argc + 1;\n"
        "    char **rt_argv = calloc((size_t)rt_argc + 1, sizeof(char *));\n"
        "    if (!rt_argv) {\n"
        "        perror(\"calloc\");\n"
        "        unlink(script_path);\n"
        "        return EXIT_FAILURE;\n"
        "    }\n"
        "    rt_argv[0] = argv[0];\n"
        "    rt_argv[1] = script_path;\n"
        "    for (int i = 1; i < argc; ++i) {\n"
        "        rt_argv[i + 1] = argv[i];\n"
        "    }\n"
        "    int rc = runtask_main(rt_argc, rt_argv);\n"
        "    free(rt_argv);\n"
        "    unlink(script_path);\n"
        "    return rc;\n"
        "}\n";

    if (fputs(body, fp) == EOF) {
        return -1;
    }

    return 0;
}

static int run_compiler(const char *stub_path, const char *output_path) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        char *const args[] = {
            "gcc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wpedantic",
            "-pthread",
            "-o",
            (char *)output_path,
            "-x",
            "c",
            (char *)stub_path,
            "-lm",
            NULL
        };
        execvp("gcc", args);
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "gcc failed with status %d\n", status);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: No TASK script specified.\n");
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    char script_spec[PATH_MAX];
    if (snprintf(script_spec, sizeof(script_spec), "%s", argv[1]) >= (int)sizeof(script_spec)) {
        fprintf(stderr, "Error: Script name too long.\n");
        return EXIT_FAILURE;
    }

    if (ensure_task_extension(script_spec, sizeof(script_spec)) != 0) {
        fprintf(stderr, "Error: Script name too long for '.task' extension.\n");
        return EXIT_FAILURE;
    }

    char script_path[PATH_MAX];
    if (build_script_path(script_spec, script_path, sizeof(script_path)) != 0) {
        fprintf(stderr, "Error: Could not resolve script path.\n");
        return EXIT_FAILURE;
    }

    char resolved_script[PATH_MAX];
    if (!realpath(script_path, resolved_script)) {
        fprintf(stderr, "Error: Could not locate script '%s': %s\n", script_path, strerror(errno));
        return EXIT_FAILURE;
    }

    char repo_root[PATH_MAX];
    if (get_repo_root(repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "Error: Failed to determine repository root.\n");
        return EXIT_FAILURE;
    }

    char tasks_prefix[PATH_MAX];
    if (snprintf(tasks_prefix, sizeof(tasks_prefix), "%s/tasks/", repo_root) >= (int)sizeof(tasks_prefix)) {
        fprintf(stderr, "Error: Path too long.\n");
        return EXIT_FAILURE;
    }

    size_t prefix_len = strlen(tasks_prefix);
    if (strncmp(resolved_script, tasks_prefix, prefix_len) != 0) {
        fprintf(stderr, "Error: Script must reside under '%s'.\n", tasks_prefix);
        return EXIT_FAILURE;
    }

    const char *base_name = strrchr(resolved_script, '/');
    base_name = base_name ? base_name + 1 : resolved_script;
    char output_name[PATH_MAX];
    size_t base_len = strlen(base_name);
    if (base_len > 5) {
        base_len -= 5; /* remove .task */
    }
    if (base_len == 0 || base_len >= sizeof(output_name)) {
        fprintf(stderr, "Error: Invalid script name '%s'.\n", base_name);
        return EXIT_FAILURE;
    }
    memcpy(output_name, base_name, base_len);
    output_name[base_len] = '\0';

    if (access(resolved_script, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot read script '%s'.\n", resolved_script);
        return EXIT_FAILURE;
    }

    unsigned char *script_data = NULL;
    size_t script_size = 0;
    if (read_entire_file(resolved_script, &script_data, &script_size) != 0) {
        return EXIT_FAILURE;
    }

    char runtask_path[PATH_MAX];
    if (snprintf(runtask_path, sizeof(runtask_path), "%s/apps/runtask.c", repo_root) >= (int)sizeof(runtask_path)) {
        fprintf(stderr, "Error: Path to runtask.c is too long.\n");
        free(script_data);
        return EXIT_FAILURE;
    }

    if (access(runtask_path, R_OK) != 0) {
        fprintf(stderr, "Error: Missing runtask source at '%s'.\n", runtask_path);
        free(script_data);
        return EXIT_FAILURE;
    }

    char stub_template[] = "/tmp/budostack_compileXXXXXX";
    int fd = mkstemp(stub_template);
    if (fd < 0) {
        perror("mkstemp");
        free(script_data);
        return EXIT_FAILURE;
    }

    FILE *stub_fp = fdopen(fd, "w");
    if (!stub_fp) {
        perror("fdopen");
        close(fd);
        unlink(stub_template);
        free(script_data);
        return EXIT_FAILURE;
    }

    if (write_stub(stub_fp, script_data, script_size, runtask_path, repo_root, output_name) != 0) {
        fprintf(stderr, "Error: Failed to write temporary source stub.\n");
        fclose(stub_fp);
        unlink(stub_template);
        free(script_data);
        return EXIT_FAILURE;
    }

    if (fclose(stub_fp) != 0) {
        perror("fclose");
        unlink(stub_template);
        free(script_data);
        return EXIT_FAILURE;
    }

    free(script_data);

    if (run_compiler(stub_template, output_name) != 0) {
        unlink(stub_template);
        return EXIT_FAILURE;
    }

    unlink(stub_template);

    printf("Built executable '%s' from %s\n", output_name, resolved_script);
    return EXIT_SUCCESS;
}
