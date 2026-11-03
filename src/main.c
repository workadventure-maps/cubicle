/*---------------------------------------------------------------------------------
    Cubicles: a falling-block game for 1 to 4 players around a WorkAdventure arcade cabinet, in versus (a well
    each) or open space (one shared well). P1 on port 1, P2-P4 on a Multitap in port 2. Design: DESIGN.md.
---------------------------------------------------------------------------------*/
#include <snes.h>
#include <string.h>
#include "music/soundbank.h"

extern char blocks_a_til, blocks_a_pal, blocks_b_til, blocks_b_pal, blocks_p_pal, walls_til, walls_pal;
extern char font_til, font_tilend, font_pal;
extern char p1_til, p2_til, p3_til, p4_til;
extern char p1_pal, p2_pal, p3_pal, p4_pal;
extern char sfx_lock, sfx_lock_end, sfx_clear, sfx_clear_end, sfx_junk, sfx_junk_end;
extern char sfx_join, sfx_join_end, sfx_topout, sfx_topout_end;
extern char SOUNDBANK__;
extern u8 spc_fread, spc_fwrite; // PVSnesLib's queue of driver commands
void spcBootMusic(void); // src/music.asm, for our altered SNESMOD driver
void spcSetTempo(u8 bpm);
void spcQueueOrder(u8 order);

// Note: initialized globals live in ROM with this compiler, so only tables are initialized.
// Compiler bug: `a || !b` gives 0 when a is true. Never put a negation right of ||: write `b == 0`.

// ---- Layout -------------------------------------------------------------------------------
// The screen is 32x28 tiles. BG1 and BG3 scroll 4 px left, so a well (7 cells) plus one separator column is
// 64 px: four wells fill the 256 px screen, the first separator column straddling both screen edges.
// Per well: player label on row 1, Woka and next piece on rows 2-4, partition caps on row 5, the well on
// rows 6-25, floor on row 26, READY marks on row 27.
// Open space: one well, 7 columns per player, each player's Woka and next piece above their 7 columns. The team
// score is on row 1 and the player labels on row 2. An even-width well needs no BG scroll to be centered.
#define WELL_W 7
#define WELL_TOP 6
#define FLOOR_ROW 26
#define LABEL_ROW 1
#define NEXT_ROW 3
#define CAP_ROW 5
#define READY_ROW 27
#define MSG_ROW 14
#define NOTE_ROW 16 // mode of the next game
#define HUD_ROW 1   // open space: team score
#define TEAM_LABEL_ROW 2
#define WOKA_Y 15

// Board: a 32-bit mask per row (two u16, low word first), columns 0..w-1 in bits 3..w+2, walls in the other
// bits (src/well.asm explains how pieces are tested). Rows 0-1 are hidden above the well, 2-21 visible, 22-25
// floor (all bits set), so a piece box (4 rows) never reads past it. Versus: 4 boards of 7 columns. Open space:
// board 0 only, 7 columns per player.
#define HID 2
#define BOTTOM 21
#define ROWS 26
#define CELLS 0x03F8 // versus: the cells of a row's low word
const u8 vs_base[4] = {0, ROWS, 2 * ROWS, 3 * ROWS};
// open space, by player count: walls of an empty row, first screen column of the well, BG scroll
const u16 open_wall_lo[5] = {0, 0xFC07, 0x0007, 0x0007, 0x0007};
const u16 open_wall_hi[5] = {0, 0xFFFF, 0xFFFE, 0xFF00, 0x8000};
const u8 open_col0[5] = {0, 13, 9, 6, 2};
const u8 open_scroll[5] = {4, 4, 0, 4, 0};

// VRAM (words): Woka frames $0000-$1FFF (P1..P4 at $0000, $0800, $1000, $1800), BG1 tiles $2000 (blocks_a, blocks_b, walls: 16 tiles each),
// BG3 font $6000, BG3 text map $7000, BG1 map $7400.
// CGRAM: text palettes 0..4 (colors 0..19), BG palettes 2..5, sprite palettes 8..11.
#define VRAM_BG_TILES 0x2000
#define VRAM_FONT 0x6000
#define VRAM_TEXT_MAP 0x7000
#define VRAM_BG_MAP 0x7400
#define TEXT_ATTR 0x2000 // high priority (above everything with BG3_MODE1_PRIORITY_HIGH), palette 0

// BG1 map entries (tile | palette), tile order from tools/mkgfx.py
#define PAL_A 0x0800
#define PAL_B 0x0C00
#define PAL_W 0x1000
#define PAL_P 0x1400 // blocks_a's tiles in the players' colors
#define T_EMPTY (PAL_A | 9)
#define T_FLASH (PAL_A | 10)
#define T_JUNK (PAL_B | 20)
#define T_SEP (PAL_W | 33)
#define T_SEP_FIRST (PAL_W | 34)
#define T_SEP_LAST (PAL_W | 35)
#define T_CAP (PAL_W | 36)
#define T_CAP_FIRST (PAL_W | 37)
#define T_CAP_LAST (PAL_W | 38)
#define T_FLOOR (PAL_W | 39)
#define T_FLOOR_FIRST (PAL_W | 40)
#define T_FLOOR_LAST (PAL_W | 41)
// by color: the 7 piece types, then (open space) players 1-4
#define COLOR_P1 7
const u16 piece_tile[11] = {PAL_A | 1, PAL_A | 2, PAL_A | 3, PAL_A | 4, PAL_B | 17, PAL_B | 18, PAL_B | 19,
                            PAL_P | 1, PAL_P | 2, PAL_P | 3, PAL_P | 4};
const u16 ghost_tile[11] = {PAL_A | 5, PAL_A | 6, PAL_A | 7, PAL_A | 8, PAL_B | 21, PAL_B | 22, PAL_B | 23,
                            PAL_P | 5, PAL_P | 6, PAL_P | 7, PAL_P | 8};

// ---- Pieces ---------------------------------------------------------------------------------
// I O T S Z J L in their spawn orientation: 4 rows of the bounding box, bit 0 = leftmost column.
// The other orientations turn clockwise inside the box (init_pieces).
#define PIECE_I 0
#define PIECE_O 1
const u8 piece_rows[28] = {0, 15, 0, 0, 3, 3, 0, 0, 2, 7, 0, 0, 6, 3, 0, 0,
                           3, 6, 0, 0, 1, 7, 0, 0, 4, 7, 0, 0};
const u8 piece_box[7] = {4, 2, 3, 3, 3, 3, 3};
// rotation: try in place, then 1 column left, 1 right, 2 left, 2 right (u8 arithmetic: 255 is -1). The 2-column
// tries let an upright I piece against a wall turn flat.
const u8 kick[5] = {0, 255, 1, 254, 2};

