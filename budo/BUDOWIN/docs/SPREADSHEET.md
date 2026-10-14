# Spreadsheet architecture and workbook specification

`APPS/SPREADSHEET.BWA` is a native ABI 1.14+ application. It uses the shared
SDK menu, button, scrollbar, caret, dialog, theme and managed-window services.
The desktop associates `.BSHEET` with `spreadsheet`. Existing CSV/TSV desktop
associations remain configurable; the spreadsheet's Open dialog imports them.

## Module boundaries

| Module | Responsibility | Extension point |
| --- | --- | --- |
| `sheet_model.h` | Sparse workbook, cell coordinates, dimensions, alignment, chart metadata | Storage limits and workbook records |
| `sheet_formula.h` | Bounded parser, typed values, evaluation cache | `SheetFunction` registry and callback |
| `sheet_edit.h` | Reference-aware copy, fill, structural edits and stable row sorting | Transactional workbook operations |
| `sheet_io.h` | Native persistence and CSV/TSV interchange | Versioned format reader/writer |
| `sheet_chart.h` | Chart types and shared drawing/export canvas | `SheetChartType` registry and canvas callbacks |
| `spreadsheet.c` | UI state, selection, history and asynchronous dialogs | Menus and UI interactions |

All content mutations invalidate the evaluation cache. Values cache by sorted
cell index; indices are never retained across mutations. Formula traversal has
cycle detection and bounded recursion. No formula evaluates C, shell commands
or arbitrary code. Native instances own separate workbook, history and clipboard
allocations; the host loads each instance independently.

The UI stages multi-cell mutations in a separate workbook. A failed allocation,
capacity check, formula translation or file read leaves the current workbook
unchanged. Undo stores at most sixteen workbook states, including chart metadata.
The saved-content hash determines whether a save confirmation is required,
including after undo/redo. Editing is committed before close/save confirmation.

## Formula extensions

Append a `SheetFunction` descriptor with an uppercase name, syntax hint, argument
bounds and a callback. Names, rather than enum values, are stored in formulas.
Callbacks receive typed numbers, text, blanks, errors and lazy rectangular ranges.
Use `sheet_value_at` to visit range members and propagate errors explicitly.
New callbacks do not require changes to the grid or file format. The operation
field is an optional callback discriminator, not a persisted identifier.

The parser supports parentheses, arithmetic, right-associative powers, postfix
percent, comparisons, concatenation, absolute/mixed A1 references and cross-sheet
references. Spaces in sheet names require single quotes; doubled quotes escape
quotes. `IF` and `IFERROR` have parser-managed lazy branches. Introducing another
lazy function requires defining its branch evaluation policy in the parser.

The initial registry covers arithmetic/trigonometry, descriptive statistics,
linear regression/correlation, conditionals, text transforms, exact lookups,
conditional sums/counts, dates, and PMT/PV/FV/NPV. MATCH/VLOOKUP support exact matching;
approximate modes return an error. COUNTIF/SUMIF support exact values and numeric
comparison criteria, not wildcard patterns. Trigonometric angles use radians.
PMT/PV/FV use periodic rates and spreadsheet cashflow sign conventions; payment
type is 0 for end-of-period and 1 for beginning-of-period. NPV discounts its first
cashflow by one period; add an initial time-zero investment separately.

DATE/YEAR/MONTH/DAY use the spreadsheet 1900 date serial system, including its
legacy serial 60 placeholder for 1900-02-29. DATE normalizes month/day overflow
and supports resulting dates from 1900 through 9999. TODAY uses UTC and cached
formula values are invalidated on a UTC day change during evaluation. Dates are
numbers; date formatting displays YYYY-MM-DD.

Unquoted numeric input is parsed as a finite double (including a trailing `%`).
A leading apostrophe forces text. Blank arithmetic operands coerce to zero;
aggregate functions ignore text/blanks. Error values include `#REF!`, `#VALUE!`,
`#DIV/0!`, `#NAME?`, `#NUM!`, `#CYCLE!` and `#LIMIT!`.

## Chart extensions

Append a chart descriptor and increase `SHEET_CHART_TYPES`. Existing IDs are
stable: 0 line, 1 scatter, 2 bar, 3 histogram, 4 pie. Never reorder persisted IDs.
A renderer receives a workbook calculation context, persistent source range,
labels, viewport and `SheetCanvas`. Use the same renderer for screen and SVG;
new diagram types need no formula or grid changes. Additional chart metadata
requires a workbook schema version change, with a reader for previous versions.

One live figure is stored per sheet. Line/bar use row positions for X; scatter
uses numeric X from the first column. For multi-column sources, subsequent
columns are Y series, up to eight. Text headers and missing/error values are
skipped; line gaps break the connecting path. Histogram uses ten equal bins of
the first column. Pie accepts at most eight rows of nonnegative values; the
first column supplies labels when a second value column is selected. Titles and
axis labels are persisted. SVG output escapes user text and shares chart geometry
with the UI. Cartesian figures have independently switchable horizontal/vertical
major/minor grids; axes and ticks remain visible when grids are disabled. Major
intervals contain four minor grid lines. Y captions rotate counterclockwise beside
the Y axis; X captions center horizontally below the X axis. The shared canvas
provides an optional vertical-text callback for backends with native rotation;
the default renders rotated SDK bitmap glyphs. The 5x7 bitmap font lives in
`sdk/font.h` and is shared with Flow.

PCX exports render the same canvas at 800x500, then write PCX v5 with 8-bit RGB
planes, an even 800-byte stride, and bounded per-plane RLE runs. This preserves
exact chart colors without palette quantization. SVG retains native rotated text.
Both exporters stage output atomically and preserve an existing destination when
rendering or writing fails. Selection aggregates visit only populated cells.

