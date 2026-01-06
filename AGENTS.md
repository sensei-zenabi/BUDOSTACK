# BUDOSTACK Contributor Notes

Short, necessary guidance for development:

## Build & tests
* Always run `make clean all` from the repo root before PRs. Fix all warnings.

## Code style (C)
* Portable C11 + POSIX.1-2001/2008; guard feature macros like existing files.
* 4-space indentation, K&R braces, one statement per line.
* Include only needed headers; project headers before standard headers.
* Use bounded string ops (`snprintf`, `strncpy`) and always null-terminate.
* Pair allocations with frees; use `size_t` for sizes.
* Print useful errors (`perror`/`fprintf`); no silent failures.
* Make file-local helpers `static`.

## Layout & binaries
* Root C sources implement the launcher.
* `lib/` is shared by every binary—keep APIs generic.
* `commands/`, `apps/`, `games/`, `utilities/`: one `main` per `.c`.
* `tasks/` holds TASK scripts and assets.
* New top-level C source folders require `makefile` updates.
