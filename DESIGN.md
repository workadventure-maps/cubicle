# Cubicles

An original falling-block versus game for the Super Nintendo, for 1 to 4 players around a WorkAdventure arcade
cabinet (cloud-game, libretro snes9x core). Sister game of Bombadventure, built the same way.

It is a game in the falling-block genre, not a Tetris recreation: no Tetris name, logo, music or look, and our
own well size and rules.

## Decisions (2026-10-31)

| Topic | Decision |
|---|---|
| Name | Cubicles (office cubicles are blocks). Chosen over "Tetradventure", to keep clear of the Tetris trademark. Checked on 2026-10-31: no falling-block game uses it (a freeware laser puzzle is called Cubicle). Repository: workadventure-maps/cubicle. |
| Platform | SNES (LoROM), C with PVSnesLib, assembly only where measured. Must run on snes9x (cloud-game). |
| Players | 1 to 4. P1 on port 1, P2 to P4 through a Multitap on port 2. |
| Mode | Versus first: one well per player, cleared lines send junk rows. Co-op (one shared wide well) maybe later, as a second mode. (Added 2026-11-01 as "open space", see below.) |
| Screen | Only the wells in play are shown, centered. One well size for every player count: 7 columns by 20 rows of 8px blocks (four 56px wells plus separators fill 256px). Layout changes only between matches. Keep HUD text out of the top-left corner, where the cabinet badge sits. |
| Controls | Left/Right move (auto-repeat), Down soft drop, Up, A or X turns clockwise, B or Y drops at once (hard drop), Start joins. No counter-clockwise turn. (Changed the same day: Up was the hard drop and B/Y turned counter-clockwise.) No pause. No hold piece for now; it was ruled out because we thought the cabinet only had A, B and Start, but its keyboard also maps C, V to X, Y, A, S to L, R and Shift to Select, so a hold on L/R is possible later. Select (Shift on the cabinet keyboard) switches the mode of the next game (added 2026-11-01). |
| Joining | Start on the title begins a game. Alone: endless solo game. A second player's Start interrupts a solo game ("new challenger"): its score still counts, a short countdown, then versus. During a versus match, Start reserves a spot (READY shown) and the next match includes that player. |
| Away rule | The ROM can't see a player leave: the cabinet frees the slot (or hands it to the longest watcher) and the pad just goes quiet. Nobody is put out during a match: a well left alone fills up and tops out by itself. A pad that touched nothing during a whole match sits out the next one until Start is pressed on it. Nobody left for the next match: back to the title. (First version, changed the same day: a pad silent for 15 seconds was out of the match at once. In play-testing that felt like a bug.) |
| Versus | Last well standing wins the match. Win counter per player, kept until the cabinet reboots. Fall speed rises every 30 seconds, so matches end within about 3 minutes. |
| Attacks | Clearing n lines at once sends n-1 junk rows to every other living well (tunable). Junk rows have one gap, same column for one attack. |
| Pieces | The 7 four-block pieces, dealt from a shuffled set of all 7. One seed per match: every player gets the same sequence at their own pace. One next-piece preview. Landing shadow drawn as an outline. |
| Rotation | Our own simple kicks: try in place, then 1 column left, 1 right, 2 left, 2 right (the 2-column tries let an upright I piece against a wall turn flat). Lock delay 30 frames, restarted by up to 8 moves. |
| Solo | Endless score attack: speed rises with lines cleared, score until top-out. Best score shown on the title while the cabinet runs (no saves: a cabinet always boots fresh). |
| Look | Plain bevelled blocks in our own colors (no piece gets the color other games give it). Wells are cubicles: partition panels between them. Above each well, the player's Woka (21x21, hand-drawn frames from Bombadventure's `assets/sprites/`: shrinking the 32x32 Woka blurs it) hops on clears, looks around nervously when the stack is within 6 rows of the top, and turns its back when out. Wokas' outfits follow the cabinet badge colors (P1 red, P2 blue, P3 green, P4 yellow-ish). Title: CUBICLES spelled in blocks, the four Wokas full size; generated title art can come later. |
| Art rights | Only Woka parts designed and owned by WorkAdventure (`admin/public/wokas`), as in Bombadventure. |
| Sound | Placeholder effects: lock, clear, junk incoming, join, top-out. No move or rotate sound (starting a sound costs ~40 scanlines, and four players move all the time). |
| Music (2026-11-01) | Solo games only: in versus and open space, the players around the cabinet talk to each other. Grieg's "In the Hall of the Mountain King", from the public-domain Mutopia score (#1888, Grieg's own piano version), arranged by `tools/mkmod.py` into `music/mountain_king.it` with synthesized chip instruments; the .it is the source now (edit it in a tracker). It follows the game: pattern 0 (the opening bar) is never played; patterns 1-20 form 7 groups of rising tension (1-4, 5-6, 7-10, 11-12, 13-16, 17-18, 19-20); a group always plays to its end, then the stack height picks the next group, up or down (rows 0-6, 7-8, 9-10, 11-12, 13-14, 15-16, 17+); the tempo follows the level, 138 BPM on level 1 to 238 on level 9 and up. Game over lets the current group play to its end, then plays the ending (21-22), then silence; the GAME OVER screen stays until the ending is over or a key is pressed (after its usual 3 seconds), and leaving early stops the music. A challenger interrupting a solo game stops the music (nobody lost). Since patterns play in any order, each one starts channels 1-6 with a note or a note-off (`===`), or a note from the previous pattern can ring forever: keep that when editing the .it, `make test` checks it. |
| License | MIT, code and ROM, public under workadventure-maps like Bombadventure. |

