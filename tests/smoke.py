"""Headless gameplay and budget checks on the snes9x core: run with `make test`."""
import os
import random
import struct
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
MUS_OFF, MUS_PLAY, MUS_END = range(3)


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

    def rows(self, p):
        return [self.peek16('rows', p * ROWS + y) for y in range(ROWS)]

    def set_row(self, p, y, mask):
        self.poke16('rows', mask, p * ROWS + y)

    def until(self, state, frames):
        for _ in range(frames):
            if self.peek('state') == state:
                return
            self.run(1)
        assert self.peek('state') == state, (self.peek('state'), state)


def start(players, seq=None):
    """A match with these players; seq: one piece type for every piece, to make drops predictable."""
    g = Game(ROM)
    g.until(TITLE, 120)  # booting loads the music: about 35 frames
    g.run(2)
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
    g.press(0, 'b', frames=1)
    g.run(30)
    assert g.peek16('lines') == 2 and g.peek16('score_lo') == 40, (g.peek16('lines'), g.peek16('score_lo'))
    assert g.rows(0)[2:22] == [WALLS] * 20


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
    assert g.peek('state') == RESULT and g.peek('music') == MUS_OFF  # nobody lost: no ending
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
    g = start([0, 1])  # versus has no music: the players talk to each other
    g.run(120)
    assert g.peek('music') == MUS_OFF and g.loudness == 0, g.loudness
    g.press(0, 'b', frames=3)
    assert g.loudness > 1000, g.loudness  # lock


def patterns_start_silent():
    # the game plays patterns in any order: each one must start channels 1-6 with a note or a note-off, or a
    # note left on at the end of the previous pattern never stops (check this after editing the .it in a tracker)
    d = open(os.path.join(ROOT, 'music', 'mountain_king.it'), 'rb').read()
    orders, ins, smp, pats = struct.unpack('<4H', d[0x20:0x28])
    table = 0xC0 + orders + 4 * (ins + smp)
    for k in range(pats):
        p, mask, notes = struct.unpack('<I', d[table + 4 * k:table + 4 * k + 4])[0] + 8, {}, set()
        while d[p]:  # first row of the packed pattern
            ch = (d[p] & 0x7f) - 1
            if d[p] & 0x80:
                p += 1
                mask[ch] = d[p]
            m = mask[ch]
            if m & 1:
                notes.add(ch)
            p += 1 + (m & 1) + bool(m & 2) + bool(m & 4) + 2 * bool(m & 8)
        assert set(range(6)) <= notes, f'pattern {k}: channels {[c + 1 for c in sorted(set(range(6)) - notes)]} may hold a note'


def music():
    # solo: a group of patterns plays to its end, then the stack height picks the next group; level sets tempo
    g = start([0], seq=PIECE_O)
    g.poke('level', [8])  # level 9: 238 BPM, about 250 frames per pattern
    lag, worst, played = g.peek16('lag_frame_counter'), 0, []

    def play(frames, height):
        nonlocal worst
        for _ in range(frames):
            if height is not None:
                g.poke('high', [BOTTOM + 1 - height] * 4)
                g.poke('grav', [0] * 4)  # the piece hangs: no lock measures the real stack
            g.run(1)
            if g.peek('state') == PLAY:  # not the title rebuilt after the game over
                worst = max(worst, frame_cost(g))
            if not played or g.peek('music_pos') != played[-1]:
                played.append(g.peek('music_pos'))
    play(1100, 18)  # 18 rows: group 1-4 ends, then 19-20
    assert g.peek('music_tempo') == 238 and played[-5:] == [1, 2, 3, 4, 19], played
    play(500, 0)    # the stack drops during 19: 20 still plays, then back to 1
    assert played[-3:] == [19, 20, 1], played
    assert g.peek16('lag_frame_counter') == lag and worst < 200, (g.peek16('lag_frame_counter') - lag, worst)
    for y in range(4, 22):  # top out: the ending (21-22), then silence
        g.set_row(0, y, FULL ^ (1 << 9))
    g.press(0, 'b', frames=1)
    g.until(RESULT, 60)
    assert g.peek('music') == MUS_END
    play(600, None)
    assert played[-2:] == [21, 22] and g.loudness == 0, (played, g.loudness)
    print(f'music: played {played}, busiest frame {worst} scanlines')


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
    patterns_start_silent()
    music()
    wokas_match_art()
    four_clears_at_once()
    random_battle()
    print('ok')
