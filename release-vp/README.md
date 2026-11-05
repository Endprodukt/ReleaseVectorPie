# Release for VectorPie

VectorPie version by **Endprodukt**. Original game by **Gimogames**.

A port of **Release** to [VectorPie](https://github.com/Goldfinch2/VectorPie)
(Raspberry Pi + USB-DVG vector monitor), with two gameplay styles: the Android
original's (rebuilt from the APK, `src/original.c`, documented in
`../docs/android_*.md`) and Malban's Vectrex version's (`src/main.c`, ported from
its 6809 source with the same units, 50 Hz timing, phase tables and quirks,
documented in `../docs/spec_core.md` and `../docs/spec_objects.md`). This file
holds the developer notes; the project's overview is `../README.md`.

## Layout

| path | what |
|---|---|
| `src/main.c` | screens, sound, settings, `vp_run`, and the Vectrex gameplay |
| `src/original.c` | the Android original's gameplay (included by main.c; see `../docs/android_*.md`) |
| `src/data.c`, `src/release.h` | phase records, shield correction tables, vector shapes (from `data.i`) |
| `package/game.ini` | the package manifest for the cabinet |
| `src/font_xspace.inc` | the original game's font as strokes (generated) |
| `tools/sim/sim.c` | runs the game without the SDK (stubs + a bot), writes SVG frames |
| `tools/make-font/` | gets the font out of the APK and turns it into strokes |

## Build (WSL)

The SDK must be built and installed for each platform first
(`cmake --install` in the SDK puts it in `~/VectorPie/dist/<platform>`).
Builds go to `~/build/release-vp/<preset>` (outside `/mnt/f`, which is faster).

```sh
cmake --preset linux && cmake --build --preset linux   # test window via WSLg
~/build/release-vp/linux/release --windowed

cmake --preset pi && cmake --build --preset pi         # Pi (needs Debian 13 + tools/setup-wsl.sh)
cmake --build --preset pi --target vpk                 # -> release-0.1.0.vpk
```

Simulator (no window needed):

```sh
gcc -O2 -I<sdk>/include -Isrc tools/sim/sim.c src/data.c -lm -o /tmp/sim
/tmp/sim 30000 bot /tmp/frames 300 1500     # frames, mode, svg dir, frames to save
# modes: bot (near perfect), human (reaction time, misjudges; SIM_SEED=n), pattern, demo,
#        title, entry, menu (the last three just show the screen)
```

Text is drawn as boxes then. For real letters, compile with `-DSIM_REAL_TEXT` and link the
SDK's `src/text/vp_text.cpp` (built with g++).

`--classic` turns the effects layer off (sparks, shock rings, shield colors, twinkling stars,
announcements): the game logic is the same either way. In the simulator: `SIM_CLASSIC=1`.

## Controls

One button, the shield: P1 buttons 1-4 as standard (all the same, as on the
Vectrex), laid out in the service menu. START 1 or any button: play. Initials:
the button for the next letter (hold it to run), START to take it. Tab or F2 on
the attract pages: the service menu. Esc (the cabinet's exit button) returns to
the VectorPie menu.
