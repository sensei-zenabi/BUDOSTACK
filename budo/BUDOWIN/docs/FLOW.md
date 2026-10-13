# Flow native application architecture

`appsrc/flow.c` provides the native editor and host integration (minimum BWA 1.14).
On 1.16 hosts, recovery autosaves run from the idle callback without input or
repainting, including while the editor window is minimized. Earlier hosts use
the existing draw-driven fallback.
`flow_model.h` defines bounded graph/project records and orthogonal routing.
`flow_io.h` implements transactional parsing and atomic project saves.
`flow_render.h` draws the canvas and exports SVG/8-bit PCX.
`flow_font.h` and `flow_encoding.h` use BUDOWIN's existing bitmap glyphs and
Nordic/UTF-8 boundary conventions. `.FLOW` defaults to the `flow` application;
saved user association settings retain precedence.

## Model and limits

A project contains eight diagrams, sixteen reusable node prototypes, and four
custom diagram templates. Each diagram has up to 64 nodes and 96 directed edges
on a 1200×800 pixel page. IDs are unique within a diagram. Labels/names hold
63 UTF-8 bytes and descriptions 255 bytes. The editor keeps 24 project states
(current state plus up to 23 undo steps); edits after undo discard redo states.
Group copy preserves internal edges and paste allocates new IDs. Deleting a
node removes incident edges in the same undoable edit.

Coordinates snap to a ten-pixel grid. Dragging also aligns nearby node edges
and centers. Routes use shortest paths on the ten-pixel grid, treating node
bounding boxes as obstacles, then compress collinear points. Opposite-direction
edges use different ports. Unroutable paths remain visible as red dashed lines.
Routes are cached until diagram geometry changes. Sequence participants have
lifelines; message edges use explicit vertical lanes, adjustable by dragging.

## Editable project format (version 1)

Files are UTF-8 text. Blank lines and `#` comments are accepted. Quoted strings
escape `\\`, `\"`, `\n`, `\r`, and `\t`. Unknown directives, invalid UTF-8,
truncated records, invalid geometry, duplicate IDs, and dangling edges reject
the complete load, preserving the currently edited project.

```text
FLOW 1
DIAGRAM 0 4 "Example" "A flowchart"
NODE 1 2 100 40 100 50 "Start" ""
NODE 2 0 100 140 100 50 "Process" "Description\nSecond line"
EDGE 3 1 2 150 "" ""
END
ELEMENT 4 100 50 "MCU" "Reusable block"
DONE
```

`DIAGRAM type next_id name description` begins a diagram. `TEMPLATE` uses the
same fields and records, storing a custom template rather than an active page.
`NODE id shape x y width height label description` defines an element.
`EDGE id source_id destination_id sequence_lane label description` defines an
arrow. `ELEMENT shape width height label description` defines a reusable node.
`END` closes a diagram/template; `DONE` terminates the project. The next ID
must exceed every used ID. Geometry stays inside a 20-pixel page margin;
minimum node size is 50×30. Sequence lanes are 100–740 pixels.

| Type | Value |
| --- | --- |
| Flowchart | 0 |
| Sequence | 1 |
| State machine | 2 |
| System/block diagram | 3 |
| Control/signal flow | 4 |

| Shape | Value |
| --- | --- |
| Process | 0 |
| Decision | 1 |
| Start/end | 2 |
| State | 3 |
| Block | 4 |
| Participant | 5 |
| Note | 6 |

## Saving, recovery, and exports

Project and export writes use a unique temporary file beside the destination,
flush/fsync its data, and rename it over the destination. Failed writes before
rename preserve the original file. Dirty state compares project content against
the last successful save, including when undo returns to a saved state.

Each window owns a private `~/.budowin-flow/flow-PID-XXXXXX` recovery file.
Autosave follows completed edits after two seconds, and no more frequently than
five seconds while unchanged. It is deferred during dragging, property editing,
and confirmations. Recovery scans ignore live processes and offer the newest
orphan snapshot. Recovery loads into an untitled dirty project, retains the old
snapshot until a fresh recovery copy succeeds, and requires Save As for the
chosen project path. Explicit discard removes that snapshot. A forced close
attempts a final recovery save. Normal successful save or explicit discard
removes the current recovery snapshot. A failed autosave is reported and stops
retrying until another edit; absence of a writable recovery directory is shown
in the status bar. Recovery stores project content, not unfinished text edits.

SVG exports the current page with shapes, paths, arrowheads, labels, descriptions,
and participant lifelines. XML content is escaped. PCX exports the same page
with the BUDOWIN bitmap font, eight colors, a 256-entry palette, one 8-bit plane,
and per-scanline PCX RLE. Neither export contains editor chrome or the alignment
grid. Projects preserve Unicode text; bitmap display uses the host's existing
ASCII/Nordic repertoire. Exported SVG retains Unicode labels.
