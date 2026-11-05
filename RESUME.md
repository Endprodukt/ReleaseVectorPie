# Resume — continuing the VectorPie port

Read `STATUS.md` first (state, limitations, tasks), then `release-vp/README.md` (build).

## Environment (this PC)

- WSL distribution `Ubuntu-24.04` (user `ocare`, sudo needs a password). It has gcc, cmake,
  ninja, mingw-w64 and libsdl2-dev, which is enough for the **linux** preset.
- SDK copy built in WSL: `~/vp/sdk` (rsync from `F:\Release Game\VectorPie\sdk`), installed to
  `~/VectorPie/dist/linux`. After changing or updating the SDK: rsync, rebuild, `cmake --install build-linux`.
- Game builds: `~/build/release-vp/<preset>`. From PowerShell:
  `wsl -d Ubuntu-24.04 -- bash -lc "cd '/mnt/f/Release Game/release-vp' && cmake --build --preset linux"`
- Pi and Windows .exe builds need the SDK's `tools/setup-wsl.sh`, which only runs on Debian 13:
  not set up yet.

## Building for and loading onto the cabinet

Short way: bump `version` in `release-vp/package/game.ini`, then
`powershell -ExecutionPolicy Bypass -File "F:\Release Game\release-vp\tools\deploy.ps1"`
(copies the sources, builds on the cabinet, installs over the old version keeping `save/`).
A new game (not an update) needs a reboot before the menu lists it. The steps it does:

The cabinet (hostname `vectorpie`, IPv4 192.168.0.122; the name resolves to IPv6 only, and
known_hosts has the IPv4 address) has gcc, cmake, make and libsdl2-dev: the game is built on it.
SSH: `ssh -i %USERPROFILE%\.ssh\id_ed25519_vectorpie pi@192.168.0.122`.
If the cabinet refuses the key (its home folder was reset on 2026-11-05, taking authorized_keys and
~/release-build with it), the owner runs a script that appends the key on its own line and fixes the
rights (asks for the pi password; Claude does not enter passwords), then `mkdir -p ~/release-build`.

1. On the PC (PowerShell, in `F:\Release Game`):
   `tar -czf src.tar.gz --exclude=build --exclude=build-pc --exclude=build-pi --exclude=build-linux --exclude=.git -C VectorPie sdk -C . release-vp`
   (not `--exclude=build-*`: it also drops `tools/build-on-cabinet.sh`)
   and `scp` it to `~/release-build/` on the cabinet, then `tar -xzf` there.
2. On the cabinet, in `~/release-build`: `sh build-on-cabinet.sh` (from `release-vp/tools/`;
   strip CRs from it first: `sed -i 's/\r$//'`). It builds the SDK into `dist/`, the game, and
   `build-game/release-<version>.vpk`.
3. Install it as the menu does: `tar -xzf build-game/release-*.vpk -C /persistent/games/`
   (gives `/persistent/games/release/`; keep its `save/`), `mkdir -p .../release/save`. Or copy
   the .vpk to `/persistent/games/packages/` and use Settings -> GAMES -> INSTALL GAMES.
   Bump `version` in `package/game.ini` for an update.
4. The menu (`/usr/local/share/advance/vector_pie_menu`, started by `~/.bash_profile` on tty1,
   no restart loop) ignores SIGTERM; a new game shows after a reboot (`sudo systemctl reboot`
   works over SSH without a password). Its log, `/usr/local/share/advance/vector_pie_menu.log`,
   lists every package it read ("packages: <id> <version> (<manufacturer>)").
5. `manufacturer` in game.ini must be one of the menu's groups (ATARI, CINEMATRONICS, SEGA,
   MIDWAY, VECTORBEAM, CENTURI, EXIDY, ROCK-OLA, OTHER): anything else is read but never shown.
   Release uses Other, like the other homebrew games.

## How to check changes

- Build: no warnings expected (`-Wall -Wextra` via `vp_program`).
- Logic: build and run `tools/sim/sim.c` (see the README). Its bot prints statistics every
  10 s; pass frame numbers to get SVG pictures. To look at them, convert them to PNG (System.Drawing in
  PowerShell worked).
- Real run: `VP_STATS=1 ~/build/release-vp/linux/release --windowed` (WSLg window).

## Text

All screen text goes through `ui_text` / `ui_fit` / `text_c` in `main.c`: the original's font
(`xs_text`, from `src/font_xspace.inc`) with effects on, the SDK's Asteroids letters with
`--classic`. `xs_text` centers by the drawn ink, not the advance, so letters line up exactly.
For Asteroids, `VP_TEXT_CENTER` centers by the 12-unit advance of an 8-unit letter: `ui_text`
adds `height / 6`.

The font is made by `tools/make-font/`: `extract_fonts.py` (UnityPy) saves the APK's fonts,
`make_stroke_font.py` (numpy, Pillow) renders each glyph, thins it to its centre line, traces and
simplifies it (`EPSILON`: fewer points = fewer lines on the board) and writes the include.

## Reading the APK

UnityPy is installed in WSL at `~/unitypy-venv` (`~/unitypy-venv/bin/python -I script.py <dir>`).
The APK's Unity data (2019.4) only has white materials and particle systems; the object colors
are in the textures. The user's screenshots of the original are the reference for colors.

Over SSH from PowerShell: pass remote commands in single quotes (PowerShell eats `$`), and
avoid double quotes inside them (Windows ssh splits them); copy files with scp instead.

## Music

`assets/sounds/*.ogg` are whole tracks whose end is blended into the audio before the loop point
(`tools/loop_music.py --intro`, commands in its header; sources in the scratchpad's `audio/`).
main.c `music_load` decodes them at start with the SDK's stb_vorbis, `music_feed` streams them
(`vp_stream_*`) from 0 and jumps from the end to the loop point (table `music[]`, seconds).
The loop points in the table and in the files must match.

## Service menu and controls

As in the cabinet's other games (F:\Pinout, F:\Vector Racer, F:\Tempest 2000 Port, F:\Time Pilot 3D):
`src/service.c` is built after Pinout's, `src/vpbind.[ch]` must stay byte-identical to the master
in `F:\Tempest 2000 Port\src\vpbind.[ch]`. The shield button is read with `vpb_held(G.set.button
[SB_SHIELD])`; `vpb_update()` runs first in every update. The simulator stubs vpbind.

## The two gameplays

`G.fx` (service menu "Style", saved in settings.ini) picks the gameplay: 1 = the Android
original (`src/original.c`, included by main.c just before the sound section; prototypes near
the top of main.c), 0 = the Vectrex logic in main.c. The screens around the game are shared.
The original's tools and dumps (Il2CppDumper output, disassembler scripts) are in the session
scratchpad only; the findings are in `docs/android_*.md`. Simulator: `orig` mode plays the
original with its demo player, `demo` in original style never dies.

## Where things are in main.c

The data comes first, then: helpers/RNG, objects (pool of 38, list order), phases, bonus,
spawning (`check_spawn`, `no_spawn_now`), shield (`on_shield`, `exchange_to_explosion`,
`continue_shield`, `finish_shield`), `behave()` (one switch over all object types),
`game_step()` (the original's main-loop order), drawing, screens, sound synthesis, `vp_run`.
Raw Vectrex units are converted to board units only in `bx()` / `by()` (`G.ax`, `G.ay`:
2040 board units = the Vectrex half height, x squeezed by 3/4 for the 4:3 tube).
