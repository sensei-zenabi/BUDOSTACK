DUNGEON HERO - CONTENT WORKFLOW

Color Palette: CGA1 HIGH (Black and White)

The game is designed so normal content development is done by drawing PCX sheets and editing DAT files.

GRAPHICS

Dungeon tiles:

DATA/GFX/TILES/TILE000.PCX
DATA/GFX/TILES/TILE000.DAT

Character sprites:

DATA/GFX/SPRITES/CHAR000.PCX
DATA/GFX/SPRITES/CHAR000.DAT

Tile/sprite DAT format:

CELL 16 16
FLOOR 0 0
WALL  1 0

Coordinates are zero-based grid cells. Sheets may grow to any number of rows or columns.

CHAR000 is the single character sprite sheet for the whole game. Every character has exactly one 16x16 sprite. Facing direction is NOT drawn into the PCX; the engine draws a small three-pixel direction marker on the corresponding edge of the character cell.

Current CHAR000.DAT layout:

CELL 16 16
TRANSPARENT TOP_LEFT

HERO          0 0
GOBLIN        1 0
SKELETON      2 0
ORC           3 0
MUMMY         0 1
CHAOS_WARRIOR 1 1
GARGOYLE      2 1

Therefore the minimum CHAR000.PCX required by the current campaign is 64x32 pixels. The sheet may be expanded later by adding more named 16x16 cells to CHAR000.DAT.

Existing artwork:
- DATA/GFX/FULL/FULL000.PCX - title/menu
- DATA/GFX/TILES/TILE000.PCX - dungeon tiles

Artwork still required:
- DATA/GFX/SPRITES/CHAR000.PCX - hero and every enemy character

ENGINE UI PALETTE

The in-game HUD, pop-up windows and direction markers do not depend on PCX palette colors. After loading an artwork palette, the engine rewrites reserved VGA palette entries to fixed monochrome values:

0   black
252 dark gray
253 medium gray
254 light gray
255 white/light gray

Palette indices 0 and 252-255 are therefore reserved for the engine and should not be used for important artwork colors.

STARTUP

The game starts with a generated Mode 13h BUDOSTACK PRESENTS screen. It uses a built-in black/gray/white palette and a short PC-speaker ident, remains visible for approximately three seconds, and then continues to the normal menu PCX. It is shown once per program launch.

OPTIONS

The OPTIONS menu item is active. ENTER opens a monochrome text pop-up over the menu.

Settings:
- Sounds: ON / OFF
- Language: ENG / FIN

Use Up/Down to select a setting. Left/Right or Enter changes it. Esc closes the pop-up.

Sound OFF disables the generated PC-speaker effects immediately and remains in effect for the current program session.

LOCALIZATION

All editable user-facing gameplay/UI text is stored in language dictionaries:

DATA/LANG/ENG.DAT
DATA/LANG/FIN.DAT

LANG.H loads these files at startup. Dictionary syntax is deliberately simple:

KEY=TEXT

Lines beginning with # or ; are comments. Empty lines are ignored.

Example:

HUD_GOLD=GOLD
MSG_DOOR_OPEN=DOOR OPENED.
ITEM_POTION=POTION

The Finnish file uses exactly the same symbolic keys:

HUD_GOLD=KULTA
MSG_DOOR_OPEN=OVI AUKI.
ITEM_POTION=ROHTO

This means Finnish wording and abbreviations can be tuned without recompiling the game. The ENG dictionary is the fallback dictionary: if a key is missing or empty in FIN.DAT, the engine uses the matching value from ENG.DAT. If the key is also missing from ENG.DAT, the symbolic key itself is displayed, making missing localization data visible during testing instead of crashing the game.

Format placeholders must be preserved when editing formatted strings. For example:

MSG_ENEMY_DOWN=%s DOWN +%dG +%dXP
MSG_CHEST_GOLD=CHEST +%d GOLD
COMBAT_ATTACK_ROLL=ATTACK : %d + %d = %d

A translation may reorder surrounding words, but it must retain compatible %s, %d or %ld placeholders required by that key.

The dictionaries cover:
- Options labels/help
- HUD labels and controls
- inventory titles/help/item descriptions
- combat-roll popup and results
- gameplay and combat messages
- item display names
- enemy display names
- end-of-run statistics
- player-name prompt
- TOP 10 scoreboard
- load/error messages

Internal gameplay identifiers remain unchanged. For example, ENEMIES.DAT still uses GOBLIN and ITEMS.DAT still uses POTION regardless of selected language. Localization only changes what is displayed to the player.

The DOS build uses ASCII-safe Finnish spellings such as AANET, KAYTA and TAYNN instead of relying on a particular DOS code page for accented characters. HUD labels are abbreviated where necessary so Finnish text fits the same 320x200 / 40-column interface as English. FIN.DAT is therefore the intended place to review and improve Finnish terminology while keeping line lengths within the UI.

LEVELS

Campaign files:

DATA/GFX/MAP/LEVEL1.DAT
...
DATA/GFX/MAP/LEVEL10.DAT

