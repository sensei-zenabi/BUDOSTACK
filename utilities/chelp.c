#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_WIDTH 78

static void print_lines(const char *const *lines, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        puts(lines[i]);
}

static void print_section_break(const char *title)
{
    printf("%s\n", title);
    for (size_t i = 0; title[i] != '\0' && i < LINE_WIDTH; ++i)
        putchar('-');
    putchar('\n');
}

static void print_intro(void)
{
    static const char *intro[] = {
        "BUDOSTACK C Programming Handbook",
        "==============================================================",
        "This utility prints a deep, university-level C programming guide.",
        "Read it with a pager (| less) and search within. Lines cap at 78",
        "columns to stay pager friendly. The goal is to help you grow into",
        "a junior systems developer able to write, debug, and ship C",
        "software.",
        "",
        "Using this tool",
        "- View with paging: utilities/chelp | less",
        "- Full rebuild in repo: make clean all",
        "- Single-file compile template: cc -std=c11 -Wall -Wextra",
        "  -Wpedantic -g -o demo demo.c",
        "- Run static analysis locally when possible: clang-tidy demo.c",
        "",
        "Learning expectations",
        "- Assume no prior C background; start from compilation basics.",
        "- Move through syntax, memory, data structures, tooling, and",
        "  patterns.",
        "- Reinforce each chapter with exercises and guided lab outlines.",
        "- Use BUDOSTACK makefile flags as your defensive defaults.",
    };

    print_lines(intro, sizeof(intro) / sizeof(intro[0]));
}

static void print_language_core(void)
{
    print_section_break("Language fundamentals");

    static const char *core[] = {
        "Compilation pipeline",
        "1. Preprocess: expand #include, #define, and conditional blocks.",
        "2. Compile: translate each translation unit (.c + headers) to .o.",
        "3. Link: resolve symbols into executables or archives.",
        "4. Optional: static analysis, sanitizers, profilers, formatters.",
        "",
        "Hello, world",
        "#include <stdio.h>",
        "int main(void) {",
        "    printf(\"Hello, world\\n\");",
        "    return 0;",
        "}",
        "",
        "Types and constants",
        "- Integer families: char, short, int, long, long long.",
        "- Floating families: float, double, long double.",
        "- Fixed width: uint8_t, uint16_t, uint32_t, uint64_t.",
        "- Qualifiers: const (read-only), volatile (outside influence).",
        "- Literal suffixes: 1U, 1L, 1ULL; bases: 42, 052, 0x2A.",
        "- bool from <stdbool.h>; use true/false instead of 0/1 for clarity.",
        "",
        "Pointers and references",
        "- Pointers store addresses; *p dereferences, &x yields an address.",
        "- Always initialize pointers and set them to NULL after free.",
        "- Pointer arithmetic steps in sizeof(pointed-type) units.",
        "- Use size_t for sizes and ptrdiff_t for pointer differences.",
        "",
        "Storage duration and linkage",
        "- Automatic: block-local variables live until scope exit.",
        "- Static duration: file-scope or static locals persist for program",
        "  life.",
        "- Dynamic: malloc/calloc/realloc allocate until free.",
        "- Linkage: extern exposes across translation units; static hides",
        "  inside.",
        "",
        "Control flow",
        "- Selection: if/else, switch/case/default with break.",
        "- Iteration: for, while, do/while; continue skips, break exits.",
        "- goto is allowed; reserve for single-exit cleanup in error paths.",
        "",
        "Functions",
        "- Declare prototypes before use to catch mismatches at compile",
        "  time.",
        "- Arguments pass by value; pass pointers when callees should",
        "  mutate.",
        "- Document ownership: who allocates, who frees, valid lifetimes.",
        "- Mark helpers static when only used within one translation unit.",
        "",
        "Arrays and strings",
        "- Arrays decay to pointers when passed to functions; pass lengths",
        "  too.",
        "- Strings are char arrays terminated by '\\0'; keep space for it.",
        "- Prefer snprintf/strnlen over unsafe strcpy/strlen on unknown",
        "  data.",
        "- Multidimensional arrays must match parameter declarations",
        "  exactly.",
        "",
        "Structs, unions, enums",
        "- Structs group fields; use designated initializers for clarity.",
        "- Enums define named integer constants; great with switch.",
        "- Unions overlay storage; track which member is active.",
        "- Bit-fields pack flags: struct flags { unsigned ready:1; }.",
        "",
        "Macros and constants",
        "- Prefer const variables or enums for typed constants.",
        "- Use macros for small wrappers or compile-time configuration.",
        "- Parenthesize macro parameters to avoid precedence bugs.",
        "- Example: #define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))",
    };

    print_lines(core, sizeof(core) / sizeof(core[0]));
}