## Open space: the co-op mode (2026-11-01)

The partitions come down: one shared well for the whole team.

| Topic | Decision |
|---|---|
| Choosing it | On the title, Left/Right on any pad switch between `< VERSUS >` and `< OPEN SPACE >`. Select on any pad switches the next game's mode at any time ("NEXT: OPEN SPACE" shows for 2 s); the game in progress keeps its rules. The countdown names the mode. Alone, both modes are the solo game; a second player's Start interrupts it as in versus ("NEW COWORKER!"). |
| Well | 7 columns per player (14, 21 or 28) by 20 rows, so every player places as many pieces per line as alone. Each player's Woka, label and next piece sit above their own 7 columns, and their first piece comes in there, but pieces can go anywhere. Each next piece comes in above where the player's last one locked (its box column, moved in if a piece turned against a wall would stick out), so nobody walks back across the well every time (2026-11-03). Only the two outer walls are drawn. |
| Colors (2026-11-03) | Every block is in its player's badge color (P1 red, P2 blue, P3 green, P4 yellow, like the labels), not its type's: the team sees who placed what. The next-piece box too. Same block tiles as blocks_a, in a fourth BG palette (5). |
| Teammates' pieces | Solid. A piece can't move or turn into a teammate's falling piece. A piece resting on one neither falls nor locks: it waits. A hard drop onto one stops on it without locking. A new piece whose place is taken by a teammate's piece waits (no top-out); one blocked by the stack ends the game. The landing outline shows where the piece lands on the stack, ignoring teammates' pieces, and is only drawn on empty cells. |
| Clears | Full rows across the whole width. When they collapse, falling pieces above them move down with the rows: everything in the well shifts together, and each piece keeps its place relative to the stack (so nothing needs redrawing). |
| Score and speed | One team score with the solo rules: points times level, a level every 10 lines, solo fall speeds. No junk. Best team score shown on the title (TEAM), kept until the cabinet reboots. |
| End | The team is out when a new piece can't come in or a piece locks above the well: GAME OVER, the well grays out once. Everyone who touched their pad plays the next game, in the selected mode. |
| Coffee break | A player who touches nothing for 10 seconds gets no new piece once their current one has landed, and their Woka turns its back; any button brings them back. Unlike the dropped 15-second rule in versus, nobody is put out: the pieces just stop coming, so someone who left can't sink the team with pieces piling up in their columns. Everyone on break ends the game ("EVERYONE ON BREAK"); players still on break at the end sit out the next game. |
| Joining | Start during a team game reserves a spot for the next game (READY), as in versus. Joining a running game (the well widening by 7 columns) could come later. |
| Screen | Team score, lines and level on row 1, player labels on row 2, Wokas 2 px lower than in versus. A well of even width is centered with BG1/BG3 scroll 0 instead of 4. |

## Prototype milestone

Versus for 1 to 4 players and solo score attack, with the joining, challenger, away, speed and junk rules above,
win counter, title screen, placeholder effects, headless tests (rules, VBlank, frame budget), deployed to the
cabinet. Polish (music, attract demo, high-score table, clear animations) waits until people have played it.

## Technical notes

- **Layout.** Mode 1. BG1: wells, partitions, next pieces and the title logo (tiles at VRAM $2000: three sets of
  16 tiles, BG palettes 2-4; palette 5 shows blocks_a's tiles in the players' colors). BG3: text (font $6000, map $7000). BG1 map at $7400. BG1 and BG3 scroll 4 px left,
  so a well (7 cells) plus a separator column is 64 px and the first separator straddles both screen edges.
  BG2 is unused. Wokas: OAM entries 0-3, 32x32, all 8 frames of each player in VRAM at once ($0000-$1FFF), so a
  pose change is a tile number, no upload.
