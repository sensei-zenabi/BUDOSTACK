# BUDOWIN document review

Branch: `feature/budowin-document-improvements`, based on `dev` at
`d3c931421cb88ca229f684f90cc723450c1672e8`.

Build with `make clean all`; run regression checks with `bash tests/run_budowin.sh`.
Launch `./budo/budowin` from BUDOSTACK's graphical terminal for desktop review.

| Document request | Implemented behavior and review steps |
| --- | --- |
| General: Nordic editor input | Select NORD in Settings. Type äöåÄÖÅ, save/reopen a plain-text file, and verify characters and caret advancement. Queued navigation no longer discards later input. |
| General: Ctrl+Tab | Open multiple applications. Hold Ctrl and tap Tab to cycle through a visible application list; add Shift to reverse. Minimized applications restore when selected. |
| General: narrower scrollbars | Scrollbars use a shared 12-pixel width, including their drawing and hit targets. Check Editor, Explorer and Terminal. |
| Editor: standard selection | Shift+arrows/Home/End/Page Up/Page Down extend selection; Ctrl+Shift+arrows select words. Shift+click and dragging select text with scrolling at the edges. Verify Ctrl+A/C/X/V, replacement typing, Delete/Backspace and Undo/Redo. Clipboard is internal to BUDOWIN. |
| Editor: robust navigation scrolling | Open a document several screens long, including wrapped lines. Move down and back up with arrows and Page Up/Page Down; the caret stays visible. |
| Editor: mouse row offset | Scroll into the middle of a long wrapped document. Click several rows and wrapped segments; insertion follows the clicked visual row. |
| Explorer: row scrolling | In both Icon and List/Details views, use wheel, scrollbar and keyboard. The viewport advances by rows rather than full pages. |
| Explorer: delete files/folders | Create disposable files and a folder containing nested/hidden files. Select Delete and confirm. Deletion includes broken symlinks without following targets; failures report the affected name and system error. |
| Explorer: selectable Up | Single-click Up in List/Details: it highlights. Enter/double-click navigates to the parent. Arrow Up from the first file can focus Up; Arrow Down returns to files. |
| Explorer: icon name truncation | Open a folder with long names in Icon view. Truncated labels end in `...`; Details and hover reveal the name. |
| Terminal: embedded BUDOSTACK | Open Terminal, run `cmath`, enter `123456+7`, then exit the application. Output/prompt follow the session; the desktop stays open. The PTY runs the real launcher and command parser. Check terminal resizing, scrollback, Nordic output and native Budo graphics/input. TASK pixel/sprite/text/render and mouse commands are also handled inside the window. |
| Paint: View zoom/grid | Choose zoom from 100% through 3200% in View, or use +/-; at 800% and above the enabled grid is visible. The faint grid overlays pixels and does not alter saved artwork. |
| Paint: workspace usage | Create a new image: its initial dimensions use the available drawing area. Load a small icon and zoom it for editing; oversized images use scrollbars. |
| Paint: image/canvas sizing | Image → Image Size scales with nearest-neighbor sampling. Image → Canvas Size preserves positions and crops/extends at the bottom/right. Enter dimensions, use Tab between fields, Enter to apply or Escape to cancel; verify Undo/Redo. |
| Paint: familiar pixel-art behavior | Separate image and canvas resizing, exact nearest-neighbor scaling, stepped zoom, non-destructive grid, indexed colors and undo follow common pixel-art workflows. Research references are below. |
| Paint: PCX asset editing | Load repository sprites/icons, edit palette-indexed pixels and save/reopen. Dimensions and all 256 palette entries are preserved, including odd-width PCX padding. Supported images are 8-bit one-plane PCX, 1–2048 pixels per dimension. |
| Desktop: folder popup | Create a desktop folder, move shortcuts into it and double-click it. Its contents open in Explorer while the root desktop remains available. Shortcut references preserve original files. Built-in launcher icons remain on the root desktop. |
| Desktop: minimized applications | Minimize applications. Their labeled buttons appear at the bottom; clicking one restores and focuses it. |
| Desktop: top bar | Check centered “BUDOWIN by BUDOSTACK”, weekday at left, and local `YYYY-MM-DD HH:MM:SS` clock at right. Seconds update while idle. Maximized windows reserve the top bar. |

## Research references

Aseprite distinguishes [sprite/image size](https://www.aseprite.org/docs/sprite-size/)
from [canvas size](https://www.aseprite.org/docs/canvas-size/) and documents
[resizing](https://www.aseprite.org/docs/resize/). These informed the separate
resize operations and nearest-neighbor behavior for indexed sprites.

## Automated verification

The suite covers document selection and undo, Nordic input, wrapped cursor
mapping, upward scrolling, row-based Explorer navigation, Up focus, deletion
confirmation, Ctrl+Tab and minimized restoration. It also runs a real BUDOSTACK
PTY with interactive cmath and return to the prompt, tests fragmented ANSI/UTF-8
and OSC graphics, and exercises graphics packets, shared-memory frames and input.
The graphics integration test substitutes socket endpoint setup where the test
sandbox restricts Unix socket creation. Actual desktop interaction and visual
appearance should be reviewed on the graphical host.

Validation on this branch: `make clean all` and the complete BUDOWIN regression
suite passed. This build environment lacks SDL2 development files, so the
makefile skipped optional SDL demos; BUDOWIN and its five native modules built.