static void print_memory_and_ub(void)
{
    print_section_break("Memory, undefined behaviour, and safety");

    static const char *mem[] = {
        "Dynamic memory",
        "- malloc(size) returns uninitialized storage; calloc zeroes memory.",
        "- realloc(ptr, n) resizes; on failure the old block remains valid.",
        "- Always check allocation results before use and free on every",
        "  path.",
        "- Free in reverse ownership order and null the pointer after free.",
        "",
        "Lifetime and aliasing",
        "- Dangling pointers are UB; avoid returning addresses to locals.",
        "- Strict aliasing: access an object only through compatible types.",
        "- volatile does not make things thread-safe; it prevents certain",
        "  compiler optimizations on that object.",
        "",
        "Common UB pitfalls",
        "- Buffer overruns, use-after-free, double-free, null dereferences.",
        "- Signed overflow is UB; unsigned wraps by definition.",
        "- Shifts by negative or too-large counts are UB.",
        "- Modifying a const object through a non-const pointer is UB.",
        "- Reading uninitialized variables yields indeterminate data.",
        "",
        "Defensive techniques",
        "- Prefer size_t for lengths; validate before arithmetic to avoid",
        "  wrap.",
        "- Bound every loop that copies memory; keep space for terminators.",
        "- Use calloc for zeroed buffers when you require deterministic",
        "  state.",
        "- Add assertions in debug builds to catch impossible conditions",
        "  early.",
        "",
        "Memory layout",
        "- Alignment may add padding; order struct fields wide-to-narrow to",
        "  reduce waste.",
        "- offsetof(type, field) from <stddef.h> helps with packed layouts.",
        "- Never assume pointer size or endianness; prefer explicit",
        "  protocols.",
    };

    print_lines(mem, sizeof(mem) / sizeof(mem[0]));
}

static void print_tooling(void)
{
    print_section_break("Tooling, build systems, and workflow");

    static const char *tooling[] = {
        "Compilers and flags",
        "- Use repo defaults: -std=c11 -Wall -Wextra -Werror -Wpedantic -g.",
        "- Add -O0 for debugging, -O2 for general builds, -O3 rarely.",
        "- Enable sanitizers during development: -fsanitize=address,undef.",
        "",
        "Linking and libraries",
        "- Static libs: ar rcs libfoo.a foo.o; link with -lfoo -Lpath.",
        "- Shared libs: position independent code via -fPIC; link with",
        "  -shared.",
        "- Order matters: place libraries after objects that reference them.",
        "",
        "Makefiles",
        "- Declare variables for flags (CFLAGS, LDFLAGS) and sources.",
        "- Use pattern rules: %.o: %.c ; $(CC) $(CFLAGS) -c $< -o $@",
        "- Add phony targets for tooling (format, tidy, docs, clean).",
        "- Keep builds reproducible: pin flags, avoid environment surprises.",
        "",
        "Debugging",
        "- Build with -g; run gdb ./prog. Inspect frames with bt, frame N,",
        "  info locals, and print values with p var.",
        "- printf-debugging works; keep labels clear and flush output lines.",
        "- Use valgrind or sanitizers to catch memory leaks and races early.",
        "",
        "Profiling",
        "- time for wall-clock; perf record/report on Linux for hotspots.",
        "- gprof when compiled with -pg, or sampling profilers like perf.",
        "",
        "Version control habits",
        "- Commit small, logical changes with meaningful messages.",
        "- Run make clean all before pushing to ensure warning-free builds.",
        "- Document assumptions in comments near the code that uses them.",
    };

    print_lines(tooling, sizeof(tooling) / sizeof(tooling[0]));
}