// ---- Rules ----------------------------------------------------------------------------------
#define SPAWN_COL 5   // box column + 3 (the mask shift): box at column 2
#define LOCK_DELAY 30 // frames a landed piece can still move
#define LOCK_RESETS 8 // moves that restart the lock delay, per piece
#define DAS_DELAY 12  // frames before a held direction repeats
#define DAS_REPEAT 3
#define SOFT_DROP 2       // frames per row while Down is held
#define CLEAR_FRAMES 20   // full rows flash before they collapse
#define SPAWN_FRAMES 6    // pause between a lock and the next piece
#define SEQ_LEN 252       // 36 shuffled sets of the 7 pieces; versus: all players read it from the start
#define MAX_JUNK 20
#define BREAK_FRAMES 600 // open space: a player who touched nothing for 10 s gets no new piece (coffee break)
// fall speed in frames per row: versus every 30 seconds, solo every 10 lines
const u8 vs_speed[8] = {36, 24, 16, 10, 6, 4, 2, 1};
const u8 solo_speed[16] = {48, 40, 33, 27, 22, 18, 15, 12, 10, 8, 7, 6, 5, 4, 3, 2};
const u8 line_points[5] = {0, 1, 4, 9, 16}; // x 10 x (level + 1)
const u8 bit_count[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

#define MODE_VERSUS 0
#define MODE_OPEN 1

// game states
#define ST_TITLE 0
#define ST_COUNTDOWN 1
#define ST_PLAY 2
#define ST_RESULT 3
#define ST_ENDING 4 // the match just ended: the result text waits for the next frame (text is slow)
// well phases; a well is alive from W_FALL up
#define W_OFF 0    // not in the match, or out of it
#define W_DYING 1  // topped out, waiting to gray out
#define W_FALL 2   // a piece is falling
#define W_SETTLE 3 // after a lock: rows collapse, junk rises, next piece
#define W_LOCK 4   // landed: locks on the first frame with room
// Locking, spawning or redrawing a piece costs ~10-20 scanlines: at most MAX_WORK per frame, the rest waits.
// In open space a lock costs about twice as much (lock_work 2), and the wide collapse takes a frame of its own.
#define MAX_WORK 3

// sound effects, by rising priority (one starts per frame)
#define SFX_LOCK 0
#define SFX_CLEAR 1
#define SFX_JUNK 2
#define SFX_JOIN 3
#define SFX_TOPOUT 4
#define SFX_NONE 255

// Woka frames (tools/mkgfx.py): 8 per player, all in VRAM, player p's frame f at tile p * 128 + 4 frames
// per 64-tile band. In the well 2/3 size; on the title full size, walking (F_TITLE + walk_cycle).
#define F_STAND 0
#define F_LEFT 1
#define F_RIGHT 2
#define F_BACK 3
#define F_TITLE 4
const u8 walk_cycle[4] = {0, 1, 2, 1};
const u8 title_x[4] = {40, 88, 136, 184};
#define TITLE_Y 120
#define TITLE_LABEL_ROW 21
#define TITLE_PROMPT_ROW 23
#define TITLE_BEST_ROW 25
#define TITLE_TEAM_ROW 26
#define TITLE_MODE_ROW 12
#define LOGO_ROW 5

// text palettes: 0 white, then P1..P4 in the cabinet's badge colors (red, blue, green, yellow), BGR555
const u16 hud_colors[5] = {0x7FFF, 0x211F, 0x7E08, 0x2388, 0x139F};

// CUBICLES in 3x5 blocks, one row per byte (bit 2 = left column)
const u8 logo[40] = {7, 4, 4, 4, 7, 5, 5, 5, 5, 7, 6, 5, 6, 5, 6, 7, 2, 2, 2, 7,
                     7, 4, 4, 4, 7, 4, 4, 4, 4, 7, 7, 4, 6, 4, 7, 7, 4, 7, 1, 7};

// ---- State (RAM) ----------------------------------------------------------------------------
u16 bgmap[1024];   // BG1 map
u16 txtmap[1024];  // BG3 text map
u8 bg_lo, bg_hi;   // rows waiting for VRAM (255: none)
u8 txt_lo, txt_hi;
u16 hudpal[20];

u8 shape_rows[112];         // [(type * 4 + rotation) * 4 + row]
u8 blk_x[112], blk_y[112];  // the 4 blocks of each shape, same index
u16 blk_off[112];           // their bgmap offsets from the box's top-left cell
u16 row_map[ROWS];          // bgmap index of board row y, column 0 of a well at screen column 0

// +2: a piece's 16-bit window on a row's last byte reads one byte past it
u16 rows[8 * ROWS + 2];
u16 ov[8 * ROWS + 2];  // falling pieces of a shared well (solid for teammates), same layout as rows
u16 dmark[8 * ROWS + 2]; // collapse: rows holding a drawn piece or ghost cell (src/well.asm)
u16 newrow[ROWS];      // collapse: where each board row went (0xFFFF: cleared)
u16 solid;             // 0xFFFF: collisions include ov (open space); 0: only the board
u8 rbase[4];           // each player's board: first row index in rows
u8 wcells[4];          // its width in cells
u16 wall_lo[4], wall_hi[4]; // its empty row
u8 phase[4];
u8 ptype[4], prot[4], pcol[4], pyy[4], gy[4]; // pcol: box column + 3; pyy, gy: box row of piece and ghost
u8 pcolor[4];        // its color (piece_tile index): its type, or in open space its player
u8 pk[4];            // byte of the board row where each piece's masks start (pcol / 8)
u16 pmask[20];       // each falling piece's 4 row masks, shifted by pcol % 8 (p * 4), then candidate masks (TM)
#define TM 16
u16 drawn[32];       // bgmap cells showing each well's piece (0-3) and ghost (4-7); 0xFFFF: none
u16 drawn_t[32];     // the tile drawn there
u8 col0[4];          // screen column of each player's well's first cell
u8 zcol[4];          // screen column of each player's own 7 columns (Woka, next piece, label)
u8 spcol[4];         // pcol of a new piece (open space: where the player's last piece locked)
u8 grav[4], lockt[4], resets[4], das[4], wait[4];
u8 airborne[4];      // the last test found room below the piece, and it hasn't moved down since
u8 pending[4];       // junk rows on their way
u8 cleared[4];       // rows cleared by the last lock, still to collapse
u8 seqi[4];
u8 seq[SEQ_LEN];
u16 keys_now[4], keys_down[4], keys_before[4]; // presses seen by the game, so a late frame can't lose one
u16 keys_held[4];    // presses kept for the next frame (open space: a frame where the well collapsed)
u8 cheer[4];         // frames left of the Woka's hop after a clear
u8 high[4];          // highest filled board row (the Woka panics when it gets close to the top)

u8 inmatch[4], ready[4], wins[4];
u8 touched[4]; // pressed something since the match started: who didn't sits out the next one (left the cabinet)
u16 idle[4];   // frames since the pad was last touched
u8 brk[4];     // open space: on a coffee break (no new piece until the pad is touched)
u8 mode;       // MODE_VERSUS or MODE_OPEN, for the next game (title: Left/Right; any time: Select)
u8 coop;       // this game is open space with 2 or more players
u8 scroll_x, scroll_set; // BG scroll, applied in VBlank
u16 note_timer;
u8 nplay, npads;
u8 state;
u16 state_timer;
u16 match_frames;
u8 vs_level, fall_speed;
u16 score_hi, score_lo, best_hi, best_lo, lines; // scores are hi * 10000 + lo
u16 team_hi, team_lo; // best open-space score
u8 level, level_lines; // level_lines: lines cleared since the level began
u8 hud_step;           // score numbers still to redraw (one per frame)
u8 heavy;  // one heavy board operation (collapse, junk, gray-out) per frame
u8 work;   // piece locks and redraws this frame (at most MAX_WORK)
u8 lock_work; // what a lock counts in work: 1, 2 in open space
u8 dirty[4]; // the piece needs a redraw (1), and its ghost moved too (2)
u8 next_draw; // first well to consider for redraws, rotating for fairness
u8 seeded;
u8 sprite_turn; // the Woka draw_sprites updates next
char buf[12];

// ---- Sound ----------------------------------------------------------------------------------
brrsamples sfx_table[5];
u8 sfx_next;

void sfx(u8 id)
{
    if (sfx_next == SFX_NONE || id > sfx_next)
        sfx_next = id;
}

// ---- Music ----------------------------------------------------------------------------------
// Solo games only: in versus, the players talk to each other. "In the Hall of the Mountain King"
// (music/mountain_king.it): pattern 0 is the opening bar (not played), patterns 1-20 form 7 groups of rising
// tension, 21-22 end a lost game. A group plays to its end, then the stack height picks the next one (up or
// down), or the ending after a game over; the level sets the tempo. Our driver (snesmod/sm_spc.asm) takes both
// from the game.
#define MUS_OFF 0
#define MUS_PLAY 1
#define MUS_END 2      // game over: the current group, then the ending, then silence
#define ENDING 21
#define ORDER_STOP 0xFE // stop when the current pattern ends; the driver then reports this position
const u8 group_first[7] = {1, 5, 7, 11, 13, 17, 19};
const u8 group_last[7] = {4, 6, 10, 12, 16, 18, 20};
// stack height in rows (0-20) -> group
const u8 height_group[21] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 6};
u8 music, music_pos, music_next, music_tempo;

