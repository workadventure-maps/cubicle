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
| Mode | Versus first: one well per player, cleared lines send junk rows. Co-op (one shared wide well) maybe later, as a second mode. |
| Screen | Only the wells in play are shown, centered. One well size for every player count: 7 columns by 20 rows of 8px blocks (four 56px wells plus separators fill 256px). Layout changes only between matches. Keep HUD text out of the top-left corner, where the cabinet badge sits. |
| Controls | Left/Right move (auto-repeat), Down soft drop, Up, A or X turns clockwise, B or Y drops at once (hard drop), Start joins. No counter-clockwise turn. (Changed the same day: Up was the hard drop and B/Y turned counter-clockwise.) No pause. No hold piece for now; it was ruled out because we thought the cabinet only had A, B and Start, but its keyboard also maps C, V to X, Y, A, S to L, R and Shift to Select, so a hold on L/R is possible later. |
| Joining | Start on the title begins a game. Alone: endless solo game. A second player's Start interrupts a solo game ("new challenger"): its score still counts, a short countdown, then versus. During a versus match, Start reserves a spot (READY shown) and the next match includes that player. |
| Away rule | The ROM can't see a player leave: the cabinet frees the slot (or hands it to the longest watcher) and the pad just goes quiet. Nobody is put out during a match: a well left alone fills up and tops out by itself. A pad that touched nothing during a whole match sits out the next one until Start is pressed on it. Nobody left for the next match: back to the title. (First version, changed the same day: a pad silent for 15 seconds was out of the match at once. In play-testing that felt like a bug.) |
| Versus | Last well standing wins the match. Win counter per player, kept until the cabinet reboots. Fall speed rises every 30 seconds, so matches end within about 3 minutes. |
| Attacks | Clearing n lines at once sends n-1 junk rows to every other living well (tunable). Junk rows have one gap, same column for one attack. |
| Pieces | The 7 four-block pieces, dealt from a shuffled set of all 7. One seed per match: every player gets the same sequence at their own pace. One next-piece preview. Landing shadow drawn as an outline. |
| Rotation | Our own simple kicks: try in place, then 1 column left, 1 right, 2 left, 2 right (the 2-column tries let an upright I piece against a wall turn flat). Lock delay 30 frames, restarted by up to 8 moves. |
| Solo | Endless score attack: speed rises with lines cleared, score until top-out. Best score shown on the title while the cabinet runs (no saves: a cabinet always boots fresh). |
| Look | Plain bevelled blocks in our own colors (no piece gets the color other games give it). Wells are cubicles: partition panels between them. Above each well, the player's Woka (21x21, hand-drawn frames from Bombadventure's `assets/sprites/`: shrinking the 32x32 Woka blurs it) hops on clears, looks around nervously when the stack is within 6 rows of the top, and turns its back when out. Wokas' outfits follow the cabinet badge colors (P1 red, P2 blue, P3 green, P4 yellow-ish). Title: CUBICLES spelled in blocks, the four Wokas full size; generated title art can come later. |
| Art rights | Only Woka parts designed and owned by WorkAdventure (`admin/public/wokas`), as in Bombadventure. |
| Sound | Placeholder effects: lock, clear, junk incoming, join, top-out. No move or rotate sound (starting a sound costs ~40 scanlines, and four players move all the time). No music yet. |
| License | MIT, code and ROM, public under workadventure-maps like Bombadventure. |

## Prototype milestone

Versus for 1 to 4 players and solo score attack, with the joining, challenger, away, speed and junk rules above,
win counter, title screen, placeholder effects, headless tests (rules, VBlank, frame budget), deployed to the
cabinet. Polish (music, attract demo, high-score table, clear animations) waits until people have played it.

## Technical notes

- **Layout.** Mode 1. BG1: wells, partitions, next pieces and the title logo (tiles at VRAM $2000: three sets of
  16 tiles, BG palettes 2-4). BG3: text (font $6000, map $7000). BG1 map at $7400. BG1 and BG3 scroll 4 px left,
  so a well (7 cells) plus a separator column is 64 px and the first separator straddles both screen edges.
  BG2 is unused. Wokas: OAM entries 0-3, 32x32, all 8 frames of each player in VRAM at once ($0000-$1FFF), so a
  pose change is a tile number, no upload.
- **Board.** One u16 per row: columns in bits 3-9, the other bits are walls; 2 hidden rows above the well and
  4 full rows below it, so a piece's 4-row box never reads outside its well. A collision test is four ANDs.
  The BG1 map in RAM is the only record of block colors: rows that move copy their cells.
- **Frame budget.** Compiled C was far too slow for the board: a piece move cost ~160 scanlines, collapsing a
  well ~250. Collision, landing row, piece drawing, locking, collapsing, junk, gray-out and the next-piece box
  are in `src/well.asm`; rules, input and screens stay in C. On top of that, work is spread over frames: at most
  3 locks, spawns or piece redraws per frame (the rest waits a frame), one heavy board operation (collapse, junk,
  gray-out) per frame, a sound starts only on a quiet frame, and the result text waits a frame after the match
  ends. Measured: 4 idle players cost 76 scanlines; busiest frames 188 (all four clear lines on the same frame)
  and 183 (random 4-player battle with junk); `tests/smoke.py` fails above 200 or on any lag frame during play.
- **Presses.** The game finds new presses itself by comparing each pad with the previous frame it saw, not with
  PVSnesLib's `padsDown`, which is computed per VBlank: a frame that runs late would lose a press.
- **Compiler bug.** 816-tcc evaluates `a || !b` as 0 when `a` is true (any negation right of `||`). Write
  `b == 0` instead. It made Start do nothing on the title until found.
- **VBlank.** RAM copies of both maps, dirty row ranges, at most 24 BG rows and 8 text rows per frame. Building
  a match redraws the whole screen in C and may lag a frame or two behind the countdown.