- **Board.** A 32-bit mask per row (two u16): columns in bits 3 to w+2 (w = 7 in versus, up to 28 in open
  space), the other bits are walls; 2 hidden rows above the well and 4 full rows below it, so a piece's 4-row
  box never reads outside its well. A falling piece is four 16-bit masks seen through a window starting at byte
  pcol / 8 of its rows, shifted by pcol % 8: a 4-column box always fits in it, and the 65816 loads 16 bits from
  any byte address, so a collision test stays four ANDs whatever the width. The BG1 map in RAM is the only
  record of block colors: rows that move copy their cells (MVN).
- **Shared well.** Teammates' falling pieces are ORed into an overlay with the board's layout; collisions test
  board | overlay, with the moving piece taken out of the overlay first. The ghost search and the "resting on
  the stack?" test have board-only entry points. Erasing a piece only clears cells that still show the tile it
  drew (a teammate may have drawn over them). A collapse marks the rows holding drawn piece or ghost cells,
  copies those rows even when they look empty, records where each row went, and moves every falling piece's box
  row, ghost row and drawn cells along with it.
- **Frame budget.** Compiled C was far too slow for the board: a piece move cost ~160 scanlines, collapsing a
  well ~250. Collision, landing row, piece drawing, locking, collapsing, junk, gray-out, the next-piece box and
  reading the pads are in `src/well.asm`; rules and screens stay in C. On top of that, work is spread over
  frames: at most 3 locks, spawns or piece redraws per frame (the rest waits a frame; an open-space lock counts
  2), one heavy board operation (collapse, junk, gray-out) per frame, a sound starts only on a quiet frame, and
  the result text waits a frame after the match ends. An open-space collapse (up to 28 columns, ~100 scanlines
  under a tall stack) only runs on a frame with no other work, and the other pieces wait that frame (their new
  presses are kept for the next). A falling piece with no input and no row to fall this frame costs nothing.
  One Woka sprite is updated per frame (all four in C cost 25 scanlines), and the score panel is redrawn one
  number per frame. Measured: 4 idle players cost 60 scanlines; busiest frames 177 (all four clear lines on the
  same frame), 161 (random 4-player battle with junk), 140 (open-space clear under a tall stack) and 177
  (random 4-player open-space game); `tests/smoke.py` fails above 200 or on any lag frame during play.
- **Presses.** The game finds new presses itself by comparing each pad with the previous frame it saw, not with
  PVSnesLib's `padsDown`, which is computed per VBlank: a frame that runs late would lose a press.
- **Compiler bug.** 816-tcc evaluates `a || !b` as 0 when `a` is true (any negation right of `||`). Write
  `b == 0` instead. It made Start do nothing on the title until found.
- **Slow division.** 816-tcc's `/` and `%` are library loops: printing the 4 numbers of the score panel with
  them cost ~110 scanlines. Numbers are printed by subtracting powers of ten, and the level counts its own lines.
- **VBlank.** RAM copies of both maps, dirty row ranges, at most 24 BG rows and 8 text rows per frame. Building
  a match redraws the whole screen in C and may lag a frame or two behind the countdown.
- **Music driver.** PVSnesLib's SNESMOD driver can't set the tempo from the game (pattern `Txx` is clamped to
  80-200 BPM) or pick the next pattern. `snesmod/sm_spc.asm` is an altered copy of PVSnesLib 4.6.0's driver
  source (zlib) with two commands: $0C sets the tempo, $0D queues the order to play when the current pattern
  ends ($FE stops there and reports position $FE, so the game knows the ending is over). The game re-queues the next order every frame during a group's last pattern, so the
  jump uses the stack height at the very end. PVSnesLib links its own driver inside `libc.obj`, so
  `src/music.asm` has `spcBootMusic` (its `spcBoot`, uploading ours) and the two calls, which go through
  PVSnesLib's command queue; all other `spc*` functions work unchanged. `spcPlay` resets the tempo to the
  module's, so the game sends the tempo after it. Starting the module keeps the driver busy 8-19 frames
  (clearing and waiting for the echo buffer), and `spcPlaySound` waits for queued commands first: a sound effect
  starts only once the queue is empty, or the game would freeze. Loading the module at boot takes ~35 frames.
  The driver's timer runs ~2% slower than a tracker at the same tempo. Memory: driver to $19C1, module from
  $1A00 (22 KB), echo buffer (6 KB) under the effects' stream region at the top.
