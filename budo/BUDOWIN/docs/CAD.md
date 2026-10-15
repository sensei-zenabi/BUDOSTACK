# CAD native module and drawing format

`appsrc/cad.c` builds to `APPS/CAD.BWA`, with application ID `cad` and
minimum host ABI 1.14. The desktop associates `.BCAD` files with this module.
The application uses SDK theme roles, menus, buttons, input carets and the
host's asynchronous file and save-confirmation dialogs. Each native instance
owns its document, history, selection and clipboard.

## Model and geometry

`cad_model.h` defines a bounded document: 2,048 entities, 16 layers and a
32-state undo history. Coordinates use double precision, millimetres and a
Cartesian Y-up axis. Stored coordinates are limited to +/-1,000,000 mm.
Grid spacing ranges from 0.1 to 10,000 mm. The canvas uses the SDK default
5x7 font at one device pixel per bit, with six-pixel character advance, for
text, dimensions and leader notes at every zoom. Font size is not editable.
Legacy annotation heights (0.1..10,000 mm) remain in the file format for
existing drawings, dimension-arrow geometry and SVG/DXF export; numeric
property edits preserve them. New annotations use a fixed 16 mm model height.

Entities store type, zero-based layer index, three points (`a`, `b`, `c`),
annotation height and up to 127 bytes of notation text. Type values are:

| Value | Entity | Point interpretation |
| --- | --- | --- |
| 1 | Line | `a` and `b`: endpoints |
| 2 | Rectangle | `a` and `b`: opposite corners |
| 3 | Circle | `a`: centre; `b`: point on circumference |
| 4 | Horizontal dimension | `a`, `b`: measured points; `c`: dimension offset |
| 5 | Vertical dimension | `a`, `b`: measured points; `c`: dimension offset |
| 6 | Aligned dimension | `a`, `b`: measured points; `c`: normal offset |
| 7 | Text | `a`: lower-left text anchor |
| 8 | Leader note | `a`: arrow tip; `b`: elbow; `c`: text anchor |

Dimensions compute their labels from their own measured coordinates. They are
independent annotation entities, with no references or constraints linking
them to other geometry. Moving a whole selection moves its dimensions too;
editing unrelated geometry does not update separately selected dimensions.
Text is horizontal. Drawing and exports share dimension/leader construction.
Circle display and hit testing use analytic geometry; circles export as
true circles. Generic geometry traversal tessellates them for bounds.

Layers store a name (31 bytes), RGB color, visibility, lock, dashed flag and
line width (0.01..10 mm). Hidden layers neither draw nor participate in snaps.
Locked layers remain visible and snappable but cannot be selected or modified
by geometry operations. Pasting uses the active layer. Layer properties are
an explicit administrative operation and can unlock a layer.

Grid and object snapping are independent. Object snaps cover line endpoints
and midpoints, rectangle corners, circle centres/cardinal points, and
annotation anchors. Orthogonal mode constrains line/leader construction.
Selection movement is one history transaction; Escape restores its baseline.
Screen lines are clipped before rasterization to bound work at extreme zoom.
Font pixels clip individually at canvas edges. Text hit testing and box
selection use the fixed screen-font footprint transformed back to model
coordinates. Zoom 100% (toolbar, View menu, or `0`) restores the initial
0.5 pixels/mm scale while retaining the viewport centre; the status bar shows
the zoom percentage relative to that default.

Horizontal and vertical SDK scrollbars remain visible beside the canvas.
Arrow buttons and the mouse wheel pan by 40 screen pixels; track clicks pan
by a page, and the thumbs support dragging with pixel-resolution positions. The scroll range covers the
visible drawing and viewport with room to pan into empty space. It expands
between gestures and remains fixed during a thumb drag or held arrow.
Wheel up/down pans vertically; holding either Shift key pans horizontally
(up = left, down = right). Arrow keys pan on their respective axes. Panning
also works between point placements without changing existing model points.
Navigation does not modify drawing content or undo history. The host delivers
the wheel event's captured modifier state to native key callbacks, so a Shift
release queued before dispatch does not change that wheel event's direction.

## BCAD version 1

`cad_io.h` writes a portable, line-oriented text format with LF line endings.
Numbers use C-locale decimal notation; the native host does not change that
locale. Strings are hexadecimal byte pairs in BUDOWIN's text encoding; `-`
represents an empty string. Strings cannot contain control bytes.

```text
BCAD 1
<entity-count> <layer-count> <grid-mm>
<rgb-decimal> <visible> <locked> <dashed> <width-mm> <name-hex>
... one record per layer ...
<type> <layer> <ax> <ay> <bx> <by> <cx> <cy> <height-mm> <text-hex>
... one record per entity ...
END
```

All points and heights are serialized even when unused by the entity type.
Unused storage is zero-initialized. Load parses into a separate allocation,
rejecting overflow, nonfinite values, invalid indices, unsupported versions,
degenerate geometry, truncation and trailing records. Failure preserves the
live document and its undo history. Save and export use a sibling temporary
file, flush, `fsync`, close and atomic rename; failures preserve the previous
file. Existing destination permissions are retained.

New, open and close use the host's save/discard/cancel workflow. A pending
action proceeds only after a successful save or explicit discard. Failed or
cancelled Save As leaves the live document intact. Export cannot replace the
current project through the same path, a symlink or a hard-link alias.

## Export contracts

SVG exports visible layers, escaped Nordic notation text, physical millimetre
width/height, and a matching viewBox with a 10 mm drawing margin. Its Y-down
coordinates are converted from the model. It can be printed at actual size
or scaled by the receiving application; no page-layout or print-driver API is
part of this module.

DXF exports an ASCII AC1015 drawing with `$INSUNITS=4` and `$MEASUREMENT=1`.
It preserves all layers, including hidden and locked layers, colors and dash
styles. Layer names use `L<index>_<name>` with illegal characters replaced.
Nordic text uses DXF Unicode escapes. Layer widths map to the nearest supported
DXF line weight. Circles remain CIRCLE entities. Other geometry and dimensions
export as LINE/TEXT primitives; exported dimensions are not associative DXF
DIMENSION entities. SVG and DXF import are outside this version.

Regression coverage lives in `tests/budowin_cad.c`,
`tests/budowin_cad_exports.py`, and the desktop's native integration test.
Run them through `tests/run_budowin.sh`.
