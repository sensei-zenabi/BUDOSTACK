Folder to store BUDOSTACK TASK language commands.

For help > type the COMMAND > e.g. '_IMAGE'

Requirements for Commands:
- Written with plain C with no separate header files
- #define _POSIX_C_SOURCE 200112L  // Enable POSIX.1-2001 features
- Have -[x] and -[y] arguments to define output position from top-left
  corner of terminal, where x is columns and y rows
- Have -[color] argument, where colors are defined using standard 256
  color VGA / IBM PC color palette
- Output their results to stdout

## Data analysis helpers

- `_CSVSTATS` — compute descriptive statistics (count, sum, mean, min, max, variance, standard deviation) for a numeric column in a `;` separated CSV file.
- `_CSVFILTER` — filter rows based on numeric or lexical comparisons against a selected column and optionally write the result to a file.
