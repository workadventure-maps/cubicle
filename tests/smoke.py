"""Headless gameplay and budget checks on the snes9x core: run with `make test`."""
import os
import random
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from snes import Snes, symbols  # noqa: E402

ROM = os.path.join(ROOT, 'cubicles.sfc')
SYM = symbols(os.path.join(ROOT, 'cubicles.sym'))
TITLE, COUNTDOWN, PLAY, RESULT, ENDING = range(5)
W_OFF, W_DYING, W_FALL, W_SETTLE = range(4)
ROWS, BOTTOM, WALLS, FULL, CELLS = 26, 21, 0xFC07, 0xFFFF, 0x03F8
PIECE_I, PIECE_O = 0, 1
O_COLS = 0x60  # an O piece at its spawn column covers columns 2 and 3 (bits 5 and 6)
W_LOCK = 4
# BG1 tiles (src/main.c)
T_EMPTY, T_FLASH, T_JUNK = 0x0809, 0x080A, 0x0C14
PIECE_TILES = {0x0801, 0x0802, 0x0803, 0x0804, 0x0C11, 0x0C12, 0x0C13}
GHOST_TILES = {0x0805, 0x0806, 0x0807, 0x0808, 0x0C15, 0x0C16, 0x0C17}
# open space: player k's O piece spawns on columns 7k+2 and 7k+3 of the shared well
def open_o_cols(k):
    return 3 << (7 * k + 5)


class Game(Snes):
    def peek(self, name, n=1):
        ram = self.wram()
        v = list(ram[SYM[name]:SYM[name] + n])
        return v if n > 1 else v[0]

    def peek16(self, name, index=0):
        ram, a = self.wram(), SYM[name] + 2 * index
        return ram[a] | ram[a + 1] << 8

    def poke(self, name, values, offset=0):
        self.poke_wram(SYM[name] + offset, values)

    def poke16(self, name, value, index=0):
        self.poke_wram(SYM[name] + 2 * index, [value & 255, value >> 8])

    # board rows are 32-bit (low word first): board b row y is rows[(b * ROWS + y) * 2]
    def rows(self, p):
        """Versus well p: the low words (the high word is all walls)."""
        return [self.peek16('rows', (p * ROWS + y) * 2) for y in range(ROWS)]

    def set_row(self, p, y, mask):
        self.poke16('rows', mask, (p * ROWS + y) * 2)

    def row32(self, y, b=0):
        return self.peek16('rows', (b * ROWS + y) * 2) | self.peek16('rows', (b * ROWS + y) * 2 + 1) << 16

    def set_row32(self, y, mask, b=0):
        self.poke16('rows', mask & 0xFFFF, (b * ROWS + y) * 2)
        self.poke16('rows', mask >> 16, (b * ROWS + y) * 2 + 1)

    def until(self, state, frames):
        for _ in range(frames):
            if self.peek('state') == state:
                return
            self.run(1)
        assert self.peek('state') == state, (self.peek('state'), state)


def start(players, seq=None, open_space=False):
    """A match with these players; seq: one piece type for every piece, to make drops predictable."""
    g = Game(ROM)
    g.run(30)
    if open_space:
        g.press(0, 'right', frames=2)  # Left/Right on the title switch the mode
        assert g.peek('mode') == 1
    for p in players:
        g.press(p, 'start', frames=2)
    g.until(COUNTDOWN, 400)
    if seq is not None:
        g.poke('seq', [seq] * 252)
    g.until(PLAY, 200)
    g.run(2)  # the first pieces spawn
    return g


def locks_at_the_bottom():
    g = start([0], seq=PIECE_O)
    assert g.peek('phase') == W_FALL
    g.press(0, 'b', frames=1)
    r = g.rows(0)
    assert r[20] == r[21] == WALLS | O_COLS, [hex(v) for v in r[18:22]]
    # solo well: first cell on screen column 13; board row 21 is screen row 25
    assert g.peek16('bgmap', 25 * 32 + 15) == 0x0800 | 2


