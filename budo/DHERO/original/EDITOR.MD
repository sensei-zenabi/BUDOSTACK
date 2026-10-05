DUNGEON HERO - LEVEL EDITOR

EDITOR.EXE is a separate Mode 13h campaign editor launched from the game's OPTIONS popup.

BUILD

Build a second RHIDE executable named EDITOR.EXE with these source files:

- EDITOR.C
- PCX.C
- DATA.C

Do not add EDITOR.C to the DUNGEON HERO game executable. The normal game target remains:

- MAIN.C
- PCX.C
- DATA.C

Keep EDITOR.EXE in the same working directory as the game executable. In OPTIONS, press E to launch it. The game restores FULL000.PCX and the Options popup when the editor exits.

PURPOSE

The editor modifies the existing ten campaign files directly:

DATA/GFX/MAP/LEVEL1.DAT
...
DATA/GFX/MAP/LEVEL10.DAT

This makes it possible to build a custom ten-level campaign or rebalance the supplied campaign without manually editing map coordinates.

The editor uses the same TILE and CHAR graphics as the game and displays the current level in VGA Mode 13h. The white/gray double rectangle is the edit cursor.

CONTROLS

Arrow keys
Move the edit cursor.

Tab
Cycle tools:

TILE -> HERO -> ENEMY -> DOOR -> CHEST -> ITEM -> EXIT

+ / -
Cycle the current TILE, ENEMY type, or ITEM selection when the corresponding tool is active.

Enter
Apply the current tool at the cursor.

Delete
Remove an ENEMY, DOOR, CHEST, or ITEM at the cursor.

R
Rotate the HERO start direction or an ENEMY at the cursor.

G
With CHEST selected, cycle chest gold through 0, 10, 25, and 50.

I
With CHEST selected, cycle the chest item through ITEMS.DAT and NONE.

F1
Open the in-editor help panel.

F2
Save the current changed level.

Page Up / Page Down
Change between LEVEL1 and LEVEL10. Unsaved changes must be saved or discarded first.

Esc
Exit. If changes are unsaved, the editor asks for a second Esc before discarding them.

TOOLS

TILE
Select a tile with +/- and paint it with Enter. The selectable list comes from the level's existing LEGEND, so tile behavior remains WALK/BLOCK according to the DAT file.

HERO
Enter moves the hero START to a walkable cursor tile. R rotates UP -> RIGHT -> DOWN -> LEFT.

ENEMY
Select an enemy type from ENEMIES.DAT with +/-. Enter places or changes the enemy at the cursor. Enemies can only be placed on walkable tiles. R rotates the enemy. Delete removes it.

DOOR
Enter creates a DOOR interaction and paints DOOR_CLOSED. The opened replacement is DOOR_OPEN. Delete removes the interaction and restores FLOOR when FLOOR exists in the legend.

CHEST
Enter creates a CHEST and paints the CHEST tile. New chests start with 0 gold and NONE item. G changes gold. I changes the item. Delete removes the chest and restores FLOOR.

ITEM
Select an item from ITEMS.DAT with +/-. Enter places or changes a loose ITEM pickup on a walkable tile. Its replacement tile is FLOOR. Delete removes it.

EXIT
Enter moves the level exit/stairs to the cursor. The editor keeps one campaign exit and automatically links LEVEL1->LEVEL2, ... LEVEL9->LEVEL10, and LEVEL10->END. If STAIRS exists in the legend, the cursor tile is painted STAIRS.

SAFE SAVING

F2 does not overwrite the level blindly. The editor:

1. writes LEVELn.TMP
2. reloads the temporary file through data_load_level()
3. aborts the save if the parser rejects it
4. moves the previous LEVELn.DAT to LEVELn.BAK
5. installs the validated new LEVELn.DAT

The backup therefore contains the previous saved version of that level.

SCOREBOARD RESET

Every successful save of a changed level deletes:

DATA/GAME/SCORES.DAT

This is intentional. A modified campaign may be easier, harder, or have different gold rewards, so scores from an earlier campaign configuration are not comparable. Merely opening the editor, viewing a level, or exiting without saving changes does not reset the scoreboard.