Each level contains:
- SIZE and sheet names
- LEGEND: map symbol -> symbolic tile + WALK/BLOCK
- MAP
- START position and initial direction
- DOOR interactions
- CHEST rewards
- optional ITEM pickups
- ENEMY placements and initial directions
- EXIT destination

All levels reference the same character sheet:

HERO CHAR000

Directions are plain words and are independent from sprite graphics:

START 2 2 DOWN
ENEMY GOBLIN 9 3 LEFT
ENEMY ORC 15 8 UP

Valid directions are UP, DOWN, LEFT and RIGHT.

Example level transition:

EXIT 18 1 LEVEL2

The final level uses:

EXIT 18 1 END

Reusable game balance lives in:

DATA/GAME/ITEMS.DAT
DATA/GAME/ENEMIES.DAT

All enemy definitions reference CHAR000. A level only names the enemy type and position; its sprite is resolved by the same symbolic name in CHAR000.DAT.

GAMEPLAY

The campaign is a single-run, no-save dungeon quest inspired by solo board-game dungeon crawling.

Controls:
- Arrow keys: move; moving into an enemy attacks it
- Enter: interact with/attack the object or enemy in front
- Space: open/close inventory
- Esc: abandon the current run; inside a pop-up it closes that pop-up instead

FOG OF WAR / LINE OF SIGHT

The engine computes a strict 360-degree field of view from the hero after every turn.

- Only cells currently in line of sight are visible.
- Everything outside current line of sight is completely black.
- Previously visited or previously seen cells are NOT remembered or displayed.
- Enemies, stairs, chests, doors and all other map content disappear again when they leave current line of sight.
- Walls and other blocking map geometry stop sight.
- Closed doors block sight.
- Open doors do not block sight, so rooms and corridors beyond them become visible when there is a clear line.
- Chests do not block sight.
- A blocking wall/door cell itself may be visible, but cells behind it are hidden.

Visibility is permissive at the tile level so an empty room behaves naturally. The hero does not look only from tile-center to tile-center. Instead, the engine tests the center and four inset corners of each target tile. A tile is visible when any meaningful part of it can be seen.

This means:
- All open cells of an unobstructed rectangular room are visible from any open position in that room.
- The visible inside faces/corners of the room boundary are also drawn, so an empty room does not acquire artificial dark wedges at its far corners.
- A wall that merely grazes one sight ray does not hide a tile if another clear part of that tile is visible.
- Walls still hide all floor, enemies and objects genuinely behind them.

Diagonal corner handling remains strict:
- Sight rays are traced at sub-tile resolution.
- When a ray crosses diagonally from one tile into another, both orthogonal side tiles are checked.
- If both side tiles are opaque, a non-blocking target behind that sealed diagonal corner is hidden.
- An opaque wall tile at the visible corner itself may still be drawn as part of the room boundary.
- Open diagonal space remains visible when the geometry actually provides a clear path.

Enemy detection uses the same permissive line-of-sight ray test as the hero. The additional boundary-wall rendering is visual only and never lets enemies see through walls.

ENEMY AI

After each successful hero action, enemies receive a turn.

- If an enemy can currently see the hero, it becomes alerted and moves toward the hero.
- Adjacent enemies with line of sight attack.
- Alerted enemies remember the hero's last visible position and continue toward it after line of sight is broken.
- If an alerted enemy reaches the last-seen position without finding the hero, it stops searching and returns to wandering.
- Enemies that have not seen the hero wander in random legal directions and sometimes remain still.
- Walls and closed doors therefore prevent enemies from automatically knowing where the hero is.

The direction marker shows which way each character currently faces.

COMBAT / DICE ROLLS

Combat is intentionally interactive and board-game-like instead of deterministic.

Whenever the hero attacks an enemy, or an adjacent enemy attacks the hero, a monochrome COMBAT ROLL pop-up opens over the dungeon. The player must press ENTER to roll the dice. The result screen then shows both raw d6 results, both modifiers, the final totals, and whether damage was dealt. ENTER closes the result and resumes the turn.

Both sides use the same opposed-roll rule:

attack total  = 1d6 + ATTACK
defense total = 1d6 + DEFENSE

If attack total is greater than defense total, the attack hits.

Damage = attack total - defense total

If attack total is equal to or lower than defense total, the defender blocks/avoids the attack and takes zero damage. Ties therefore favor the defender.

Hero ATTACK is the hero's base attack plus the equipped weapon bonus. Hero DEFENSE is base defense plus equipped armor/shield defense. Enemy ATTACK and DEFENSE come from DATA/GAME/ENEMIES.DAT.

This means equipment still has a strong effect, but no ordinary attack is guaranteed. A weak attacker can occasionally beat a strong defender with a favorable roll, while strong armor can completely stop attacks.

The combat pop-up is implemented in COMBAT.H and uses the same reserved black/gray/white UI palette as Options and Inventory. All combat-popup wording is editable in ENG.DAT / FIN.DAT using COMBAT_* keys. Miss messages use MSG_MISS and MSG_ENEMY_MISS.