def clears_and_scores():
    g = start([0], seq=PIECE_O)
    g.set_row(0, 20, FULL ^ O_COLS)
    g.set_row(0, 21, FULL ^ O_COLS)
    g.set_row(0, 19, WALLS | 1 << 3)               # a block above them, in column 0
    g.poke16('bgmap', T_JUNK, 23 * 32 + 13)        # (solo well: first cell on screen column 13)
    g.press(0, 'b', frames=1)
    g.run(30)
    assert g.peek16('lines') == 2 and g.peek16('score_lo') == 40, (g.peek16('lines'), g.peek16('score_lo'))
    assert g.rows(0)[2:22] == [WALLS] * 19 + [WALLS | 1 << 3]
    assert g.peek16('bgmap', 25 * 32 + 13) == T_JUNK and g.peek16('bgmap', 23 * 32 + 13) == T_EMPTY  # its cell too


def junk_goes_to_opponents():
    g = start([0, 1, 2], seq=PIECE_O)
    g.set_row(0, 20, FULL ^ O_COLS)
    g.set_row(0, 21, FULL ^ O_COLS)
    g.press(0, 'b', frames=1)          # P1 clears 2 lines: 1 junk row to each opponent
    assert g.peek('pending', 4) == [0, 1, 1, 0], g.peek('pending', 4)
    g.press(1, 'b', frames=1)          # P2 locks, then the junk rises under its piece
    g.run(12)
    r = g.rows(1)
    assert bin(r[21] & CELLS).count('1') == 6, hex(r[21])
    assert r[19] == r[20] == WALLS | O_COLS, [hex(v) for v in r[18:22]]
    assert g.peek('pending', 4)[1] == 0


def i_piece_turns_flat_against_the_wall():
    g = start([0], seq=PIECE_I)
    g.press(0, 'up', frames=1)         # Up turns too: upright, in box column 2
    for _ in range(6):
        g.press(0, 'left', frames=1)
        g.run(1)
    assert (g.peek('prot'), g.peek('pcol')) == (1, 1)   # against the left wall
    g.press(0, 'a', frames=1)
    assert (g.peek('prot'), g.peek('pcol')) == (2, 3), (g.peek('prot'), g.peek('pcol'))  # flat on columns 0-3


def challenger_interrupts_solo():
    g = start([0])
    g.press(0, 'left', frames=2)
    g.press(1, 'start', frames=2)
    g.run(3)
    assert g.peek('state') == RESULT
    g.until(COUNTDOWN, 200)
    assert g.peek('inmatch', 4) == [1, 1, 0, 0] and g.peek('nplay') == 2


def idle_player_stays_in():
    # nobody is put out for not touching the pad: 20 seconds later both wells still play
    g = start([0, 1])
    g.run(1200)
    assert g.peek('state') == PLAY and g.peek('phase', 2) == [W_FALL, W_FALL], g.peek('phase', 2)


def untouched_pad_sits_out_next_match():
    # P2 never touches the pad (its player left the cabinet): its well fills up, and it sits out the next match
    g = start([0, 1], seq=PIECE_O)
    g.press(0, 'left', frames=2)
    for y in range(4, 22):
        g.set_row(1, y, FULL ^ (1 << 9))  # P2's well nearly full, no line to clear
    g.until(RESULT, 400)
    assert g.peek('wins', 4) == [1, 0, 0, 0]
    g.until(COUNTDOWN, 200)
    assert g.peek('inmatch', 4) == [1, 0, 0, 0] and g.peek('nplay') == 1  # P1 plays on alone


def top_out_goes_back_to_title():
    g = start([0], seq=PIECE_O)
    for y in range(4, 22):
        g.set_row(0, y, FULL ^ (1 << 9))  # nearly full well, column 6 open: no line to clear
    g.press(0, 'b', frames=1)            # lands on the top rows; the next O can't spawn
    g.until(RESULT, 60)
    g.until(TITLE, 200)
    assert g.peek('inmatch', 4) == [0, 0, 0, 0]


def speed_rises_in_versus():
    g = start([0, 1])
    assert g.peek('fall_speed') == 36
    g.poke16('match_frames', 1799)
    g.run(1)
    assert (g.peek('vs_level'), g.peek('fall_speed')) == (1, 24)


def sound():
    g = start([0])
    g.run(30)
    assert g.loudness == 0
    g.press(0, 'b', frames=3)
    assert g.loudness > 1000, g.loudness  # lock


