# Status â€” Release for VectorPie

Last updated: 2026-11-05

## What is here

| path | what |
|---|---|
| `Release_1.1.2_APKPure.apk` | the Android original (Unity/IL2CPP); not used for the port |
| `VRelease/` | Malban's Vectrex version: 6809 source, public domain (see `VRelease/README.txt`) |
| `docs/spec_core.md` | spec from the 6809 source: main loop, shield, kill test, phases, spawning, scoring, coordinates |
| `docs/spec_objects.md` | spec: every object, explosions, popups, RNG, starfield, all vector shapes |
| `VectorPie/` | clone of github.com/Goldfinch2/VectorPie (the SDK is in `VectorPie/sdk`) |
| `release-vp/` | **the port** (C, VectorPie SDK); see `release-vp/README.md` |

## Current state (v0.2.0)

Two gameplays, chosen in the service menu (Style) and saved; `--classic` forces Vectrex:
- **Original** (default): the Android original's gameplay, rebuilt from its code and data
  (`src/original.c`; rules in `docs/android_player.md`, `docs/android_enemies.md`,
  `docs/android_values.md`, read from the APK with Il2CppDumper + capstone). Its screen
  (720 px tall, 960 wide on a 4:3 tube), player (22 px polygon, 5..8 sides, 5 deg/s), ring
  (grows 1.5/s, width 12.5/s, hits what it touches at release, x(1 + 0.1(k-1))), all nine
  enemy types with their prefab values, the 5-wave spawner on real time, GameTimeScale
  +0.0075/s, the three power-ups (shield 45 s, ring speed / width x2 for 15 s), the star's
  growing payout, the "+N" popups. Not checked against the running original; the hit tests
  are distance approximations of Unity's box colliders.
- **Vectrex**: Malban's Vectrex version (the rest of this file below), Vectrex look.

Service menu (v0.3.0): the cabinet's shared menu, as in Pinout / Vector Racer / Tempest 2000 /
Time Pilot 3D (`src/service.c`, built after Pinout's; `src/vpbind.c` is the shared controls
module, a byte-identical copy of the Tempest 2000 port's master). F2 or TAB (or MENU BUTTON)
from anywhere but the initials entry. Pages: GAMEPLAY (STYLE original/vectrex, ATTRACT SOUND,
RESET HIGHSCORES), CONTROLS (SHIELD BUTTON, standard yoke buttons 1+2, left Ctrl, joy1
button 1; MENU BUTTON), SOUND (master, music, effects volume). CANCEL / SAVE, back to the title.
Saved in `save/settings.ini` (style, attract_sound, volume, music_volume, effects_volume,
shield, menu_key; the 0.1.x/0.2.x keys are still read).
The screens around the game (title, instructions, high scores, game over, initials) now look the
same in both styles: the original's font, starfield, logo and shapes; STYLE only changes the game.

v0.3.1/0.3.2, the user's choices (deviations from the original):
- Bigger text: credit line, "Demo", the instructions (panel full width, text 128).
- The player and its ring are always a hexagon (the original turns 5, 6, 7, 8 sides, one more after
  every ring; the Vectrex version per press). `o_next_sides` / `new_shield`.
- (Until v0.4.1 only START started a game; since v0.4.2 START or any button does.)

v0.4.0:
- Game music: the owner's "Starship 8-Bit" (C:\Users\ocare\Downloads), looped bar-exactly:
  148.3 bpm, bar 1.6183 s from 0.315 s, loop bar 2 .. bar 46 (3.5516 .. 74.7587 s, eleven 4-bar
  phrases), one-beat crossfade; Ogg Vorbis (`assets/sounds/starship.ogg`). The track repeats
  nothing exactly, so this is a musical loop, not a sample-exact one (preview: see v0.4.1).
- Title music: the original's "opening", now a seamless loop (0.9451 s .. end, Ogg). The
  original's dubChip (quiet one-bar intro, so its loop dropped into near-silence) is no longer
  used. All cut with `tools/loop_music.py`; package 2.6 MB instead of 19.5 MB.
- Fixed: the starfield stood still during the original's game since v0.2.0 (bg_update ran only
  outside the game). The title draws the player and its ring exactly as the game does
  (`o_draw_player`), the ring opening for 0.8 s and starting again.

