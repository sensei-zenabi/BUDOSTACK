# BUDOSTACK Contributor Notes

Welcome to BUDOSTACK. This project is a modular collection of small POSIX-style
utilities written in C11. The `makefile` compiles every `.c` file with
`-std=c11 -Wall -Wextra -Werror -Wpedantic`, so new code must be warning free.

## Repository layout

* **Root C sources** (`main.c`, `commandparser.c`, etc.) implement the core
  shell/launcher.
* **`lib/`** holds reusable helpers. Every file that lives here is compiled into
  `lib/*.o` and linked into *all* executables, so keep APIs generic.
* **`commands/`, `apps/`, `games/`, `utilities/`** each contain standalone
  executables. Every source must provide a `main` entry point because the build
  rule links a single object per binary.
* **`tasks/`** stores BUDOSTACK TASK scripts alongside any supporting assets.

If you add a new top-level folder with C sources, update the `makefile` to make
sure it is either built into the core binary or excluded from the automatic
`find` queries.

## C coding guidelines

* **Language level:** stick to portable C11 and POSIX.1-2001/2008 features.
  Guard feature macros the same way existing files do (e.g. define
  `_POSIX_C_SOURCE` and `_XOPEN_SOURCE` at the top before includes).
* **Formatting:** follow the house style that existing files use—4 spaces for
  indentation, K&R braces, one statement per line, and descriptive block
  comments above functions when behaviour is non-trivial.
* **Includes & headers:** include headers you actually rely on. Prefer standard
  headers over platform-specific ones. Keep header inclusion order logical:
  project headers first, then standard library headers.
* **Memory & resources:** pair every allocation with a free. When returning
  heap data, document ownership in a comment. Use `size_t` for sizes and counts.
* **Strings & buffers:** prefer `snprintf`, `strncpy`, and bounded loops. Always
  leave room for the null terminator and explicitly set it when truncating.
* **Error handling:** propagate errors with informative `perror`/`fprintf`
  messages. Do not silently ignore failure paths—remember the build treats
  warnings as errors, so rely on the compiler to help catch mistakes.
* **Static functions:** make helper functions `static` when they are only used
  within a single translation unit.

## Adding commands, apps, games, or utilities

* Place the new `.c` file in the appropriate folder. The makefile will
  auto-detect it and produce an executable with the same path stem.
* Command-line argument parsing is manual: follow the patterns in
  `commandparser.c` and reuse shared helpers from `lib/` when possible.
* If your tool needs shared functionality, consider adding it to `lib/` so it
  can be reused by other binaries.

## TASK scripts (`tasks/`)

* Script files use the custom TASK language. Keep assets in `tasks/assets/` and
  reference them with relative paths.
* Include a short header comment in each script describing its purpose and the
  commands/apps it invokes.

## Testing & build expectations

* **Always run `make clean all` (or an equivalent full rebuild) locally before
  requesting a review/creating a PR.** The top-level `makefile` already
  configures the strict warning flags (`-std=c11 -Wall -Wextra -Werror
  -Wpedantic`), so running `make` from the repo root guarantees you test with
  the same build settings the CI expects.
* Ensure `make` completes without warnings. Because every warning is an error,
  compilation failures usually indicate a style or portability issue.
* Prefer fast, self-contained checks. Avoid adding scripts that require
  long-running daemons or non-standard dependencies.
* When applicable, run `make clean` again after successful builds if you plan to
  switch branches. This avoids accidentally skipping a translation unit because
  an outdated `.o` file already existed.

These guidelines apply to the entire repository. Add a nested `AGENTS.md` if a
subdirectory needs stricter or different rules.