def wokas_match_art():
    # each Woka above its well is its hand-drawn frame from assets/sprites (exact colors, one palette per player)
    import tempfile
    from PIL import Image
    g = start([0, 1, 2, 3])
    assert g.peek('cheer', 4) == [0, 0, 0, 0] and g.peek('phase', 4) == [W_FALL] * 4  # standing, facing down
    with tempfile.NamedTemporaryFile(suffix='.png') as f:
        g.screenshot(f.name)
        screen = Image.open(f.name).convert('RGB').point(lambda v: v >> 3 << 3)
    oam = g.peek('oamMemory', 16)
    for p in range(4):
        x, y = oam[4 * p], oam[4 * p + 1]
        art = Image.open(os.path.join(ROOT, 'assets', 'sprites', f'p{p + 1}.png')).convert('RGBA').crop((21, 0, 42, 21))
        for ay in range(21):
            for ax in range(21):
                r, gg, b, a = art.getpixel((ax, ay))
                if a:  # the 21x21 frame sits at (5, 10) in the 32x32 sprite
                    assert screen.getpixel((x + 5 + ax, y + 10 + ay)) == (r >> 3 << 3, gg >> 3 << 3, b >> 3 << 3), \
                        f'P{p + 1} pixel ({ax},{ay})'


def mode_switches():
    g = Game(ROM)
    g.run(30)
    g.press(1, 'right', frames=2)
    assert g.peek('mode') == 1
    g.press(2, 'left', frames=2)       # any pad, either way
    assert g.peek('mode') == 0
    g.press(0, 'start', frames=2)
    g.press(1, 'start', frames=2)
    g.until(PLAY, 600)
    assert (g.peek('coop'), g.peek('nplay')) == (0, 2)
    g.press(0, 'left', frames=2)
    g.press(1, 'select', frames=2)     # during a match: Select switches the next game's mode
    assert g.peek('mode') == 1 and g.peek('coop') == 0
    for y in range(4, 22):
        g.set_row(1, y, FULL ^ (1 << 9))  # P2 tops out
    g.press(1, 'b', frames=2)
    g.until(RESULT, 100)
    g.until(COUNTDOWN, 300)
    assert (g.peek('coop'), g.peek('nplay')) == (1, 2)


def open_space_layout():
    g = start([0, 1, 3], seq=PIECE_O, open_space=True)
    assert g.peek('coop') == 1
    assert [(g.peek('wcells', 4)[p], g.peek('rbase', 4)[p]) for p in (0, 1, 3)] == [(21, 0)] * 3  # one board
    assert g.row32(10) == 0xFF000007 and g.row32(22) == 0xFFFFFFFF   # 21 columns in bits 3..23
    assert g.peek('phase', 4) == [W_FALL, W_FALL, W_OFF, W_FALL]
    assert [g.peek('pcol', 4)[p] for p in (0, 1, 3)] == [5, 12, 19]  # each above its own 7 columns


def open_space_clears():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    holes = open_o_cols(0) | open_o_cols(1)
    g.set_row32(20, 0xFFFFFFFF ^ holes)
    g.set_row32(21, 0xFFFFFFFF ^ holes)
    g.press(0, 'b', frames=1)
    assert g.peek16('lines') == 0      # P2's columns are still open
    g.press(1, 'b', frames=1)          # the team clears 2 lines: one score
    g.run(30)
    assert g.peek16('lines') == 2 and g.peek16('score_lo') == 40, (g.peek16('lines'), g.peek16('score_lo'))
    assert [g.row32(y) for y in range(2, 22)] == [0xFFFE0007] * 20
    assert g.peek('pending', 4) == [0, 0, 0, 0]  # no junk in open space


def teammates_pieces_are_solid():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    g.held[1] = {'left'}
    g.run(60)                          # P2 slides left until P1's piece stops it
    assert g.peek('pcol', 2) == [5, 7], g.peek('pcol', 2)
    g.held[1] = set()


def spawn_waits_for_teammate():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    g.press(0, 'b', frames=1)          # P1's piece locks: its spawn columns are free for a moment
    g.poke('wait', [60])               # (longer pause before P1's next piece)
    g.held[1] = {'left'}
    for _ in range(40):                # P2's piece takes P1's spawn place
        g.run(1)
        if g.peek('pcol', 2)[1] == 5:
            break
    g.held[1] = set()
    assert g.peek('pcol', 2)[1] == 5
    g.run(30)
    assert g.peek('phase', 2) == [W_SETTLE, W_FALL] and g.peek('state') == PLAY  # waiting, not out
    g.press(1, 'b', frames=1)
    g.run(3)
    assert g.peek('phase', 2) == [W_FALL, W_SETTLE], g.peek('phase', 2)