u8 level_tempo(void) // 138 BPM on level 1 to 238 on level 9 and up
{
    u8 l = level < 8 ? level : 8;
    return 138 + l * 12 + (l >> 1);
}

void music_start(void)
{
    music = MUS_PLAY;
    music_tempo = level_tempo();
    spcPlay(1);
    spcSetTempo(music_tempo); // spcPlay resets the module's tempo
    music_pos = 255;          // queue the next order once the driver reports this one
}

void music_end(void)
{
    music = MUS_END;
    music_next = 255; // queue again: the ending replaces the next group
}

void music_stop(void)
{
    if (music != MUS_OFF)
        spcStop();
    music = MUS_OFF;
}

void update_music(void)
{
    u8 pos, g, next, p, top;
    if (music == MUS_OFF)
        return;
    if (music == MUS_PLAY && level_tempo() != music_tempo)
    {
        music_tempo = level_tempo();
        spcSetTempo(music_tempo);
    }
    pos = spcGetMusicPosition();
    if (pos != music_pos)
    {
        music_pos = pos;
        music_next = 255;
    }
    if (music == MUS_END && pos == ORDER_STOP) // the ending is over
    {
        music = MUS_OFF;
        return;
    }
    if (music == MUS_END && pos >= ENDING)
        next = pos == ENDING ? ENDING + 1 : ORDER_STOP;
    else
    {
        g = 0;
        while (g < 6 && group_last[g] < pos)
            g++;
        if (pos >= group_first[g] && pos < group_last[g])
            next = pos + 1;
        else if (music == MUS_END)
            next = ENDING;
        else // last pattern of a group: the next group follows the stack, until the very end of this pattern
        {
            top = BOTTOM + 1;
            for (p = 0; p < 4; p++)
                if (inmatch[p] && high[p] < top)
                    top = high[p];
            next = group_first[height_group[BOTTOM + 1 - top]];
        }
    }
    if (next != music_next)
    {
        spcQueueOrder(next);
        music_next = next;
    }
}

void init_sound(void)
{
    spcBootMusic();
    spcAllocateSoundRegion(12);
    spcSetBank(&SOUNDBANK__);
    spcLoad(MOD_MOUNTAIN_KING);
    music = MUS_OFF;
    spcSetSoundDataEntry(15, 8, 2, &sfx_lock_end - &sfx_lock, &sfx_lock, &sfx_table[0]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_clear_end - &sfx_clear, &sfx_clear, &sfx_table[1]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_junk_end - &sfx_junk, &sfx_junk, &sfx_table[2]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_join_end - &sfx_join, &sfx_join, &sfx_table[3]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_topout_end - &sfx_topout, &sfx_topout, &sfx_table[4]);
    spcSetSoundTableEntry(&sfx_table[0]);
    sfx_next = SFX_NONE;
}

// Starting a sound costs about 40 scanlines, so it waits for a frame with little board work. spcPlaySound
// first waits until the driver took every queued command, and starting the music keeps it busy 8 to 19
// frames (it clears and waits for the echo buffer): the game would freeze, so the sound waits instead.
void update_sound(void)
{
    if (sfx_next != SFX_NONE && !heavy && work < 2 && spc_fread == spc_fwrite)
    {
        spcPlaySound(sfx_next);
        sfx_next = SFX_NONE;
    }
    update_music();
    spcProcess();
}

// ---- Text -----------------------------------------------------------------------------------
void mark_txt(u8 y)
{
    if (y < txt_lo)
        txt_lo = y;
    if (y > txt_hi || txt_hi == 255)
        txt_hi = y;
}

void mark_bg(u8 lo, u8 hi)
{
    if (lo < bg_lo)
        bg_lo = lo;
    if (hi > bg_hi || bg_hi == 255)
        bg_hi = hi;
}

void text(u8 x, u8 y, u8 pal, char *s)
{
    u16 i = (y << 5) + x;
    u16 attr = TEXT_ATTR | (pal << 10);
    while (*s)
        txtmap[i++] = (*s++ - 32) | attr;
    mark_txt(y);
}

void clear_row(u8 y)
{
    u16 i = y << 5;
    u8 n;
    for (n = 0; n < 32; n++)
        txtmap[i + n] = 0;
    mark_txt(y);
}

void center(u8 y, u8 pal, char *s)
{
    u8 n = 0;
    while (s[n])
        n++;
    clear_row(y);
    text(16 - (n >> 1), y, pal, s);
}

// digits by subtraction: this compiler's divisions are slow (a 4-number score panel cost ~100 scanlines)
const u16 pow10[5] = {1, 10, 100, 1000, 10000};

void number(u8 x, u8 y, u16 v, u8 digits)
{
    u8 i = 0;
    u16 p;
    char d;
    while (digits)
    {
        p = pow10[--digits];
        d = '0';
        while (v >= p)
        {
            v -= p;
            d++;
        }
        buf[i++] = d;
    }
    buf[i] = 0;
    text(x, y, 0, buf);
}

void player_label(u8 x, u8 y, u8 p)
{
    buf[0] = 'P';
    buf[1] = '1' + p;
    buf[2] = 0;
    text(x, y, 1 + p, buf);
}

void draw_ready(void)
{
    u8 p;
    for (p = 0; p < 4; p++)
        text((p << 3) + 2, READY_ROW, 1 + p, ready[p] ? "READY" : "     ");
}

// solo: score, lines and level right of the well, best score left of it. Open space: one line on top.
// Number k: 0 level, 1 lines, 2 and 3 the score's low and high part.
const u8 hud_digits[4] = {2, 4, 4, 3};
const u8 solo_hud_x[4] = {22, 22, 25, 22};
const u8 solo_hud_y[4] = {17, 13, 9, 9};
const u8 open_hud_x[4] = {29, 22, 11, 8};