v0.4.1:
- Both tracks play from their very start once, then loop from the loop point (Starship 3.5516 s,
  opening 0.9451 s). The Oggs are the whole tracks with the loop's blended end
  (`loop_music.py --intro`); main.c decodes them at start (the SDK's stb_vorbis) and streams them
  (`vp_stream_*`, a quarter second ahead), falling back to `vp_music_play` if decoding fails.
  Checked: decoded lengths exact, Starship's jump smaller than an average sample step.
  Preview: `F:\Release Game\starship_game_preview.wav` (whole track, then 10 s of the loop).
- The whoosh (v0.4.0) is removed again, at the user's request.
- Installed on the cabinet 2026-11-05 (save/ kept). Its home folder had been reset that day
  (system update?): the SSH key and ~/release-build were gone and were set up again.

v0.4.2 (on the cabinet 2026-11-05):
- "Press start", "Demo", "New high score" and the START lamp blink on one beat (`blink_on`,
  20 frames on, 20 off, from the page's start).
- A game starts with START or any button (`start_pressed`: the laid out SHIELD BUTTON and
  p1_button1..6) on the title pages, in the demo, on the attract's score table and for Play
  Again after a game over. The game over page no longer has "Title (Button)": it goes back to
  the title after 12 s. This replaces v0.3.2's "only START starts".
- The demo player can die (both styles): the original's demo bot starts no more rings after
  20..35 s (`demo_careless`), so its demo ends with a death after about 26..33 s (sim, 8
  seeds); the Vectrex demo dies after about 20 s. A demo's death shows Game Over / Demo for
  5 s, then the score table; the title music keeps playing.
- Attract tutorial (16 s, after the logo, before the enemy list; `orig_draw_tutorial` in
  original.c, captions `tutor_caption` in main.c): the hexagon, "Hold the button to open your
  ring", a fighter flies in, "Let go when an enemy is inside the ring", it is destroyed; then
  "Stars are friends: let them live and they pay you points" (a star circling, +4, +6) and
  "Green balls: let them hit you for a shield, speed or a wider ring" (a ball flies in, the
  player turns green, "Shield"). Ring slower than in the game, to be watched.

v0.5.0 (on the cabinet 2026-11-05):
- The original style's enemies and objects are 3D wire models turning in space (original.c,
  "3D models"), in the original's colours and sizes, with a little perspective (O_PERSP 260 px)
  and far edges dimmer: fighter/stealth an octahedron round three axes, diver a pyramid pointing
  at the player and turning round its axis, kite a cube along its motion (a cube within until
  hit), its tail an octahedron, carrier a globe of three circles, its shots tetrahedra, the ally
  a five-pointed star with depth turning like a coin, the power-up a small globe round an
  octahedron. The tutorial and the instructions page (`orig_draw_icon`) show the same models.
  The Vectrex style's own game keeps the Vectrex shapes.
- Explosions as in Tempest 2000 (draw_prex / drawsphere): every kill makes three rings of
  17/15/13 dots, each twice as wide as the last, flashing between the enemy's colour and white
  and fading (0.7 s); kites and carriers also burst into a turning sphere of 8 circles of 16
  dots. The original's particles fly out in 3D (longer and brighter when near). A taken
  power-up rings the player. Effects use their own random numbers (`fx_seed`), so they do not
  change the game's.
- Lines a frame: at most ~940 in simulated games (was ~470); `SIM_LINES=1` in the simulator
  reports the most, `SIM_LONG=1` keeps its bot from tiring.

v0.5.1 (on the cabinet 2026-11-05), more flash and particles:
- A killed enemy's model breaks into its edges (shards, `o_shatter`): each flies off from the
  centre, tumbling, and fades (0.5..0.9 s).
- Every kill: a white flash of rays (FX_FLASH, 0.18 s), more particles (14/28/40) and fast white
  sparks, wider dot rings; the player's outline lights up white (`O.flash`).
- The ring let go flies on outward as a white shock wave (FX_SHOCK), bigger and brighter with
  every enemy it took; three or more at once: a big flash of 24 rays from the player, 30 sparks,
  the player flashing. A taken power-up flashes too; the player's death has more particles.
- The tutorial shows the flash and the shock wave too.
- Lines a frame: at most ~1350 in simulated games (v0.5.0 ~940). If the monitor flickers: fewer
  shards (O_SHARDS) or dots, or no sphere.

v0.5.2 (on the cabinet 2026-11-05):
- Starship's loop: the old jump (74.7587 -> 3.5516) slipped the beat by ~26 ms. New: 0 .. 71.5156
  once, then loops from 16.4980 (bar 44 -> bar 10, 55 s; the bars before both 0.89 alike, the
  beats aligned to 5 ms by onset envelopes; 150 ms crossfade). Found with the scratchpad's
  scripts/barloop.py (run python with -I: the folder has a dis.py). Preview:
  `F:\Release Game\starship_game_preview.wav`.
- (0.5.2 had a literal firework at the death; 0.5.3 replaced it, the user meant "something big".)
- The game over panel waits 2 s for the death, Play Again 2.5 s; the demo's game over lasts 6 s.

v0.5.3 (on the cabinet 2026-11-05): the death is a big explosion (both styles; original.c "the
death"): the player's outlines break into shards, a white star of light, a sphere and rings of
dots, sparks, the screen shakes (`O.shake`, o_bx/o_by); three hexagon waves (white, the accent
colour, red) roll to the screen's edges; the first (700 px/s) blows up every enemy it reaches
(shards, particles, rings, spheres for kites and carriers); five blasts follow round the centre
over 1.25 s. Up to ~2270 lines a frame for about a second (sim, demo death with 9 enemies on
screen): if the monitor flickers, fewer blasts or no spheres in them. Sim: `SIM_DEATH=1` keeps
the demo running after its death (scratchpad scripts/death.sh renders it).

v0.5.4 (on the cabinet 2026-11-05): the ship "twitched" on the monitor. Cause: the USB-DVG draws
about 3 million points a second and draws a long frame faster and coarser, so a frame's cost
(lit + invisible length) changing from frame to frame moves the picture. The starfield's 48 dots,
drawn in their own order, cost 45..72k board units of invisible travel a frame (more than half
of it, different every frame). Now the stars (main.c bg_draw), the enemies, the shards and the
particles (original.c o_path, o_draw_enemies, o_draw_debris) are drawn in a short path (each the
nearest one left). A normal game frame: ~60..70k units instead of ~105k, the stars ~17k, steady;
several kills at once 250k (was 355k); the death explosion still up to ~500k for about a second.
Measure with the simulator: SIM_LINES=1 (the most lines and length), SIM_COST=1 (every frame),
SIM_PARTS=1 (stars and HUD apart); scratchpad scripts/cost.sh and parts.sh.
NOT verified on the monitor yet whether the twitch is gone.

v0.5.5 (on the cabinet 2026-11-05): initials entry shows its time (30 s, ENTRY_TIMEOUT): a bar
shrinking to the middle with the seconds under it, green, yellow from 10 s, red and blinking in
the last 5 s, which also tick (the star sound, low).

v0.5.6 (on the cabinet 2026-11-06): new music
from the owner's "Retro-Futurist Space" (C:\Users\ocare\Downloads, 79.9 s, 110 bpm, bar
2.18182 s from 1.1559 s). Attract pages (MUSIC_TITLE, `assets/sounds/attract.ogg`): the song
from its start once to 68.80 s, then bars 16..32 (33.8832 .. 68.8003) loop. Initials entry
(new MUSIC_ENTRY, `assets/sounds/entry.ogg`): bars 6..14 (12.0650 .. 29.6019) looped whole; it
was silent before. The original's "opening" is no longer used (removed from the assets).
Previews: `F:\Release Game\attract_preview.wav`, `entry_preview.wav`.

High scores are seeded on the original's scale, top 4000 (END 4000, NMA 3400, MIO 2900, MAL
2400, VTK 2000, GMO 1600, TOM 1300, ALX 1000, VEC 700, PIE 400); entries seeded by earlier
versions are replaced on start, players' own entries are kept (OLI 2906 on the cabinet). On the cabinet the
table was reset to the new defaults on 2026-11-05 at the user's request (OLI's Vectrex-scale
scores removed; the old table is in `/tmp/highscores.before.txt` until the next reboot).