def piece_rests_on_teammate():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    g.held[1] = {'down'}
    g.run(20)                          # P2's piece 10 rows down
    g.held[1] = {'left'}
    for _ in range(40):                # under P1's piece
        g.run(1)
        if g.peek('pcol', 2)[1] == 5:
            break
    g.held[1] = set()
    assert g.peek('pcol', 2) == [5, 5]
    g.press(0, 'b', frames=1)          # hard drop: stops on P2's piece, doesn't lock
    y0, y1 = g.peek('pyy', 2)
    assert y0 == y1 - 2 and g.peek('phase') == W_FALL, (y0, y1)
    g.run(60)                          # longer than the lock delay: it waits on the piece below
    assert g.peek('phase') == W_FALL
    g.press(1, 'b', frames=1)
    g.press(0, 'b', frames=1)
    assert g.row32(21) & open_o_cols(0) and g.row32(19) & open_o_cols(0)


def coffee_break():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    g.press(1, 'b', frames=1)
    g.poke16('idle', 600, 1)           # as if P2 left 10 seconds ago
    g.run(20)
    assert g.peek('brk', 2) == [0, 1] and g.peek('phase', 2) == [W_FALL, W_SETTLE]
    assert g.peek('oamMemory', 8)[6] == 128 + 12  # P2's Woka turns its back (frame 3 of its 8)
    g.press(0, 'left', frames=1)
    assert g.peek('state') == PLAY     # P1 plays on alone
    g.press(1, 'a', frames=1)          # back from the break
    g.run(2)
    assert g.peek('brk', 2) == [0, 0] and g.peek('phase', 2)[1] == W_FALL


def everyone_on_break():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    g.press(0, 'b', frames=1)
    g.press(1, 'b', frames=1)
    g.poke16('idle', 600, 0)
    g.poke16('idle', 600, 1)
    g.until(RESULT, 30)
    g.until(TITLE, 200)                # nobody came back


def team_tops_out():
    g = start([0, 1], seq=PIECE_O, open_space=True)
    holes = open_o_cols(0) | open_o_cols(1)
    g.set_row32(20, 0xFFFFFFFF ^ holes)
    g.set_row32(21, 0xFFFFFFFF ^ holes)
    g.press(0, 'b', frames=1)
    g.press(1, 'b', frames=1)          # 2 lines: 40 points
    g.run(30)
    for y in range(4, 22):
        g.set_row32(y, 0xFFFFFFFF ^ (1 << 16))  # nearly full well, column 13 open
    g.press(0, 'b', frames=1)          # locks on the top rows; the next O can't come in
    g.until(RESULT, 60)
    g.run(5)                           # the shared well grays out once
    assert g.peek('phase', 2) == [W_OFF, W_OFF]
    assert (g.peek16('team_hi'), g.peek16('team_lo')) == (0, 40)  # the team's best
    g.until(COUNTDOWN, 300)            # both played: the next game starts
    assert g.peek('coop') == 1


def check_shared_well(g):
    """Open space: the board, the falling-piece overlay and the cells on screen agree."""
    col0, w = g.peek('col0'), g.peek('wcells')
    ram = g.wram()
    def u16(name, i):
        a = SYM[name] + 2 * i
        return ram[a] | ram[a + 1] << 8
    rows = [u16('rows', 2 * y) | u16('rows', 2 * y + 1) << 16 for y in range(ROWS)]
    ov = [u16('ov', 2 * y) | u16('ov', 2 * y + 1) << 16 for y in range(ROWS)]
    phase, pyy, pk = (list(ram[SYM[n]:SYM[n] + 4]) for n in ('phase', 'pyy', 'pk'))
    inmatch = list(ram[SYM['inmatch']:SYM['inmatch'] + 4])
    want = [0] * ROWS
    piece_cells, ghost_cells = set(), set()
    for q in range(4):
        if not inmatch[q]:
            continue
        if phase[q] in (W_FALL, W_LOCK):
            for i in range(4):
                m = u16('pmask', 4 * q + i) << (8 * pk[q])
                assert not (m & want[pyy[q] + i]), f'P{q + 1} overlaps a teammate'
                assert not (m & rows[pyy[q] + i]), f'P{q + 1} overlaps the stack'
                want[pyy[q] + i] |= m
        for k in range(8):
            cell = u16('drawn', 8 * q + k)
            if cell != 0xFFFF:
                (piece_cells if k < 4 else ghost_cells).add(cell)
    assert ov == want, 'overlay differs from the falling pieces'
    for y in range(2, BOTTOM + 1):
        for c in range(w):
            cell = (y + 4) * 32 + col0 + c
            t = u16('bgmap', cell)
            if rows[y] >> (c + 3) & 1:
                assert t in PIECE_TILES or t in (T_FLASH, T_JUNK), f'block at {c},{y} shows {t:04x}'
            elif t in PIECE_TILES:
                assert cell in piece_cells, f'stale piece cell at {c},{y}'
            elif t in GHOST_TILES:
                assert cell in ghost_cells, f'stale ghost cell at {c},{y}'
            else:
                assert t == T_EMPTY, f'empty cell at {c},{y} shows {t:04x}'


