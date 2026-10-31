/*---------------------------------------------------------------------------------
    Cubicles: a falling-block versus game for 1 to 4 players around a WorkAdventure arcade cabinet.
    P1 on port 1, P2-P4 on a Multitap in port 2. Design decisions: DESIGN.md.
---------------------------------------------------------------------------------*/
#include <snes.h>
#include <string.h>

extern char blocks_a_til, blocks_a_pal, blocks_b_til, blocks_b_pal, walls_til, walls_pal;
extern char font_til, font_tilend, font_pal;
extern char p1_til, p2_til, p3_til, p4_til;
extern char p1_pal, p2_pal, p3_pal, p4_pal;
extern char sfx_lock, sfx_lock_end, sfx_clear, sfx_clear_end, sfx_junk, sfx_junk_end;
extern char sfx_join, sfx_join_end, sfx_topout, sfx_topout_end;

// Note: initialized globals live in ROM with this compiler, so only tables are initialized.
// Compiler bug: `a || !b` gives 0 when a is true. Never put a negation right of ||: write `b == 0`.

// ---- Layout -------------------------------------------------------------------------------
// The screen is 32x28 tiles. BG1 and BG3 scroll 4 px left, so a well (7 cells) plus one separator column is
// 64 px: four wells fill the 256 px screen, the first separator column straddling both screen edges.
// Per well: player label on row 1, Woka and next piece on rows 2-4, partition caps on row 5, the well on
// rows 6-25, floor on row 26, READY marks on row 27.
#define WELL_W 7
#define WELL_TOP 6
#define FLOOR_ROW 26
#define LABEL_ROW 1
#define NEXT_ROW 3
#define CAP_ROW 5
#define READY_ROW 27
#define MSG_ROW 14
#define WOKA_Y 15

// Board: one u16 bit mask per row, columns 0..6 in bits 3..9, walls in the other bits. Rows 0-1 are hidden
// above the well, 2-21 visible, 22-25 floor (all bits set), so a piece box (4 rows) never reads past it.
#define HID 2
#define BOTTOM 21
#define ROWS 26
#define CELLS 0x03F8
#define WALLS 0xFC07
#define FULL 0xFFFF
const u8 row_base[4] = {0, ROWS, 2 * ROWS, 3 * ROWS};

// VRAM (words): Woka frames $0000-$1FFF (P1..P4 at $0000, $0800, $1000, $1800), BG1 tiles $2000 (blocks_a, blocks_b, walls: 16 tiles each),
// BG3 font $6000, BG3 text map $7000, BG1 map $7400.
// CGRAM: text palettes 0..4 (colors 0..19), BG palettes 2..4, sprite palettes 8..11.
#define VRAM_BG_TILES 0x2000
#define VRAM_FONT 0x6000
#define VRAM_TEXT_MAP 0x7000
#define VRAM_BG_MAP 0x7400
#define TEXT_ATTR 0x2000 // high priority (above everything with BG3_MODE1_PRIORITY_HIGH), palette 0

