# Editor language definition format

BUDOWIN reads `languages/*.lang` in its asset directory at startup. Files are
loaded in filename order; the first language matching the document's extension
or basename wins. Unknown languages use plain text. Writer and Reader views do
not apply syntax colors. Saving under another extension changes the language on
the next redraw. No rebuild is needed to install another definition; restart
BUDOWIN after changing language files.

Each UTF-8 text file contains `key=value` records, without spaces around `=`.
Empty lines and lines beginning with `#` are ignored. Supported records:

| Record | Value |
| --- | --- |
| `name` | Display name, required, up to 63 bytes |
| `extensions` | Space-separated dotted extensions, case-insensitive, up to 255 bytes |
| `filenames` | Space-separated basenames, case-sensitive, up to 255 bytes |
| `keywords` | Space-separated, case-sensitive identifier keywords, up to 4095 bytes |
| `rule` | `style\|POSIX extended regular expression` |
| `span` | `style\|literal opener\|literal closer\|flags` |

At least one extension or basename is required. Styles are `text`, `keyword`,
`comment`, `string`, `number`, `directive`, `heading`, `tag`, and `operator`.
Style colors adapt to light/dark surfaces. Selection and search-match colors
have priority.

Example:

```text
name=Example
extensions=.example
keywords=if else return
span=comment|/*|*/|multiline
span=string|"|"|escape
rule=comment|//.*
rule=number|[0-9]+([.][0-9]+)?
```

Spans take precedence over rules and keywords at the current position. The
first matching span/rule wins within its group. Regex matches must begin at the
current position; `^` only matches the start of the logical line. Empty matches
are ignored. Identifiers use letters, digits and underscores; keywords match
whole identifiers. Multiline span state crosses logical lines, including lines
above the viewport. The state cache belongs to each Editor window and updates
after edits, undo/redo and filename changes.

Span flags may contain `multiline` and `escape`, separated by spaces. Without
`multiline`, unterminated spans end at the line boundary. `escape` treats a
backslash and its following character as literal text. Delimiters are literal,
nonempty strings of up to 31 bytes; a pipe cannot occur in a span delimiter.
Nested spans, language embedding and parser-level constructs such as arbitrary
C++ raw-string delimiters are not supported by this lexical highlighter.

Limits: 64 languages, 32 regex rules and 16 spans per language. Invalid files
are skipped with a diagnostic on stderr; other definitions still load. Missing
language directories leave the Editor usable as a plain text editor. Known
language files also receive code icons and open in Editor by default unless an
explicit file association overrides or disables that behavior.

## Source text representation

The document limit is 16,384 logical rows with 2,047 cells per row. A terminating
newline occupies an empty final row. File-open errors distinguish long rows,
too many rows, binary/control bytes and read failures, preserving the current
document and its undo history. Additional Editor windows allocate their document
storage at creation; the immutable defaults do not contain document buffers.

All printable ASCII characters have bitmap glyphs and can be entered, edited,
copied and saved. In code mode, Tab inserts a literal tab and displays it as a
single-cell arrow, keeping byte positions aligned with cursor and selection
positions. Writer mode continues to insert spaces at its tab stops. LF and CRLF
files keep their line-ending convention and final-newline presence on save;
mixed LF/CRLF files normalize to CRLF. The existing CP850/Nordic conversion
remains in use: this change does not introduce a full Unicode glyph renderer.