def open_space_worst_frame():
    # a tall stack in a 28-column well, three teammates' pieces in the air, P1 clears 2 lines
    rng = random.Random(2)
    g = start([0, 1, 2, 3], seq=PIECE_O, open_space=True)
    g.run(30)                          # (GO! is gone)
    for y in range(8, 22):             # random blocks, with a chute down P1's columns
        g.set_row32(y, 0x80000007 | rng.getrandbits(28) << 3 & ~(1 << (3 + rng.randrange(28))) & ~open_o_cols(0))
    g.set_row32(20, 0xFFFFFFFF ^ open_o_cols(0))
    g.set_row32(21, 0xFFFFFFFF ^ open_o_cols(0))
    g.poke('dirty', [3, 3, 3, 3])
    g.run(2)
    lag, worst = g.peek16('lag_frame_counter'), 0
    g.held[0] = {'b'}
    for _ in range(60):
        g.run(1)
        g.held[0] = set()
        worst = max(worst, frame_cost(g))
    assert g.peek16('lines') == 2
    assert g.peek16('lag_frame_counter') == lag, 'lag frames'
    assert worst < 200, f'busiest frame used {worst} scanlines'
    print(f'open space, clear under a tall stack: busiest frame {worst} scanlines')


def open_space_battle():
    """4 players at random in one well, with nearly full rows slipped in: the board, the overlay and the screen
    stay consistent, within the budgets."""
    rng = random.Random(4)
    g = start([0, 1, 2, 3], open_space=True)
    lag = g.peek16('lag_frame_counter')
    checks, cleared, worst, games, recent = 0, 0, 0, 0, []
    for frame in range(5000):
        if frame % 8 == 0:
            for p in range(4):
                g.held[p] = {rng.choice(['left', 'right', 'up', 'a', 'down', 'none'])} - {'none'}
                if rng.random() < 0.15:
                    g.held[p].add('b')
        state = g.peek('state')
        if frame % 90 == 0 and state == PLAY:
            fill_bottom_row(g, rng)
        g.run(1)
        state = g.peek('state')
        recent = (recent + [state])[-4:]
        games += recent[-2:] == [RESULT, COUNTDOWN]
        building = COUNTDOWN in recent or (state == RESULT and g.peek16('state_timer') == 0)
        now = g.peek16('lag_frame_counter')
        assert now == lag or building, f'lag at frame {frame}, states {recent}'
        lag = now
        if not building:
            worst = max(worst, frame_cost(g))
        if state == PLAY and frame % 5 == 0:
            check_shared_well(g)
        if frame % 10 == 0:
            checks += check_screen(g)
        cleared = max(cleared, g.peek16('lines'))
    assert checks > 100 and cleared > 0, (checks, cleared)
    assert g.peek16('vbl_late') == 0
    assert worst < 200, f'busiest frame used {worst} scanlines'
    print(f'open space battle: {games} new games, up to {cleared} lines, busiest frame {worst} scanlines')


def fill_bottom_row(g, rng):
    """Fill the bottom row of the shared well but one column, blocks drawn, if no piece is anywhere near it."""
    if g.peek('ov', 4 * 26)[4 * 14:] != [0] * (4 * 12) or g.peek('state') != PLAY:
        return
    col0, w = g.peek('col0'), g.peek('wcells')
    y, hole = BOTTOM, rng.randrange(w)
    row = g.row32(y)
    if row & (1 << (hole + 3)):
        return  # (the row would be full without a lock)
    for c in range(w):
        if c != hole and not row >> (c + 3) & 1:
            row |= 1 << (c + 3)
            g.poke16('bgmap', T_JUNK, (y + 4) * 32 + col0 + c)
    g.set_row32(y, row)
    lo, hi = g.peek('bg_lo'), g.peek('bg_hi')
    g.poke('bg_lo', [min(lo, y + 4)])
    g.poke('bg_hi', [y + 4 if hi == 255 else max(hi, y + 4)])