void draw_hud(u8 k)
{
    u16 v = score_hi;
    if (k == 0)
        v = level + 1;
    else if (k == 1)
        v = lines;
    else if (k == 2)
        v = score_lo;
    if (coop)
        number(open_hud_x[k], HUD_ROW, v, hud_digits[k]);
    else
        number(solo_hud_x[k], solo_hud_y[k], v, hud_digits[k]);
}

void draw_score(void)
{
    u8 k;
    for (k = 0; k < 4; k++)
        draw_hud(k);
}

void draw_best(u8 x, u8 y)
{
    number(x, y, best_hi, 3);
    number(x + 3, y, best_lo, 4);
}

void draw_mode_note(void)
{
    center(NOTE_ROW, 0, mode ? "NEXT: OPEN SPACE" : "NEXT: VERSUS");
    note_timer = 120;
}

// ---- Pieces ---------------------------------------------------------------------------------
void init_pieces(void)
{
    u8 t, r, i, x, n, s, k;
    for (t = 0; t < 7; t++)
    {
        n = piece_box[t];
        s = t << 4;
        for (i = 0; i < 4; i++)
            shape_rows[s + i] = piece_rows[(t << 2) + i];
        for (r = 1; r < 4; r++)
        {
            for (i = 0; i < 4; i++)
                shape_rows[s + (r << 2) + i] = 0;
            // clockwise: (x, y) -> (n - 1 - y, x)
            for (i = 0; i < 4; i++)
                for (x = 0; x < 4; x++)
                    if (shape_rows[s + ((r - 1) << 2) + i] & (1 << x))
                        shape_rows[s + (r << 2) + x] |= 1 << (n - 1 - i);
        }
    }
    for (s = 0; s < 112; s += 4)
    {
        k = s;
        for (i = 0; i < 4; i++)
            for (x = 0; x < 4; x++)
                if (shape_rows[s + i] & (1 << x))
                {
                    blk_x[k] = x;
                    blk_y[k] = i;
                    blk_off[k] = (i << 5) + x;
                    k++;
                }
    }
}

// in src/well.asm. Row indices (at) count from the start of rows: rbase[p] + board row.
u8 fits(u8 p, u8 at);        // piece p's masks fit with its box top on row at?
u8 fits_board(u8 p, u8 at);  // the same, ignoring teammates' falling pieces
u8 find_drop(u8 p, u8 at);   // lowest row they fit at, falling from at
u8 find_land(u8 p, u8 at);   // the same, ignoring teammates' falling pieces (where the ghost goes)
void draw_piece_cells(u8 p); // erase p's piece and ghost, draw them again
void erase_piece_cells(u8 p);
u8 place(u8 p, u8 s, u8 col); // shape s at column col fits? then it becomes p's piece masks
void ov_xor(u8 p);           // p's piece into the falling-piece overlay, or out of it
u8 read_pads(u8 n);          // pads 0..n-1 into keys_now / keys_down; a bit per pad with Start, Select, Left or Right
u8 find_top(u8 p);           // highest visible row of p's board with a block in it (BOTTOM + 1: none)
u8 lock_rows(u8 p);          // the piece joins the board: 255 if locked above the well, else its full rows
void collapse(u8 p);         // full rows go away; teammates' falling pieces move down with their rows
void add_junk_rows(u8 p, u8 n, u8 gap);
void gray_well_cells(u8 p);
void draw_next_cells(u8 p, u8 t, u8 c); // c: its color
u16 dw_p, dw_slot, dw_s, dw_tilex, dw_col, dw_y0, dw_tile, dw_dst, dw_base, dw_k, dw_n; // their scratch
u16 dw_w, dw_wlo, dw_whi, dw_ghost;

void solid_on(void)
{
    solid = coop ? 0xFFFF : 0;
}

void draw_piece(u8 p)
{
    if (dirty[p] & 2) // the ghost shows where the piece lands on the stack, not on a teammate's falling piece
        gy[p] = find_land(p, rbase[p] + pyy[p]) - rbase[p];
    draw_piece_cells(p);
    mark_bg(WELL_TOP, FLOOR_ROW - 1);
    dirty[p] = 0;
}

// redraw moved pieces, within the frame's work budget
void draw_dirty(void)
{
    u8 k, p;
    for (k = 0; k < 4; k++)
    {
        p = (next_draw + k) & 3;
        if (dirty[p] && phase[p] == W_FALL && work < MAX_WORK)
        {
            draw_piece(p);
            work++;
        }
    }
    next_draw++;
}

u8 try_rotate(u8 p) // clockwise
{
    u8 r = (prot[p] + 1) & 3, s = (ptype[p] << 4) | (r << 2), k, c;
    for (k = 0; k < 5; k++)
    {
        c = pcol[p] + kick[k];
        if (place(p, s, c))
        {
            prot[p] = r;
            pcol[p] = c;
            return 1;
        }
    }
    return 0;
}

u8 try_shift(u8 p, u8 right)
{
    u8 c = right ? pcol[p] + 1 : pcol[p] - 1;
    if (!place(p, (ptype[p] << 4) | (prot[p] << 2), c))
        return 0;
    pcol[p] = c;
    return 1;
}

// ---- Wells ----------------------------------------------------------------------------------
u8 alive(u8 p)
{
    return phase[p] >= W_FALL;
}

u8 count_alive(void)
{
    u8 p, n = 0;
    for (p = 0; p < 4; p++)
        if (alive(p))
            n++;
    return n;
}

// a topped-out player's well grays out on a later frame (heavy). In open space the whole team is out.
void top_out(u8 p)
{
    u8 q;
    if (coop)
        for (q = 0; q < 4; q++)
            if (inmatch[q])
                phase[q] = W_OFF;
    phase[p] = W_DYING;
    sfx(SFX_TOPOUT);
}

void gray_well(u8 p)
{
    u8 q;
    if (coop)
        for (q = 0; q < 4; q++)
            erase_piece_cells(q);
    gray_well_cells(p);
    mark_bg(WELL_TOP, FLOOR_ROW - 1);
}

// pending junk rows push the stack up, each with a gap in the same column (heavy)
void add_junk(u8 p)
{
    u16 i = rbase[p] << 1;
    add_junk_rows(p, pending[p], rand() % WELL_W);
    pending[p] = 0;
    mark_bg(WELL_TOP, FLOOR_ROW - 1);
    sfx(SFX_JUNK);
    if ((rows[i] | rows[i + 2]) & CELLS)
        top_out(p); // pushed out of the top
}

// open space: a player's pieces are all in their color
u8 color(u8 p, u8 t)
{
    return coop ? COLOR_P1 + p : t;
}

void draw_next(u8 p)
{
    u8 t = seq[seqi[p]];
    draw_next_cells(p, t, color(p, t));
    mark_bg(NEXT_ROW, NEXT_ROW + 1);
}