## Earlier state (v0.1.x, Vectrex gameplay)

- Game logic ported 1:1 from the Vectrex source: shield (5-8 sides, growth, cooldown, the
  polygon kill test with the original correction tables), all enemies (X, Hidden X, Hunter,
  Bomber + shots, Dragon + escorts, squads), stars with the bonus counter, the mystery "?" with
  its 3 upgrades, the 34-entry phase list, the spawn-rate logic, the score multiplier,
  explosions, popups, the starfield, the RNG and the demo mode.
- Screens: title, instructions, high scores, demo (attract mode), game over, initials entry,
  service menu.
- One button: the function `shield` (standard: P1 buttons 1-4), laid out in the service menu
  with the SDK's `vp_capture` (up to 4 controls; saved in `save/controls.ini`).
- Service menu: Tab or F2 from the attract pages or the demo. Rows: SHIELD BUTTON, EFFECTS
  on/off, SOUND on/half/off, RESET HIGH SCORES (START twice), EXIT (or Tab/F2 again).
  Effects and sound are saved in `save/settings.ini`; `--classic` still forces effects off.
- Initials: the button goes to the next letter (held: runs), START takes it; a stick's
  up/down/left also work; 30 s limit. Letters A-Z 0-9 . -
- High scores: the SDK's table (`save/highscores.txt`, top 10, shown in the VectorPie menu).
  A new cabinet starts with MAL 20000, VTK 15000, GMO 12000, TOM 9000, ALX 7000, VEC 5000,
  PIE 4000, ARC 3000, REL 2000, ASE 1000. Chosen from simulated games: a human-like bot
  (reaction time, misjudged releases) scored a median of ~1500 in 30-50 s, at best ~4100;
  a near-perfect bot ~10000 after 80 s and ~33000 after 4 min. So a first game can get onto
  the table; the top needs a few minutes of good play.