## Native file format (versions 1 and 2)

The writer emits `BUDOSHEET 2\n`; the reader accepts versions 1 and 2.
Both use UTF-8/ASCII records. Version 1 has header `BUDOSHEET 1\n`, followed by decimal `tabs cells\n`.
For each sheet, write:

1. Name as lowercase hexadecimal bytes followed by newline.
2. Decimal `enabled type source_sheet r0 c0 r1 c1\n` (zero-based coordinates).
3. Title, X label, Y label as three hexadecimal lines.

For each stored cell, in strictly increasing coordinate-key order, write decimal
`key format\n` followed by one hexadecimal raw-input line. The key is
`(sheet * 1024 + row) * 128 + column`. Formats are 0 general, 1 fixed two-decimal,
2 percent, 3 scientific, 4 monetary two-decimal (currency-neutral), and 5 ISO date.
Hex encoding preserves commas, quotes, tabs and newlines without escape ambiguity.
The reader rejects unsupported versions, invalid counts, bounds, formats, chart
IDs, duplicate/out-of-order cells, truncated strings and trailing content. A
future schema must introduce a new header version and explicit migration;
never serialize C structs, enum layouts or evaluation caches.

Version 2 retains the version 1 sheet/chart records and adds, after the three
chart label lines, a decimal grid-mask line and two dimension arrays. The mask
uses bits 1 horizontal major, 2 vertical major, 4 horizontal minor, 8 vertical
minor. Dimensions are unsigned decimal integers, sixteen per line: 128 column
widths followed by 1024 row heights, per sheet. Zero means the default (88 pixels
wide, 20 pixels high); explicit sizes are 16..512 pixels. Cell record headers are
`key format horizontal vertical\n`, followed by the same hexadecimal raw-input
line. Horizontal alignment IDs are 0 automatic, 1 left, 2 center, 3 right. Vertical
IDs are 0 middle, 1 top, 2 bottom. Styled empty cells remain stored records.

The version 1 reader supplies default dimensions/alignment and enables horizontal
major grids, preserving the original figure appearance. Saving upgrades to v2.
Unsupported versions and invalid dimensions, alignment IDs or grid bits are
rejected before replacing the current workbook. Resize, alignment, rename and
grid configuration are content changes covered by save confirmation and undo.

Writers use `mkstemp` in the destination directory, flush and sync the staged
file, then rename it atomically. Errors remove the staged file and preserve the
previous destination. CSV/TSV exports evaluated values from the active sheet;
quoted text is escaped, and formula-like text is prefixed with an apostrophe.
Imports treat formula-like fields literally. Native workbooks retain equations,
formats, multiple sheets and figures. CSV imports do not set the native save
filename, so Save asks for a new workbook destination.

## Current bounds and compatibility

Eight sheets; 1024 rows and 128 columns per sheet; 8192 stored cells per workbook;
191 bytes of raw input per cell; formula/dependency depth 64; function argument
count 32; range evaluation budget two million visits per root calculation.
These are explicit resource bounds for the native desktop, not format promises
for future versions. Undo includes sixteen snapshots and consumes approximately
26 MiB; each instance has its own allocations.

Row/column insertion and deletion update absolute and relative references across
all sheets, resize referenced ranges and update live chart source ranges. Insertion
is refused when it would move populated cells beyond workbook bounds. Deleted
individual references become `#REF!`; deleting an entire chart source disables
the figure.

Autofill translates relative references, preserves absolute axes, repeats patterns
and extrapolates two-number seeds. Copy/cut/paste is an internal per-instance
range clipboard because the host ABI has no system clipboard service. Sorting
is stable and sorts selected rows by the first selected column; formulas relocate
with relative references. Sheet renaming rewrites matching named qualifiers across all formulas, including
quoted names and apostrophe escaping. Formula strings and unqualified references
remain unchanged. The entire rename is staged and refused if any rewritten
formula would exceed the cell limit, or if the name is invalid or already used.

Calculations use binary double precision, including monetary cells. Monetary
formatting does not provide decimal accounting arithmetic. There is no XLS/XLSX
compatibility, dynamic array formulas,
external workbook linking, macros, filtering, printing or recovery journal in
this version. These require separate implementations behind the module boundaries
above. Row/column sizes can be changed by dragging header edges or using the Format
menu. Rendering, hit testing, navigation, scrolling and autofill share the same
variable-size geometry. Resize drags form one undo step; Escape restores the
original size. Structural row/column edits move stored sizes with the cells.
Numeric values that cannot fit display hash marks rather than a misleading
truncated number. The formula bar retains
the complete raw input.

## Verification

`tests/budowin_spreadsheet.c` exercises the formula families, errors, lazy branches,
cycles, reference translation, autofill, structural edits, dates, sorting, undo/redo, native round-trips,
CSV escaping, malformed files, chart export, save/close transitions, drawing bounds
and smaller/resized clients. Additional layout regressions cover dragging
header edges, size limits/cancellation, all cell alignments, formula-safe renaming,
legacy workbook migration and all sixteen grid configurations.
`tests/budowin_spreadsheet_exports.py` independently parses SVG and decodes all
five RGB PCX chart types, validating axis rotation and grid orientation/counts. `tests/budowin_native.c` exercises module discovery,
managed-window interaction and `.BSHEET` association through the real host.
The spreadsheet test is part of `tests/run_budowin.sh` and also runs under
AddressSanitizer/UndefinedBehaviorSanitizer.