// the next piece, above the player's own columns (open space: above where their last piece locked). In open
// space a teammate's falling piece in the way only delays it: the spawn is tried again next frame.
void spawn(u8 p)
{
    u8 t = seq[seqi[p]], c = spcol[p], edge, ok;
    edge = wcells[p] + 3 - piece_box[t]; // in spawn orientation a piece fills its box's first piece_box columns
    if (c > edge)
        c = edge; // the last piece was turned against the right wall
    if (c < 3)
        c = 3;   // or the left one
    ptype[p] = t;
    pcolor[p] = color(p, t);
    prot[p] = 0;
    pcol[p] = c;
    pyy[p] = t == PIECE_I ? HID - 1 : HID;
    grav[p] = 0;
    lockt[p] = 0;
    resets[p] = 0;
    airborne[p] = 0;
    ok = place(p, t << 4, c);
    if (coop && ok == 0)
    {
        solid = 0;
        ok = place(p, t << 4, c);
        solid_on();
        if (ok)
            return;
    }
    if (++seqi[p] == SEQ_LEN)
        seqi[p] = 0;
    draw_next(p);
    if (!ok)
    {
        top_out(p);
        return;
    }
    phase[p] = W_FALL;
    dirty[p] = 3;
    if (coop)
        ov_xor(p);
}

void add_score(u8 n)
{
    score_lo += line_points[n] * 10 * (level + 1);
    if (score_lo >= 10000)
    {
        score_lo -= 10000;
        if (score_hi < 999)
            score_hi++;
    }
    lines += n;
    level_lines += n;
    if (level_lines >= 10)
    {
        level_lines -= 10;
        if (level < 15)
            level++;
    }
    hud_step = 4; // redrawn one number per frame
}

// p's piece becomes blocks. In open space it leaves the falling-piece overlay.
void lock_piece(u8 p)
{
    u8 full, n, q;
    if (dirty[p])
        draw_piece(p); // the piece's final cells, which become blocks
    full = lock_rows(p); // (in open space, teammates in these columns redraw: their ghosts may land on it)
    sfx(SFX_LOCK);
    if (full == 255)
    {
        top_out(p); // locked above the well
        return;
    }
    n = bit_count[full];
    if (n)
        mark_bg(WELL_TOP, FLOOR_ROW - 1); // the full rows flash
    phase[p] = W_SETTLE;
    if (coop)
        spcol[p] = pcol[p]; // the next piece comes in above this one
    cleared[p] = n;
    wait[p] = n ? CLEAR_FRAMES : SPAWN_FRAMES;
    if (n)
    {
        sfx(SFX_CLEAR);
        cheer[p] = 40;
        if (nplay == 1 || coop)
            add_score(n);
        else
            for (q = 0; q < 4; q++)
                if (q != p && alive(q))
                {
                    pending[q] += n - 1;
                    if (pending[q] > MAX_JUNK)
                        pending[q] = MAX_JUNK;
                }
    }
}

void land(u8 p)
{
    phase[p] = W_LOCK;
    if (work + lock_work <= MAX_WORK)
    {
        work += lock_work;
        lock_piece(p);
    }
}

// the piece rests on the stack, not just on a teammate's falling piece (which only holds it up)
u8 on_stack(u8 p)
{
    return fits_board(p, rbase[p] + pyy[p] + 1) == 0;
}

void update_piece(u8 p)
{
    u16 keys = keys_now[p], down = keys_down[p];
    u8 moved = 0, step = 0, speed, rest = 0;

    speed = fall_speed;
    if ((keys & KEY_DOWN) && speed > SOFT_DROP)
        speed = SOFT_DROP;
    if ((keys | down) == 0 && airborne[p] && grav[p] + 1 < speed)
    {
        grav[p]++; // most frames: no input, still room below, no row to fall yet
        return;
    }
    if (coop)
        ov_xor(p); // out of the overlay while it moves: a piece doesn't collide with itself
    if (down & (KEY_UP | KEY_A | KEY_X))
        moved = try_rotate(p);
    if (keys & (KEY_LEFT | KEY_RIGHT))
    {
        if (down & (KEY_LEFT | KEY_RIGHT))
        {
            das[p] = DAS_DELAY;
            step = 1;
        }
        else if (--das[p] == 0)
        {
            das[p] = DAS_REPEAT;
            step = 1;
        }
        if (step && try_shift(p, (keys & KEY_LEFT) ? 0 : 1))
            moved = 1;
    }
    if (moved)
    {
        dirty[p] = 3;
        if (lockt[p] && resets[p] < LOCK_RESETS)
        {
            lockt[p] = 0;
            resets[p]++;
        }
    }
    if (down & (KEY_B | KEY_Y)) // hard drop: locks on the stack, or stops on a teammate's piece
    {
        pyy[p] = find_drop(p, rbase[p] + pyy[p]) - rbase[p];
        rest = coop ? on_stack(p) : 1;
        if (rest)
        {
            gy[p] = pyy[p]; // landed: the ghost is there too, no need to look for it again
            dirty[p] |= 1;
        }
        else
            dirty[p] = 3;
    }
    else
    {
        airborne[p] = 0;
        if (fits(p, rbase[p] + pyy[p] + 1))
        {
            lockt[p] = 0;
            if (++grav[p] >= speed)
            {
                grav[p] = 0;
                pyy[p]++;
                dirty[p] |= 1;
            }
            else
                airborne[p] = 1;
        }
        else if (coop == 0 || on_stack(p))
        {
            if (++lockt[p] >= LOCK_DELAY)
                rest = 1;
        }
        else
            lockt[p] = 0; // held up by a teammate's piece: it neither falls nor locks
    }
    if (coop)
        ov_xor(p);
    if (rest)
        land(p);
}

// after a lock: wait, then collapse cleared rows, then raise junk, then the next piece, one heavy step per frame
void settle(u8 p)
{
    if (wait[p])
    {
        wait[p]--;
        return;
    }
    if (heavy)
        return;
    if (cleared[p])
    {
        if (coop && work)
            return; // the shared well's collapse waits for a frame with nothing else to do
        collapse(p);
        mark_bg(WELL_TOP, FLOOR_ROW - 1);
        cleared[p] = 0;
        heavy = 1;
        if (coop)
            work = MAX_WORK;
        return;
    }
    if (pending[p])
    {
        add_junk(p);
        heavy = 1;
        return;
    }
    if (coop && idle[p] >= BREAK_FRAMES)
    {
        brk[p] = 1; // coffee break: no next piece until the pad is touched
        return;
    }
    if (work < MAX_WORK)
    {
        work++;
        high[p] = find_top(p);
        spawn(p);
    }
}

void update_well(u8 p)
{
    if (phase[p] == W_FALL)
    {
        if (coop && heavy)
            keys_held[p] = keys_down[p]; // the shared well collapsed this frame: this piece waits for the next
        else
            update_piece(p);
    }
    else if (phase[p] == W_SETTLE)
        settle(p);
    else if (phase[p] == W_LOCK && work + lock_work <= MAX_WORK)
    {
        work += lock_work;
        lock_piece(p);
    }
    else if (phase[p] == W_DYING && !heavy)
    {
        gray_well(p);
        phase[p] = W_OFF;
        heavy = 1;
    }
}

// ---- Screens --------------------------------------------------------------------------------
// Rebuilding a screen spans several frames: mark it dirty first, so the tests never see it half done.
void clear_screen(void)
{
    bg_lo = txt_lo = 0;
    bg_hi = txt_hi = 27;
    memset(bgmap, 0, 2048);
    memset(txtmap, 0, 2048);
}

void draw_logo(void)
{
    u8 k, r, c, bits;
    u16 i;
    for (k = 0; k < 8; k++)
        for (r = 0; r < 5; r++)
        {
            bits = logo[k * 5 + r];
            i = ((LOGO_ROW + r) << 5) + 1 + (k << 2);
            for (c = 0; c < 3; c++)
                if (bits & (4 >> c))
                    bgmap[i + c] = piece_tile[k % 7];
        }
}