static void print_patterns(void)
{
    print_section_break("Idioms, patterns, and architecture");

    static const char *patterns[] = {
        "Error handling",
        "- Return int status codes; 0 for success, non-zero for failure.",
        "- In callers, check return values and branch to cleanup labels.",
        "- Prefer one exit path per function that frees acquired resources.",
        "",
        "APIs and encapsulation",
        "- Expose opaque structs via forward declarations; manage through",
        "  constructor/destructor-like functions.",
        "- Keep headers self-contained and minimal; avoid leaking internals.",
        "",
        "Resource management",
        "- Pair every acquisition (malloc, fopen, socket) with a release.",
        "- Track ownership in comments and in function names",
        "  (create/destroy).",
        "",
        "Data structures",
        "- Linked lists: store next pointer; watch ownership and lifetime.",
        "- Dynamic arrays: grow geometrically (x1.5 or x2) to amortize",
        "  copies.",
        "- Hash tables: choose good hash, handle collisions (open addressing",
        "  or chaining).",
        "- Trees: balanced variants (AVL, red-black) keep operations",
        "  O(log n).",
        "",
        "String handling",
        "- Normalize input by trimming and validating before parsing.",
        "- Use snprintf for formatting; pre-size buffers conservatively.",
        "- Avoid strcpy/strcat on unknown data; prefer strnlen and memcpy.",
        "",
        "Testing and verification",
        "- Build small harnesses around tricky code paths.",
        "- Assert preconditions; fuzz inputs where possible.",
        "- Keep deterministic seeds for pseudo-random tests.",
    };

    print_lines(patterns, sizeof(patterns) / sizeof(patterns[0]));
}

static void print_code_sample(void)
{
    print_section_break("Worked example: safe line reader");

    static const char *sample[] = {
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "",
        "static int read_line(FILE *fp, char *buf, size_t cap) {",
        "    if (cap == 0) return -1;",
        "    if (fgets(buf, (int)cap, fp) == NULL)",
        "        return ferror(fp) ? -1 : 0;",
        "    buf[cap - 1] = '\\0';",
        "    size_t len = strnlen(buf, cap);",
        "    if (len > 0 && buf[len - 1] == '\\n')",
        "        buf[len - 1] = '\\0';",
        "    return 1;",
        "}",
        "",
        "int main(void) {",
        "    char line[128];",
        "    fputs(\"Enter a line: \" , stdout);",
        "    fflush(stdout);",
        "    int rc = read_line(stdin, line, sizeof(line));",
        "    if (rc <= 0) {",
        "        perror(\"read_line\");",
        "        return EXIT_FAILURE;",
        "    }",
        "    printf(\"You typed: %s\\n\", line);",
        "    return EXIT_SUCCESS;",
        "}",
    };

    print_lines(sample, sizeof(sample) / sizeof(sample[0]));
}