- Colors from the Android original, taken from the user's screenshots of its tutorial and
  pause screens (the APK's materials and particle systems are all white; the colors are in
  its textures): blue "+" X, red triangle Hunter, faint blue Hidden X, green Dragon and tail,
  orange Bomber, yellow Star, green Mystery, lilac score, salmon best score, shield and base
  white with green edges, background stars = small dots in many colors.
- Effects layer (on by default, `--classic` turns it off; own random numbers, outside the object
  pool, so the game plays identically: the simulator gives the same score with and without):
  sparks on every kill, a shock ring on release, a shield that pulses when full, with white
  spokes, a base that pulses red when an enemy is close, a counter-rotating core,
  a bonus time arc and gold ring, popups colored by multiplier, phase/raid announcements, a
  flickering wounded dragon with tethers to its escorts, background stars that each have their
  own color and twinkle as they fly towards the viewer (dots only, as in the original),
  rainbow title letters.
- Shapes as in the Android original: Dragon = green square (double edge) with a tail of small
  squares, Bomber = orange circle, Mystery = green ball (rings inside). X, Hunter and Star are
  the same in both. `--classic` keeps the Vectrex shapes (diamond, octagon, "?").
- Screens in the original's look (effects on): its font (Aunchanted Xspace, from the APK, turned
  into single strokes: `src/font_xspace.inc`), its starfield behind every screen, thin white
  panels, the last score top left (lilac) and the best top right (salmon), in game too.
  Initials in three shield badges (green edge, white wall, a ring when a letter is taken); the
  new high score row with a live shield beside it; Game Over panel with Play Again (button) /
  Title (START) as in the original; the service menu with a shield marker on its row.
  `--classic` uses the SDK's Asteroids letters instead.