void draw_title_labels(void)
{
    u8 p;
    for (p = 0; p < 4; p++)
    {
        if (ready[p])
            text((title_x[p] >> 3) + 2, TITLE_LABEL_ROW, 1 + p, "OK");
        else
            player_label((title_x[p] >> 3) + 2, TITLE_LABEL_ROW, p);
    }
}

void draw_title_mode(void)
{
    center(TITLE_MODE_ROW, 0, mode ? "< OPEN SPACE >" : "<  VERSUS  >");
}

void to_title(void)
{
    u8 p;
    for (p = 0; p < 4; p++)
    {
        inmatch[p] = 0;
        ready[p] = 0;
        touched[p] = 0;
        brk[p] = 0;
        phase[p] = W_OFF;
    }
    state = ST_TITLE;
    state_timer = 0;
    coop = 0;
    scroll_x = 4;
    scroll_set = 1;
    clear_screen();
    draw_logo();
    draw_title_mode();
    draw_title_labels();
    if (best_hi || best_lo)
    {
        text(10, TITLE_BEST_ROW, 0, "BEST");
        draw_best(15, TITLE_BEST_ROW);
    }
    if (team_hi || team_lo)
    {
        text(10, TITLE_TEAM_ROW, 0, "TEAM");
        number(15, TITLE_TEAM_ROW, team_hi, 3);
        number(18, TITLE_TEAM_ROW, team_lo, 4);
    }
}

void deal_sequence(void)
{
    u8 i, j, k, t;
    for (i = 0; i < SEQ_LEN; i += 7)
    {
        for (j = 0; j < 7; j++)
            seq[i + j] = j;
        for (j = 6; j > 0; j--)
        {
            k = rand() % (j + 1);
            t = seq[i + j];
            seq[i + j] = seq[i + k];
            seq[i + k] = t;
        }
    }
}

void draw_partition(u8 c, u16 sep, u16 cap, u16 floor)
{
    u8 y;
    bgmap[(CAP_ROW << 5) + c] = cap;
    for (y = WELL_TOP; y < FLOOR_ROW; y++)
        bgmap[(y << 5) + c] = sep;
    bgmap[(FLOOR_ROW << 5) + c] = floor;
}

// separators, caps and floor of all wells; with four wells the first column is also the last. Open space: the
// partitions between players are gone, only the outer walls stay.
void draw_partitions(u8 first)
{
    u8 k, c;
    if (coop)
    {
        draw_partition(first - 1, T_SEP_FIRST, T_CAP_FIRST, T_FLOOR_FIRST);
        draw_partition(first + wcells[0], T_SEP_LAST, T_CAP_LAST, T_FLOOR_LAST);
        return;
    }
    for (k = 0; k <= nplay; k++)
    {
        c = (first + (k << 3)) & 31;
        if (nplay < 4 && k == 0)
            draw_partition(c, T_SEP_FIRST, T_CAP_FIRST, T_FLOOR_FIRST);
        else if (nplay < 4 && k == nplay)
            draw_partition(c, T_SEP_LAST, T_CAP_LAST, T_FLOOR_LAST);
        else
            draw_partition(c, T_SEP, T_CAP, T_FLOOR);
    }
}

// an empty board and its cells, for player p's well
void init_board(u8 p)
{
    u8 y, j, w = wcells[p];
    u16 i, r = rbase[p] << 1;
    for (y = 0; y < ROWS; y++)
    {
        rows[r] = y > BOTTOM ? 0xFFFF : wall_lo[p];
        rows[r + 1] = y > BOTTOM ? 0xFFFF : wall_hi[p];
        r += 2;
    }
    for (y = WELL_TOP; y < FLOOR_ROW; y++)
    {
        i = (y << 5) + col0[p];
        for (j = 0; j < w; j++)
            bgmap[i + j] = T_EMPTY;
    }
    i = (FLOOR_ROW << 5) + col0[p];
    for (j = 0; j < w; j++)
        bgmap[i + j] = T_FLOOR;
}

void start_match(void)
{
    u8 p, k, j, first;
    nplay = 0;
    for (p = 0; p < 4; p++)
    {
        inmatch[p] = ready[p] || (inmatch[p] && touched[p]);
        ready[p] = 0;
        touched[p] = 0;
        nplay += inmatch[p];
    }
    coop = mode == MODE_OPEN && nplay > 1;
    lock_work = coop ? 2 : 1;
    solid_on();
    clear_screen();
    deal_sequence();
    memset(ov, 0, sizeof(ov));
    if (coop)
    {
        first = open_col0[nplay];
        scroll_x = open_scroll[nplay];
    }
    else
    {
        first = 16 - (nplay << 2);
        scroll_x = 4;
    }
    scroll_set = 1;
    k = 0;
    for (p = 0; p < 4; p++)
    {
        phase[p] = W_OFF;
        pending[p] = 0;
        cheer[p] = 0;
        brk[p] = 0;
        idle[p] = 0;
        for (j = 0; j < 8; j++)
            drawn[(p << 3) + j] = 0xFFFF;
        if (!inmatch[p])
            continue;
        if (coop)
        {
            rbase[p] = 0;
            col0[p] = first;
            wcells[p] = nplay * WELL_W;
            wall_lo[p] = open_wall_lo[nplay];
            wall_hi[p] = open_wall_hi[nplay];
            zcol[p] = first + k * WELL_W;
            spcol[p] = SPAWN_COL + k * WELL_W;
        }
        else
        {
            rbase[p] = vs_base[p];
            col0[p] = first + (k << 3) + 1;
            wcells[p] = WELL_W;
            wall_lo[p] = open_wall_lo[1];
            wall_hi[p] = open_wall_hi[1];
            zcol[p] = col0[p];
            spcol[p] = SPAWN_COL;
        }
        if (coop == 0 || k == 0)
            init_board(p);
        k++;
        cleared[p] = 0;
        dirty[p] = 0;
        phase[p] = W_SETTLE;
        wait[p] = 0;
        // open space: each player starts a quarter of the deck further, so teammates get different pieces
        // ponytail: one deck; a player 63 pieces ahead of a teammate gets the pieces that teammate had
        seqi[p] = coop ? p * (SEQ_LEN / 4) : 0;
        das[p] = 1;
        high[p] = BOTTOM + 1;
        if (coop)
            player_label(zcol[p], TEAM_LABEL_ROW, p);
        else
        {
            player_label(col0[p], LABEL_ROW, p);
            if (nplay > 1)
                number(col0[p] + 5, LABEL_ROW, wins[p], 2);
        }
    }
    draw_partitions(first);
    score_hi = score_lo = lines = 0;
    level = level_lines = hud_step = 0;
    if (nplay == 1)
    {
        text(22, 8, 0, "SCORE");
        text(22, 12, 0, "LINES");
        text(22, 16, 0, "LEVEL");
        text(3, 8, 0, "BEST");
        draw_best(3, 9);
        draw_score();
    }
    else if (coop)
    {
        text(2, HUD_ROW, 0, "SCORE");
        text(16, HUD_ROW, 0, "LINES");
        text(27, HUD_ROW, 0, "LV");
        draw_score();
    }
    match_frames = 0;
    vs_level = 0;
    music_stop(); // a lost game's ending may still play
    state = ST_COUNTDOWN;
    state_timer = 90;
    center(MSG_ROW, 0, "READY");
    if (nplay > 1)
    {
        center(NOTE_ROW, 0, coop ? "OPEN SPACE" : "VERSUS");
        note_timer = 90;
    }
    draw_ready();
}