INVENTORY

Inventory is a per-run part of the hero state and persists across all ten levels.

When a chest is opened:
- the chest's direct GOLD reward is added immediately
- any weapon, armor, potion or elixir reward is placed into inventory instead of being automatically used or equipped

Standalone ITEM pickups use the same rule. Pure gold items are converted directly to gold.

Press SPACE during gameplay to open the monochrome inventory pop-up. Up/Down selects an item. ENTER uses or equips it. SPACE or ESC closes the inventory without spending a turn.

Using/equipping an item counts as an action and enemies receive their normal turn afterward. Merely opening, browsing or closing inventory does not advance enemies.

Equipment behavior:
- equipping a weapon places the previously equipped weapon back into inventory
- equipping armor places the previously equipped armor back into inventory
- the starting DAGGER can therefore be recovered and re-equipped later

Consumables:
- POTION and ELIXIR remain stored until used
- a healing item is not consumed when HP is already full
- after successful use it is removed from inventory

This allows the player to save healing and stronger equipment for later levels instead of receiving every reward immediately.

LEVEL COMPLETION AND OPTIONAL COMBAT

A level is completed immediately when the hero reaches its EXIT/stairs coordinate. Enemies do not have to be defeated first.

This creates a deliberate risk/reward choice:
- Evading enemies can make a run faster and preserve HP.
- Fighting enemies gives gold and XP.
- More XP improves later survivability through level-ups.
- More gold directly improves the final score.
- Skipping combat may improve the speed bonus but usually reduces combat rewards.

The HUD still shows the number of living foes so the player can decide whether further fighting is worth the risk.

Hero progression persists across all ten levels:
- HP / maximum HP
- hero level and experience
- gold
- weapon
- armor/shield
- inventory contents

Level-ups increase base HP and attack and fully heal the hero.

PC SPEAKER AUDIO

All sound is generated directly through the IBM PC speaker. No WAV, MIDI or other external audio files are required. SOUND.H programs PIT channel 2 and the speaker control port and uses short millisecond delays suitable for DJGPP/DOS.

The game currently has distinct synthesized cues for:
- BUDOSTACK presentation ident
- menu intro music
- successful hero movement
- enemy movement; one short cue is emitted per enemy turn when at least one enemy actually moves
- opening doors and chests
- discovering an enemy as it enters or re-enters the hero's current line of sight
- reaching the stairs / level exit
- beginning a combat roll
- taking damage from an enemy
- killing an enemy
- leveling up
- using/equipping an inventory item

Enemy discovery is based on current fog-of-war visibility rather than permanent memory. If an enemy disappears behind a wall and later steps into sight again, the discovery cue may play again.

PC-speaker effects are intentionally short because speaker playback is synchronous: the game waits while each tone is emitted. The menu theme and major reward cues are longer, while movement and combat effects are brief.

HUD

The lower 40 pixels of Mode 13h are reserved for a five-line monochrome text HUD showing:
- current level, HP, hero level and XP
- gold and weapon
- armor and enemies remaining
- current interaction/combat message
- controls including SPACE inventory

The dungeon viewport is 20x10 tiles with camera scrolling; level DAT files may be larger than the visible viewport.

SCORE

There are no save states. NEW GAME starts a complete run at LEVEL1.

Final score uses exactly the two intended performance factors:

score = gold * 100 + speed bonus
speed bonus = max(0, 7200 - elapsed_seconds) * 5

Thus collecting more gold always improves score, while completing the campaign faster increases the time component. Optional combat creates a tradeoff between spending time to earn more gold/XP and escaping quickly for a larger speed bonus.

SCOREBOARD / TOP 10

At the end of a completed or failed run the scoreboard first shows the final run statistics and calculated SCORE. It asks for a player name of up to 20 characters. Abandoning a run with gameplay Esc does not submit a score.

After the name is entered:
- the score is inserted into the persistent high-score table
- entries are sorted from highest SCORE to lowest
- only the best 10 entries are retained
- the final TOP 10 table is displayed
- the current player's score and name are also shown below the table, even if the result did not enter the TOP 10

High scores are stored locally in:

DATA/GAME/SCORES.DAT

The file is created automatically on the first scored run. Its contents are plain text so they remain simple to inspect or reset. Blank player names use the SCORE_ANON value from the selected localization dictionary, and malformed score-file rows are ignored when loading.

The end statistics, player-name prompt and TOP 10 presentation are also dictionary-driven.

After the player presses a key to leave the TOP 10 screen, the program terminates directly without changing video mode or repainting the menu. The final scoreboard therefore remains visible on the DOS screen after the game exits.

BUILD NOTE

Build targets in RHIDE:
- MAIN.C
- PCX.C
- DATA.C

LANG.H, COMBAT.H, SOUND.H and SCORE.H are included by MAIN.C and are not separate build targets. Language DAT files are runtime data files and are not build targets. Other headers and DAT files are also not build targets.