def check_screen(g):
    """Once pending updates are flushed, the BG maps in VRAM must equal the game's RAM copies.
    A transfer that overran VBlank would leave stale tiles behind."""
    if g.peek('bg_hi') != 255 or g.peek('txt_hi') != 255 or g.peek16('bg_len') or g.peek16('txt_len'):
        return False
    ram, vram = g.wram(), g.vram()
    for buf, vaddr in (('bgmap', 0x7400), ('txtmap', 0x7000)):
        a = SYM[buf]
        assert ram[a:a + 2048] == vram[vaddr * 2:vaddr * 2 + 2048], buf + ' differs from VRAM'
    return True


def frame_cost(g):
    # scanlines from the end of VBlank work to the end of the frame's logic (~240 available)
    return (g.peek16('work_end') - g.peek16('vbl_end')) % 262


def four_clears_at_once():
    # all four players clear 2 lines on the same frame: 4 locks, 4 collapses, 12 junk rows
    g = start([0, 1, 2, 3], seq=PIECE_O)
    for p in range(4):
        g.set_row(p, 20, FULL ^ O_COLS)
        g.set_row(p, 21, FULL ^ O_COLS)
    lag, worst = g.peek16('lag_frame_counter'), 0
    for p in range(4):
        g.held[p] = {'b'}
    for _ in range(60):
        g.run(1)
        for p in range(4):
            g.held[p] = set()
        worst = max(worst, frame_cost(g))
    assert g.peek16('lag_frame_counter') == lag, 'lag frames'
    assert g.peek16('vbl_late') == 0
    for p in range(4):  # 3 junk rows from the others, then the new piece
        assert bin(g.rows(p)[21] & CELLS).count('1') == 6
    assert worst < 200, f'busiest frame used {worst} scanlines'
    print(f'four clears at once: busiest frame {worst} scanlines')


def random_battle():
    rng = random.Random(1)
    g = start([0, 1, 2, 3])
    lag = g.peek16('lag_frame_counter')
    recent, checks, matches, worst = [], 0, 0, 0
    for frame in range(4000):
        if frame % 8 == 0:
            for p in range(4):
                g.held[p] = {rng.choice(['left', 'right', 'up', 'a', 'down', 'none'])} - {'none'}
                if rng.random() < 0.15:
                    g.held[p].add('b')
        if frame % 150 == 0 and g.peek('state') == PLAY:
            g.poke('pending', [rng.randrange(4) for _ in range(4)])
        g.run(1)
        state = g.peek('state')
        recent = (recent + [state])[-4:]
        matches += recent[-2:] == [RESULT, COUNTDOWN]
        # only building a new match may lag: it redraws the whole screen, as the result screen's timer runs out
        building = COUNTDOWN in recent or (state == RESULT and g.peek16('state_timer') == 0)
        now = g.peek16('lag_frame_counter')
        assert now == lag or building, f'lag at frame {frame}, states {recent}'
        lag = now
        if not building:
            worst = max(worst, frame_cost(g))
        if frame % 10 == 0:
            checks += check_screen(g)
    assert checks > 100 and matches >= 2, (checks, matches)
    assert g.peek16('vbl_late') == 0, g.peek16('vbl_late')
    assert worst < 200, f'busiest frame used {worst} scanlines'
    print(f'random battle: {matches} new matches, busiest frame {worst} scanlines')


if __name__ == '__main__':
    locks_at_the_bottom()
    clears_and_scores()
    junk_goes_to_opponents()
    i_piece_turns_flat_against_the_wall()
    challenger_interrupts_solo()
    idle_player_stays_in()
    untouched_pad_sits_out_next_match()
    top_out_goes_back_to_title()
    speed_rises_in_versus()
    sound()
    wokas_match_art()
    mode_switches()
    open_space_layout()
    open_space_clears()
    teammates_pieces_are_solid()
    spawn_waits_for_teammate()
    piece_rests_on_teammate()
    coffee_break()
    everyone_on_break()
    team_tops_out()
    four_clears_at_once()
    random_battle()
    open_space_worst_frame()
    open_space_battle()
    print('ok')