u8 all_on_break(void)
{
    u8 p;
    for (p = 0; p < 4; p++)
        if (inmatch[p] && brk[p] == 0)
            return 0;
    return 1;
}

void show_result(void)
{
    u8 p, winner = 255;
    for (p = 0; p < 4; p++)
        if (inmatch[p] && alive(p))
            winner = p;
    if (coop)
    {
        if (score_hi > team_hi || (score_hi == team_hi && score_lo > team_lo))
        {
            team_hi = score_hi;
            team_lo = score_lo;
        }
        center(MSG_ROW, 0, all_on_break() ? "EVERYONE ON BREAK" : "GAME OVER");
        for (p = 0; p < 4; p++)
            if (brk[p])
                touched[p] = 0; // still away: sits out the next game
    }
    else if (nplay == 1)
    {
        p = 0;
        while (!inmatch[p])
            p++;
        if (score_hi > best_hi || (score_hi == best_hi && score_lo > best_lo))
        {
            best_hi = score_hi;
            best_lo = score_lo;
        }
        if (winner == 255)
        {
            center(MSG_ROW, 0, "GAME OVER");
            inmatch[p] = 0; // back to the title unless someone pressed Start
            music_end();
        }
        else
        {
            center(MSG_ROW, 0, mode ? "NEW COWORKER!" : "NEW CHALLENGER!");
            music_stop();
        }
    }
    else if (winner == 255)
        center(MSG_ROW, 0, "DRAW");
    else
    {
        if (wins[winner] < 99)
            wins[winner]++;
        number(col0[winner] + 5, LABEL_ROW, wins[winner], 2);
        center(MSG_ROW, 0, "   WINS!");
        player_label(12, MSG_ROW, winner);
        cheer[winner] = 180;
    }
    state = ST_RESULT;
    state_timer = 180;
}

// Start on a pad that is not playing: join the next match (or this one, on the title). Select on any pad, or
// Left/Right on the title, switches the mode of the next game.
void check_joins(void)
{
    u8 p, joined = 0, toggle = 0, act;
    u16 down;
    act = read_pads(npads);
    for (p = 0; act; p++, act >>= 1)
    {
        if ((act & 1) == 0)
            continue;
        down = keys_down[p];
        if ((down & KEY_SELECT) || (state == ST_TITLE && (down & (KEY_LEFT | KEY_RIGHT))))
            toggle = 1;
        if ((down & KEY_START) && !ready[p] && (state == ST_TITLE || inmatch[p] == 0))
        {
            if (!seeded)
            {
                srand(snes_vblank_count);
                seeded = 1;
            }
            ready[p] = 1;
            joined = 1;
            sfx(SFX_JOIN);
        }
    }
    if (joined)
    {
        if (state == ST_TITLE)
            draw_title_labels();
        else
            draw_ready();
    }
    if (toggle)
    {
        mode ^= 1;
        if (state == ST_TITLE)
            draw_title_mode();
        else
            draw_mode_note();
    }
    if (note_timer && --note_timer == 0)
        clear_row(NOTE_ROW);
}

void update_game(void)
{
    u8 p, n;
    heavy = 0;
    work = 0;
    check_joins();
    switch (state)
    {
    case ST_TITLE:
        n = ready[0] + ready[1] + ready[2] + ready[3];
        if (n)
        {
            if (state_timer == 0)
                state_timer = 180;
            if (--state_timer == 0)
            {
                start_match();
                return;
            }
            if (state_timer % 60 == 59)
            {
                buf[0] = '0' + (state_timer + 59) / 60;
                buf[1] = 0;
                center(TITLE_PROMPT_ROW, 0, "GET READY  "); // centered with room for " 3": the count on column 21
                text(21, TITLE_PROMPT_ROW, 0, buf);
            }
        }
        else if ((snes_vblank_count & 31) == 0)
            center(TITLE_PROMPT_ROW, 0, (snes_vblank_count & 32) ? "" : "PRESS START");
        break;

    case ST_COUNTDOWN:
        if (--state_timer == 0)
        {
            state = ST_PLAY;
            state_timer = 30;
            center(MSG_ROW, 0, "GO!");
            if (nplay == 1)
                music_start();
        }
        break;

    case ST_PLAY:
        if (state_timer && --state_timer == 0)
            clear_row(MSG_ROW);
        if (nplay == 1 || coop)
            fall_speed = solo_speed[level];
        else
        {
            if (++match_frames == 1800)
            {
                match_frames = 0;
                if (vs_level < 7)
                    vs_level++;
            }
            fall_speed = vs_speed[vs_level];
        }
        for (p = 0; p < 4; p++)
        {
            if (!inmatch[p])
                continue;
            update_well(p);
            if (cheer[p])
                cheer[p]--;
        }
        draw_dirty();
        if (hud_step && heavy == 0)
            draw_hud(--hud_step);
        n = count_alive();
        if (nplay == 1)
            n = n == 0 || ready[0] + ready[1] + ready[2] + ready[3];
        else if (coop)
            n = n == 0 || all_on_break();
        else
            n = n <= 1;
        if (n)
            state = ST_ENDING;
        break;

    case ST_ENDING:
        show_result();
        break;

    case ST_RESULT:
        if (hud_step)
            draw_hud(--hud_step);
        for (p = 0; p < 4; p++)
        {
            if (phase[p] == W_DYING)
                update_well(p);
            if (cheer[p])
                cheer[p]--;
        }
        if (state_timer && --state_timer)
            break;
        // after a lost solo game, the screen stays until the ending music is over, a key or a join
        n = 0;
        for (p = 0; p < npads; p++)
            if (keys_down[p] || ready[p])
                n = 1;
        if (music != MUS_OFF && n == 0)
            break;
        music_stop();
        n = 0;
        for (p = 0; p < 4; p++)
            n += ready[p] || (inmatch[p] && touched[p]);
        if (n)
            start_match();
        else
            to_title();
        break;
    }
}

// ---- Sprites --------------------------------------------------------------------------------
// The four Wokas are OAM entries 0..3, always large (32x32); hidden ones sit below the screen. Every frame they
// use is in VRAM, so changing frame is just a tile number. PVSnesLib copies oamMemory to OAM in VBlank.
void put_woka(u8 p, u8 x, u8 y, u8 f)
{
    u16 t = (p << 7) + ((f >> 2) << 6) + ((f & 3) << 2);
    u8 i = p << 2;
    oamMemory[i] = x;
    oamMemory[i + 1] = y;
    oamMemory[i + 2] = t;
    oamMemory[i + 3] = OBJ_PRIO(2) | OBJ_PAL(p) | (t >> 8);
}

