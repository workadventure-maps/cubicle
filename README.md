# Cubicles

A falling-block versus game for the Super Nintendo, for 1 to 4 players around a WorkAdventure arcade cabinet.
See [DESIGN.md](DESIGN.md).

## Play

- Press **Start** to join. Alone, it's an endless score attack; a second player's Start turns it into a versus
  match. Players 2 to 4 need a Multitap in port 2 (snes9x: set port 2 to "Multitap").
- **Left / Right** move, **Down** drops faster, **Up** drops at once, **A / X** turn clockwise, **B / Y** turn
  counter-clockwise.
- Clearing 2 or more lines at once sends junk rows to every opponent. Last well standing wins the match; the
  pieces fall faster every 30 seconds. A player who doesn't touch the pad for 15 seconds is out until they
  press Start again.

## Build

Linux, Python 3 with Pillow and NumPy, make, curl, unzip.

```bash
tools/setup.sh   # downloads PVSnesLib 4.6.0 and the snes9x libretro core into .tools/
make             # builds cubicles.sfc
make test        # headless gameplay and budget tests on the snes9x core
```

## Layout

| Path | What |
|---|---|
| `src/main.c` | game: matches, wells, pieces, HUD, VBlank transfers |
| `src/well.asm` | hot paths in 65816 assembly: collisions, landing row, piece drawing, collapsing rows, junk |
| `data.asm` | graphics and sound data included in the ROM |
| `tools/mkgfx.py` | builds the blocks, partitions, font and Woka sprites |
| `tools/mksfx.py` | synthesizes the placeholder sound effects |
| `tools/snes.py` | small headless libretro frontend: scripted input, Multitap, screenshots, RAM/VRAM reads |
| `tests/smoke.py` | gameplay tests, including VBlank and frame-time budgets |

## Credits

- Characters: Woka parts designed by the WorkAdventure team (`assets/woka/`), © WorkAdventu.re, MIT.
- Font: PVSnesLib example font by Alekmaul, zlib license, see `assets/font.LICENSE.txt`.
- Built with [PVSnesLib](https://github.com/alekmaul/pvsneslib).

## License

MIT, see [LICENSE](LICENSE).