// BG1 map entries (tile | palette), tile order from tools/mkgfx.py
#define PAL_A 0x0800
#define PAL_B 0x0C00
#define PAL_W 0x1000
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
const u16 piece_tile[7] = {PAL_A | 1, PAL_A | 2, PAL_A | 3, PAL_A | 4, PAL_B | 17, PAL_B | 18, PAL_B | 19};
const u16 ghost_tile[7] = {PAL_A | 5, PAL_A | 6, PAL_A | 7, PAL_A | 8, PAL_B | 21, PAL_B | 22, PAL_B | 23};

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
#define AWAY_FRAMES 900   // 15 seconds without input: the player is away
#define SEQ_LEN 252       // 36 shuffled sets of the 7 pieces, shared by all players in a match
#define MAX_JUNK 20
// fall speed in frames per row: versus every 30 seconds, solo every 10 lines
const u8 vs_speed[8] = {36, 24, 16, 10, 6, 4, 2, 1};
const u8 solo_speed[16] = {48, 40, 33, 27, 22, 18, 15, 12, 10, 8, 7, 6, 5, 4, 3, 2};
const u8 line_points[5] = {0, 1, 4, 9, 16}; // x 10 x (level + 1)
const u8 bit_count[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

// game states
#define ST_TITLE 0
#define ST_COUNTDOWN 1
#define ST_PLAY 2
#define ST_RESULT 3
#define ST_ENDING 4 // the match just ended: the result text waits for the next frame (text is slow)
// well phases; a well is alive from W_FALL up
#define W_OFF 0    // not in the match, or out of it
#define W_DYING 1  // topped out or away, waiting to gray out
#define W_FALL 2   // a piece is falling
#define W_SETTLE 3 // after a lock: rows collapse, junk rises, next piece
#define W_LOCK 4   // landed: locks on the first frame with room
// Locking, spawning or redrawing a piece costs ~10-20 scanlines: at most MAX_WORK per frame, the rest waits.
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

u16 rows[4 * ROWS];
u8 phase[4];
u8 ptype[4], prot[4], pcol[4], pyy[4], gy[4]; // pcol: box column + 3; pyy, gy: box row of piece and ghost
u16 pmask[20];       // each falling piece's 4 row masks, shifted to pcol (p * 4), then candidate masks (TM)
#define TM 16
u16 drawn[32];       // bgmap cells showing each well's piece (0-3) and ghost (4-7); 0xFFFF: none
u8 col0[4];          // screen column of each well's first cell
u8 grav[4], lockt[4], resets[4], das[4], wait[4];
u8 pending[4];       // junk rows on their way
u8 cleared[4];       // rows cleared by the last lock, still to collapse
u8 seqi[4];
u8 seq[SEQ_LEN];
u16 idle[4];
u16 keys_now[4], keys_down[4], keys_before[4]; // presses seen by the game, so a late frame can't lose one
u8 cheer[4];         // frames left of the Woka's hop after a clear
u8 high[4];          // highest filled board row (the Woka panics when it gets close to the top)

u8 inmatch[4], ready[4], away[4], wins[4];
u8 nplay, npads;
u8 state;
u16 state_timer;
u16 match_frames;
u8 vs_level, fall_speed;
u16 score_hi, score_lo, best_hi, best_lo, lines; // scores are hi * 10000 + lo
u8 level;
u8 heavy;  // one heavy board operation (collapse, junk, gray-out) per frame
u8 work;   // piece locks and redraws this frame (at most MAX_WORK)
u8 dirty[4]; // the piece needs a redraw (1), and its ghost moved too (2)
u8 next_draw; // first well to consider for redraws, rotating for fairness
u8 seeded;
char buf[12];

// ---- Sound ----------------------------------------------------------------------------------
brrsamples sfx_table[5];
u8 sfx_next;

void sfx(u8 id)
{
    if (sfx_next == SFX_NONE || id > sfx_next)
        sfx_next = id;
}

void init_sound(void)
{
    spcBoot();
    spcAllocateSoundRegion(12);
    spcSetSoundDataEntry(15, 8, 2, &sfx_lock_end - &sfx_lock, &sfx_lock, &sfx_table[0]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_clear_end - &sfx_clear, &sfx_clear, &sfx_table[1]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_junk_end - &sfx_junk, &sfx_junk, &sfx_table[2]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_join_end - &sfx_join, &sfx_join, &sfx_table[3]);
    spcSetSoundDataEntry(15, 8, 2, &sfx_topout_end - &sfx_topout, &sfx_topout, &sfx_table[4]);
    spcSetSoundTableEntry(&sfx_table[0]);
    sfx_next = SFX_NONE;
}

// Starting a sound costs about 40 scanlines, so it waits for a frame with little board work.
void update_sound(void)
{
    if (sfx_next != SFX_NONE && !heavy && work < 2)
    {
        spcPlaySound(sfx_next);
        sfx_next = SFX_NONE;
    }
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

void number(u8 x, u8 y, u16 v, u8 digits)
{
    buf[digits] = 0;
    while (digits)
    {
        digits--;
        buf[digits] = '0' + v % 10;
        v /= 10;
    }
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

// solo: score, lines and level right of the well, best score left of it
void draw_solo_hud(void)
{
    number(22, 9, score_hi, 3);
    number(25, 9, score_lo, 4);
    number(22, 13, lines, 4);
    number(22, 17, level + 1, 2);
}

void draw_best(u8 x, u8 y)
{
    number(x, y, best_hi, 3);
    number(x + 3, y, best_lo, 4);
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

// in src/well.asm
u8 fits(u8 mi, u8 at);       // masks pmask[mi..mi+3] fit with the box top on rows[at]?
u8 find_drop(u8 mi, u8 at);  // lowest row they fit at, falling from at
void draw_piece_cells(u8 p); // erase well p's piece and ghost, draw them again
u8 place(u8 p, u8 s, u8 col); // shape s at column col fits? then it becomes well p's piece masks
u8 lock_rows(u8 p);          // the piece joins the board: 255 if locked above the well, else its full rows
void collapse(u8 p);         // full rows go away
void add_junk_rows(u8 p, u8 n, u8 gap);
void gray_well_cells(u8 p);
void draw_next_cells(u8 p, u8 t);
u16 dw_p, dw_slot, dw_s, dw_tilex, dw_col, dw_y0, dw_tile, dw_dst, dw_base, dw_k, dw_n; // their scratch

void draw_piece(u8 p)
{
    if (dirty[p] & 2)
        gy[p] = find_drop(p << 2, row_base[p] + pyy[p]) - row_base[p];
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

u8 try_rotate(u8 p, u8 d)
{
    u8 r = (prot[p] + d) & 3, s = (ptype[p] << 4) | (r << 2), k, c;
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

// a topped-out or away player's well grays out on a later frame (heavy)
void top_out(u8 p)
{
    phase[p] = W_DYING;
    sfx(SFX_TOPOUT);
}

void gray_well(u8 p)
{
    gray_well_cells(p);
    mark_bg(WELL_TOP, FLOOR_ROW - 1);
}

// pending junk rows push the stack up, each with a gap in the same column (heavy)
void add_junk(u8 p)
{
    add_junk_rows(p, pending[p], rand() % WELL_W);
    pending[p] = 0;
    mark_bg(WELL_TOP, FLOOR_ROW - 1);
    sfx(SFX_JUNK);
    if ((rows[row_base[p]] | rows[row_base[p] + 1]) & CELLS)
        top_out(p); // pushed out of the top
}

void find_high(u8 p)
{
    u8 y = HID;
    while (y <= BOTTOM && !(rows[row_base[p] + y] & CELLS))
        y++;
    high[p] = y;
}

void draw_next(u8 p)
{
    draw_next_cells(p, seq[seqi[p]]);
    mark_bg(NEXT_ROW, NEXT_ROW + 1);
}

void spawn(u8 p)
{
    u8 t = seq[seqi[p]];
    if (++seqi[p] == SEQ_LEN)
        seqi[p] = 0;
    ptype[p] = t;
    prot[p] = 0;
    pcol[p] = SPAWN_COL;
    pyy[p] = t == PIECE_I ? HID - 1 : HID;
    grav[p] = 0;
    lockt[p] = 0;
    resets[p] = 0;
    draw_next(p);
    if (!place(p, t << 4, SPAWN_COL))
    {
        top_out(p);
        return;
    }
    phase[p] = W_FALL;
    dirty[p] = 3;
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
    level = lines / 10;
    if (level > 15)
        level = 15;
    draw_solo_hud();
}

void lock_piece(u8 p)
{
    u8 full, n, q;
    if (dirty[p])
        draw_piece(p); // the piece's final cells, which become blocks
    full = lock_rows(p);
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
    cleared[p] = n;
    wait[p] = n ? CLEAR_FRAMES : SPAWN_FRAMES;
    if (n)
    {
        sfx(SFX_CLEAR);
        cheer[p] = 40;
        if (nplay == 1)
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
    if (work < MAX_WORK)
    {
        work++;
        lock_piece(p);
    }
}

void update_piece(u8 p)
{
    u16 keys = keys_now[p], down = keys_down[p];
    u8 moved = 0, step = 0, speed;

    if (down & (KEY_A | KEY_X))
        moved = try_rotate(p, 1);
    else if (down & (KEY_B | KEY_Y))
        moved = try_rotate(p, 3);
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
    if (down & KEY_UP) // hard drop
    {
        pyy[p] = find_drop(p << 2, row_base[p] + pyy[p]) - row_base[p];
        dirty[p] = 3;
        land(p);
        return;
    }
    speed = fall_speed;
    if ((keys & KEY_DOWN) && speed > SOFT_DROP)
        speed = SOFT_DROP;
    if (fits(p << 2, row_base[p] + pyy[p] + 1))
    {
        lockt[p] = 0;
        if (++grav[p] >= speed)
        {
            grav[p] = 0;
            pyy[p]++;
            dirty[p] |= 1;
        }
    }
    else if (++lockt[p] >= LOCK_DELAY)
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
        collapse(p);
        mark_bg(WELL_TOP, FLOOR_ROW - 1);
        cleared[p] = 0;
        heavy = 1;
        return;
    }
    if (pending[p])
    {
        add_junk(p);
        heavy = 1;
        return;
    }
    if (work < MAX_WORK)
    {
        work++;
        find_high(p);
        spawn(p);
    }
}

void update_well(u8 p)
{
    if (phase[p] == W_FALL)
        update_piece(p);
    else if (phase[p] == W_SETTLE)
        settle(p);
    else if (phase[p] == W_LOCK && work < MAX_WORK)
    {
        work++;
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

void to_title(void)
{
    u8 p;
    for (p = 0; p < 4; p++)
    {
        inmatch[p] = 0;
        ready[p] = 0;
        away[p] = 0;
        phase[p] = W_OFF;
    }
    state = ST_TITLE;
    state_timer = 0;
    clear_screen();
    draw_logo();
    draw_title_labels();
    if (best_hi || best_lo)
    {
        text(10, TITLE_BEST_ROW, 0, "BEST");
        draw_best(15, TITLE_BEST_ROW);
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

// separators, caps and floor of all wells; with four wells the first column is also the last
void draw_partitions(u8 first)
{
    u8 k, c, y;
    u16 sep, cap, floor;
    for (k = 0; k <= nplay; k++)
    {
        c = (first + (k << 3)) & 31;
        if (nplay < 4 && k == 0)
        {
            sep = T_SEP_FIRST;
            cap = T_CAP_FIRST;
            floor = T_FLOOR_FIRST;
        }
        else if (nplay < 4 && k == nplay)
        {
            sep = T_SEP_LAST;
            cap = T_CAP_LAST;
            floor = T_FLOOR_LAST;
        }
        else
        {
            sep = T_SEP;
            cap = T_CAP;
            floor = T_FLOOR;
        }
        bgmap[(CAP_ROW << 5) + c] = cap;
        for (y = WELL_TOP; y < FLOOR_ROW; y++)
            bgmap[(y << 5) + c] = sep;
        bgmap[(FLOOR_ROW << 5) + c] = floor;
    }
}

void start_match(void)
{
    u8 p, k, j, y, first;
    u16 i;
    nplay = 0;
    for (p = 0; p < 4; p++)
    {
        inmatch[p] = ready[p] || (inmatch[p] && away[p] == 0);
        ready[p] = 0;
        away[p] = 0;
        nplay += inmatch[p];
    }
    clear_screen();
    deal_sequence();
    first = 16 - (nplay << 2);
    k = 0;
    for (p = 0; p < 4; p++)
    {
        phase[p] = W_OFF;
        pending[p] = 0;
        cheer[p] = 0;
        if (!inmatch[p])
            continue;
        col0[p] = first + (k << 3) + 1;
        k++;
        for (y = 0; y < ROWS; y++)
            rows[row_base[p] + y] = y > BOTTOM ? FULL : WALLS;
        for (y = WELL_TOP; y < FLOOR_ROW; y++)
        {
            i = (y << 5) + col0[p];
            for (j = 0; j < WELL_W; j++)
                bgmap[i + j] = T_EMPTY;
        }
        for (j = 0; j < WELL_W; j++)
            bgmap[(FLOOR_ROW << 5) + col0[p] + j] = T_FLOOR;
        for (j = 0; j < 8; j++)
            drawn[(p << 3) + j] = 0xFFFF;
        cleared[p] = 0;
        dirty[p] = 0;
        phase[p] = W_SETTLE;
        wait[p] = 0;
        seqi[p] = 0;
        idle[p] = 0;
        das[p] = 1;
        high[p] = BOTTOM + 1;
        player_label(col0[p], LABEL_ROW, p);
        if (nplay > 1)
            number(col0[p] + 5, LABEL_ROW, wins[p], 2);
    }
    draw_partitions(first);
    if (nplay == 1)
    {
        text(22, 8, 0, "SCORE");
        text(22, 12, 0, "LINES");
        text(22, 16, 0, "LEVEL");
        text(3, 8, 0, "BEST");
        draw_best(3, 9);
        score_hi = score_lo = lines = 0;
        level = 0;
        draw_solo_hud();
    }
    match_frames = 0;
    vs_level = 0;
    state = ST_COUNTDOWN;
    state_timer = 90;
    center(MSG_ROW, 0, "READY");
    draw_ready();
}

void show_result(void)
{
    u8 p, winner = 255;
    for (p = 0; p < 4; p++)
        if (inmatch[p] && alive(p))
            winner = p;
    if (nplay == 1)
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
        }
        else
            center(MSG_ROW, 0, "NEW CHALLENGER!");
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

// Start on a pad that is not playing: join the next match (or this one, on the title)
void check_joins(void)
{
    u8 p, joined = 0;
    for (p = 0; p < npads; p++)
    {
        keys_now[p] = padsCurrent(p);
        keys_down[p] = keys_now[p] & ~keys_before[p];
        keys_before[p] = keys_now[p];
        if ((keys_down[p] & KEY_START) && !ready[p] && (state == ST_TITLE || inmatch[p] == 0 || away[p]))
        {
            if (!seeded)
            {
                srand(snes_vblank_count);
                seeded = 1;
            }
            ready[p] = 1;
            away[p] = 0;
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
                center(TITLE_PROMPT_ROW, 0, "GET READY");
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
        }
        break;

    case ST_PLAY:
        if (state_timer && --state_timer == 0)
            clear_row(MSG_ROW);
        if (nplay == 1)
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
            if (alive(p))
            {
                if (keys_now[p])
                    idle[p] = 0;
                else if (++idle[p] == AWAY_FRAMES)
                {
                    away[p] = 1;
                    top_out(p);
                }
            }
            update_well(p);
            if (cheer[p])
                cheer[p]--;
        }
        draw_dirty();
        n = count_alive();
        if (nplay == 1 ? (n == 0 || ready[0] + ready[1] + ready[2] + ready[3]) : n <= 1)
            state = ST_ENDING;
        break;

    case ST_ENDING:
        show_result();
        break;

    case ST_RESULT:
        for (p = 0; p < 4; p++)
        {
            if (phase[p] == W_DYING)
                update_well(p);
            if (cheer[p])
                cheer[p]--;
        }
        if (--state_timer == 0)
        {
            n = 0;
            for (p = 0; p < 4; p++)
                n += ready[p] || (inmatch[p] && away[p] == 0);
            if (n)
                start_match();
            else
                to_title();
        }
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

void draw_sprites(void)
{
    u8 p, x, y, f;
    for (p = 0; p < 4; p++)
    {
        x = 0;
        y = 224;
        f = F_STAND;
        if (state == ST_TITLE)
        {
            x = title_x[p];
            y = TITLE_Y;
            f = F_TITLE + (ready[p] ? walk_cycle[((snes_vblank_count >> 3) + p) & 3] : 1);
        }
        else if (inmatch[p])
        {
            x = (col0[p] << 3) - 8;
            y = WOKA_Y;
            if (!alive(p))
                f = F_BACK;
            else if (cheer[p])
                y -= (cheer[p] & 8) >> 2;
            else if (high[p] < HID + 6) // stack close to the top: looks around nervously
                f = (snes_vblank_count & 16) ? F_LEFT : F_RIGHT;
        }
        put_woka(p, x, y, f);
    }
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
        keys_now[p] = keys_down[p] = keys_before[p] = 0;
    }
    next_draw = 0;

    init_pieces();
    for (i = 0; i < ROWS; i++)
        row_map[i] = (WELL_TOP - HID + i) << 5;
    seeded = 0;
    best_hi = best_lo = 0;
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