// One Woka per frame: their poses change every 8 frames at most, and all four cost 25 scanlines of compiled C.
void draw_sprites(void)
{
    u8 p = sprite_turn, x = 0, y = 224, f = F_STAND;
    sprite_turn = (p + 1) & 3;
    if (state == ST_TITLE)
    {
        x = title_x[p];
        y = TITLE_Y;
        f = F_TITLE + (ready[p] ? walk_cycle[((snes_vblank_count >> 3) + p) & 3] : 1);
    }
    else if (inmatch[p])
    {
        x = (zcol[p] << 3) - 4 - scroll_x;
        y = coop ? WOKA_Y + 2 : WOKA_Y; // open space: its label is on the row just above
        if (!alive(p) || brk[p])
            f = F_BACK; // out, or on a coffee break
        else if (cheer[p])
            y -= (cheer[p] & 8) >> 2;
        else if (high[p] < HID + 6) // stack close to the top: looks around nervously
            f = (snes_vblank_count & 16) ? F_LEFT : F_RIGHT;
    }
    put_woka(p, x, y, f);
}

// ---- VBlank transfers -------------------------------------------------------------------------
// One DMA per layer, covering the dirty rows. Everything is worked out in prepare_vblank() during the frame
// and vblank_work() only starts the DMAs. Budget: at most 24 BG rows and 8 text rows per frame.
// tests/smoke.py checks that VRAM matches RAM and that VBlank work ends in time.
u8 *bg_src, *txt_src;
u16 bg_vaddr, bg_len, txt_vaddr, txt_len;

void prepare_vblank(void)
{
    u8 lo, hi;
    bg_len = 0;
    txt_len = 0;
    if (bg_hi != 255)
    {
        lo = bg_lo;
        hi = bg_hi;
        if (hi > lo + 23)
            hi = lo + 23;
        bg_src = (u8 *)&bgmap[lo << 5];
        bg_vaddr = VRAM_BG_MAP + (lo << 5);
        bg_len = (hi - lo + 1) << 6;
        if (hi == bg_hi)
            bg_lo = bg_hi = 255;
        else
            bg_lo = hi + 1;
    }
    if (txt_hi != 255)
    {
        lo = txt_lo;
        hi = txt_hi;
        if (hi > lo + 7)
            hi = lo + 7;
        txt_src = (u8 *)&txtmap[lo << 5];
        txt_vaddr = VRAM_TEXT_MAP + (lo << 5);
        txt_len = (hi - lo + 1) << 6;
        if (hi == txt_hi)
            txt_lo = txt_hi = 255;
        else
            txt_lo = hi + 1;
    }
}

void vblank_work(void)
{
    if (scroll_set)
    {
        bgSetScroll(0, scroll_x, 0);
        bgSetScroll(2, scroll_x, 0);
        scroll_set = 0;
    }
    if (bg_len)
        dmaCopyVram(bg_src, bg_vaddr, bg_len);
    if (txt_len)
        dmaCopyVram(txt_src, txt_vaddr, txt_len);
}

// Scanline where this frame's VBlank work ended (VBlank is lines 225..261). vbl_late counts frames where it
// ran into the visible picture, where VRAM writes are lost. work_end: scanline where the frame's logic was
// done. Read by the tests.
u16 vbl_end;
u16 vbl_late;
u16 work_end;

u16 scanline(void)
{
    u8 lo;
    lo = *(vuint8 *)0x2137; // latch the PPU counters
    lo = *(vuint8 *)0x213F; // reset the read flip-flop
    lo = *(vuint8 *)0x213D;
    return lo | ((*(vuint8 *)0x213D & 1) << 8);
}

void measure_vblank(void)
{
    vbl_end = scanline();
    if (vbl_end < 225 && vbl_late < 65535)
        vbl_late++;
}

// ---- Main -------------------------------------------------------------------------------------
int main(void)
{
    u8 p, i;

    init_sound();

    // text on BG3 (2bpp), wells on BG1, Wokas are 32x32 sprites
    bgInitTileSet(2, &font_til, &font_pal, 0, (&font_tilend - &font_til), 4 * 2, BG_4COLORS, VRAM_FONT);
    bgSetMapPtr(2, VRAM_TEXT_MAP, SC_32x32);
    bgSetGfxPtr(0, VRAM_BG_TILES);
    bgSetMapPtr(0, VRAM_BG_MAP, SC_32x32);
    dmaCopyVram(&blocks_a_til, VRAM_BG_TILES, 512);
    dmaCopyVram(&blocks_b_til, VRAM_BG_TILES + 256, 512);
    dmaCopyVram(&walls_til, VRAM_BG_TILES + 512, 512);
    dmaCopyCGram(&blocks_a_pal, 32, 32);
    dmaCopyCGram(&blocks_b_pal, 48, 32);
    dmaCopyCGram(&walls_pal, 64, 32);
    dmaCopyCGram(&blocks_p_pal, 80, 32);

    // text palettes 0..4: white, then each player's color; color 0 is also the backdrop
    for (i = 0; i < 20; i++)
        hudpal[i] = 0;
    for (p = 0; p < 5; p++)
    {
        hudpal[p * 4 + 1] = hud_colors[p];
        hudpal[p * 4 + 2] = 0x1043;
    }
    hudpal[0] = 0x2CA6; // backdrop: dark office blue
    dmaCopyCGram((u8 *)hudpal, 0, 20 * 2);

    oamInit();
    oamInitGfxAttr(0x0000, OBJ_SIZE16_L32);
    dmaCopyVram(&p1_til, 0x0000, 4096);
    dmaCopyVram(&p2_til, 0x0800, 4096);
    dmaCopyVram(&p3_til, 0x1000, 4096);
    dmaCopyVram(&p4_til, 0x1800, 4096);
    oamMemory[512] = 0xAA; // OAM entries 0..3 are large, x < 256
    dmaCopyCGram((u8 *)&p1_pal, 128, 32);
    dmaCopyCGram((u8 *)&p2_pal, 144, 32);
    dmaCopyCGram((u8 *)&p3_pal, 160, 32);
    dmaCopyCGram((u8 *)&p4_pal, 176, 32);
    for (p = 0; p < 4; p++)
    {
        wins[p] = 0;
        dirty[p] = 0;
        idle[p] = 0;
        keys_now[p] = keys_down[p] = keys_before[p] = keys_held[p] = 0;
    }
    next_draw = 0;

    init_pieces();
    for (i = 0; i < ROWS; i++)
        row_map[i] = (WELL_TOP - HID + i) << 5;
    seeded = 0;
    best_hi = best_lo = team_hi = team_lo = 0;
    memset(dmark, 0, sizeof(dmark));
    mode = MODE_VERSUS;
    coop = 0;
    note_timer = 0;
    sprite_turn = 0;
    solid = 0;
    vbl_end = vbl_late = 0;

    detectMPlay5();
    npads = snes_mplay5 ? 4 : 2;

    to_title();
    dmaCopyVram((u8 *)bgmap, VRAM_BG_MAP, 2048);
    dmaCopyVram((u8 *)txtmap, VRAM_TEXT_MAP, 2048);
    bg_lo = bg_hi = txt_lo = txt_hi = 255;
    setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH);
    bgSetDisable(1);
    bgSetScroll(0, 4, 0);
    bgSetScroll(2, 4, 0);
    scroll_set = 0;
    setScreenOn();

    while (1)
    {
        update_game();
        draw_sprites();
        update_sound();
        prepare_vblank();
        work_end = scanline();
        WaitForVBlank();
        vblank_work();
        measure_vblank();
    }
    return 0;
}