- Font license: Aunchanted Xspace, (c) Graham Meade 2003, "All rights reserved" in the file;
  FontSpace lists it as freeware ("100% Free" on the author's GemFonts page). No license text
  from the author himself: confirm before a public release.
- Logo as in the original's icon: RELEASE in its font, widely spaced, the A a red triangle with
  a dim red glow; bold (each stroke drawn 9 times on a 3x3 grid, ~11 board units wide).
- Marquee: `release-vp/package/marquee.png`, 1920x360, cut from the user's artwork (full width,
  ~50 px kept above and below the logo and its glow), in the package via `game.ini`.
- Attract mode: title with the base in the middle (6 s) -> the tutorial (16 s, v0.4.2) -> the
  original's instructions, row by row (10 s) -> high scores (5 s) -> demo game (until its death).
- Sound: the original's own clips from the APK (`assets/sounds/`, WAV, ~19 MB): "opening" on
  the title pages and in the demo, "dubChip" in the game, "Spawn" for every spawn, "Death" at
  game over. The original has no other clips: shield release, explosions, star bonus, phase,
  bonus etc. are still made in code (square waves and noise).
- View: positions 1.12x closer than the Vectrex's screen (more would push the bombers' orbit
  off the 4:3 tube's top and bottom); with effects, sprites 1.7x bigger, the base drawn as big
  as its hit reach (SCALE 8), popups bigger. `--classic` keeps the Vectrex sizes.
- Starfield: no swirl with effects (it made people dizzy); the stars only fly outwards.
- START 1 lamp blinks whenever START does something (attract, demo, Title after a game,
  initials); off in the game.
- Verified: builds without warnings (linux preset). Runs at 50 fps in a WSLg window with
  sound, ~200 lines in game, ~740 on the title. The simulator (`tools/sim`) played 10 minutes
  with its bot without a crash; the rendered frames look right.
- **Not yet verified**: the Pi build, a real cabinet/USB-DVG, playing it by hand.

## Known limitations / deviations

- Music and most effects are the Android original's (see above); the Vectrex version's music
  (VTK's Arkos Tracker songs) and AYFX effects are not used.
- Game over: a simpler animation (arms of dots, then GAME OVER) instead of the original's
  7-stage sequence with 3D letters.
- Not ported: the scrolling texts on the title, the options screen (music/SFX/reset, hidden
  start-phase select), the Pac-Man/Worm/Ghost gimmicks (5th/10th/15th destroyed "?" in a row;
  their points and multiplier still count).
- The original's CPU-load adaptation is fixed at "plenty of time" (as the spec suggests).
- Text uses the SDK's fonts instead of the Vectrex letter/digit lists (`abc.i`, `NumberList`).
- Sprites keep the Vectrex proportions and are small on a big monitor (tunable).

## Outstanding tasks

1. Installed on the cabinet on 2026-11-05: version 0.1.2 (original sounds, bigger view, no
   star swirl, lamps) over 0.1.1, save/ kept. Before that: version 0.1.1 (built on the cabinet itself, see
   RESUME.md) in `/persistent/games/release/`, cabinet rebooted, the menu's log lists
   "packages: release 0.1.1 (Other)". 0.1.0 had `manufacturer = Gimogames` and did not show:
   the menu only shows its fixed manufacturer groups (ATARI, CINEMATRONICS, SEGA, MIDWAY,
   VECTORBEAM, CENTURI, EXIDY, ROCK-OLA, OTHER). Not yet played on the vector monitor.
2. Test on the cabinet / USB-DVG; tune `frame_hz` for the board if needed (`[dvg]` in
   `game.ini`).
3. Play-test by hand on the PC (WSLg window or a Windows .exe build).
4. Music: convert the AKS songs to OGG and stream them (`vp_music_play`).
5. Line budget on the board: the instructions page draws ~2700 lines (the text, ~57000 lit
   units), the title ~1150 (the bold logo is drawn 9 times, ~66000 lit units), the game ~250. Check on the USB-DVG; if the page flickers, use fewer/shorter
   lines or a `[dvg] frame_hz` in `game.ini`.
6. Optional: options screen, gimmicks, original game-over sequence, leds.cfg.