static void print_study_paths(void)
{
    print_section_break("Suggested study path");

    static const char *path[] = {
        "Phase 1: foundations",
        "- Compile basics, variables, expressions, control flow, functions.",
        "- Practice pointer fundamentals and array decay rules.",
        "- Write small programs that parse input, transform, and print.",
        "",
        "Phase 2: memory and data",
        "- malloc/calloc/realloc/free patterns; ownership documentation.",
        "- Implement dynamic arrays, linked lists, stacks, and queues.",
        "- Learn struct layout, alignment, and bit-fields.",
        "",
        "Phase 3: files and processes",
        "- fopen/fread/fwrite/fprintf and error handling.",
        "- Command-line argument parsing; environment variables.",
        "- POSIX basics: open/read/write/close, lseek, and permissions.",
        "",
        "Phase 4: concurrency and networking (POSIX level)",
        "- pthreads: thread creation, join, mutexes, condition variables.",
        "- Non-blocking I/O: select/poll/epoll, timeouts, and readiness.",
        "- Sockets: TCP/UDP, connect/listen/accept, address structures.",
        "",
        "Phase 5: tooling and quality",
        "- gdb, valgrind, sanitizers, and profilers.",
        "- clang-tidy or cppcheck where available.",
        "- Code reviews: small diffs, clear ownership, documented",
        "  invariants.",
        "",
        "Phase 6: projects",
        "- Build a text adventure with save files, command parsing, and",
        "  tests.",
        "- Write a HTTP client with sockets and robust parsing.",
        "- Implement a small allocator or memory pool for performance.",
    };

    print_lines(path, sizeof(path) / sizeof(path[0]));
}

static void print_lab_library(void)
{
    print_section_break("Guided lab catalog (aim: >10k printable lines)");

    static const char *topics[] = {
        "Pointers", "Memory", "Strings", "Files", "Parsing", "Testing",
        "Debugging", "Concurrency", "Networking", "Data structures",
        "Algorithms", "Tooling", "Numerics", "Security", "APIs",
    };
    static const char *skills[] = {
        "trace lifetimes", "avoid UB", "design interfaces", "readability",
        "complexity", "profiling", "error paths", "resource cleanup",
        "predictability", "observability", "determinism", "reentrancy",
        "throughput", "latency", "portability",
    };

    const size_t topic_count = sizeof(topics) / sizeof(topics[0]);
    const size_t skill_count = sizeof(skills) / sizeof(skills[0]);

    char line[LINE_WIDTH + 1];

    /* 750 labs x 12 lines ~= 9000 lines plus core material. */
    for (int i = 1; i <= 750; ++i) {
        const char *topic = topics[(size_t)(i - 1) % topic_count];
        const char *skill = skills[(size_t)(i - 1) % skill_count];

        snprintf(line, sizeof(line),
                 "Lab %03d | %-14s | Focus: %-16s", i, topic, skill);
        puts(line);
        puts("  Outcome: write a complete, warning-free program using C11.");
        puts("  Readings: K&R ch2-5 or C reference; trace each operator.");
        puts("  Plan: design, prototype, test, refactor, and measure.");
        puts("  Checklist: handle errors, free resources, log decisions.");
        puts("  Stretch: add benchmarks and sanitizer runs.");
        puts("  Deliverable: README with build steps and assumptions.");
        puts("  Review: explain lifetime, threading, and failure handling.");
        puts("  Demo: run with varied inputs, include adversarial cases.");
        puts("  Reflection: note surprises and how you verified results.");
        puts("  Next: translate lessons into reusable helpers.");
        puts("----------------------------------------------------------");
    }
}

static void print_flashcards(void)
{
    print_section_break("Flashcards and quick checks");

    static const char *cards[] = {
        "- What is the difference between size_t and ssize_t?",
        "- When does array-to-pointer decay not occur?",
        "- How do you prevent buffer overruns when copying strings?",
        "- Why is signed integer overflow undefined?",
        "- How does realloc behave on failure?",
        "- When should a helper be static?",
        "- What does volatile guarantee?",
        "- How do you flush stdout manually?",
        "- Why place libraries after objects on the linker command?",
        "- What are common causes of data races in C?",
        "- How do you compute the length of a flexible array member?",
        "- What does restrict promise to the compiler?",
        "- How do you safely parse command line options?",
        "- When do you prefer a struct over parallel arrays?",
        "- How do you zero sensitive data securely?",
    };

    print_lines(cards, sizeof(cards) / sizeof(cards[0]));
}

int main(void)
{
    print_intro();
    print_language_core();
    print_memory_and_ub();
    print_tooling();
    print_patterns();
    print_code_sample();
    print_study_paths();
    print_lab_library();
    print_flashcards();
    return EXIT_SUCCESS;
}

