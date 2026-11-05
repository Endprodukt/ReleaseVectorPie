# Release (Vectrex) – Core Gameplay Spec for the VectorPie C Port

Source: `F:\Release Game\VRelease\` (Malban, public domain). All file:line references are relative to that folder.
Scope: main loop / game states, player base + shield, phase system, scoring, coordinate system.
Enemy behaviours are only touched where they matter for these topics.

Conventions used below

- **frame / tick** = one pass of the main loop = 1/50 s (see §1.1).
- **"dec/bpl counter"**: most timers in the game are `dec x ; bpl skip ; x = reload ; <action>`. A counter reloaded with **N fires every N+1 frames**. This is noted as `period = N+1` everywhere it matters. Port these literally.
- **"dec/bne counter"**: fires every N frames (used e.g. for bomber shots, bonus timer).
- **Angles** are stored as *doubled degrees* `0..719` (2 per degree, because they are byte offsets into a table of 2-byte entries). Some spawns produce up to 762 (see §5.3).
- **SCALE** = radial distance from the screen centre in "scale units" (0..255, unsigned byte). See §5.
- Byte arithmetic is 8-bit unsigned unless stated; `cmp + bls/bhi/blo/bhs` = unsigned, `ble/bgt/blt/bge` = signed.

---

## 1. Main loop and game states

### 1.1 Frame rate

- Frame sync is `MY_WAIT_RECAL` (macro.i:82-108): waits for VIA timer T2 to expire, then reloads T2 from `$C83D` = BIOS `Vec_Rfrsh` (VECTREXR.I:56). The game never writes `Vec_Rfrsh`, so the BIOS default (30000 cycles @ 1.5 MHz = 20 ms) applies: **50 Hz**.
- The game assumes 50 Hz everywhere: `INITIAL_SPAWN_RESET_TIMER = 50 ; each second` (inBothBanks1.i:399), bonus timer uses 50 ticks per second (VRelease.asm:1966, objects2.asm:477), `STARLET_SCORE_DELAY = 75 ; at 50Hz 1,5 seconds` (inBothBanks1.i:372).
- If a frame overruns, the Vectrex simply runs slower (no frame skip). The game measures leftover time in `t2_rest` (macro.i:88-91) and sheds load (fewer starfields, shorter score display, squads suppressed). **Port recommendation:** fixed 50 Hz logic tick; treat "CPU headroom" as always sufficient (see §1.6).

### 1.2 State graph

```
power on -> first_init (inBothBanks2.i:6)  -> loads options/highscores (EEPROM/VecFever)
         -> start_cart (VRelease.asm:169) -> initGame -> TITLE

TITLE (title_main1, VRelease.asm:183)
  new press, button 2            -> HIGHSCORE display    (VRelease.asm:285-286)
  new press, button 3            -> OPTIONS              (VRelease.asm:287-288)
  new press, button 4 + VecFever -> exit to VecFever menu (VRelease.asm:289-292)
  new press, button 1 (or 4 w/o VecFever) -> GAME        (game_start, VRelease.asm:293)
  all 4 scroll texts finished    -> DEMO (= GAME with demo_mode=1) (VRelease.asm:225-236, 265-270)

GAME (main, VRelease.asm:302)
  enemy reaches base, no shield bonus, not demo -> GAME OVER (gameOver, VRelease.asm:2095)

GAME OVER (doGameOver, gameover.asm:87)
  explosion animation stages -> 3D "GAME OVER" text + music
  ends when music ends or a button is pressed
  -> test_highscore: if in top 5 -> HIGHSCORE ENTRY (edit_highscore) -> save
  -> TITLE (VRelease.asm:2124-2139)

DEMO
  any button state != 0 -> TITLE (VRelease.asm:343-352)
  low byte of spawn_count >= MAX_OBJECTS-2 (=36) -> HIGHSCORE display (VRelease.asm:355-359)
  player can't die: gameOver just removes the enemy (VRelease.asm:2099-2101)

HIGHSCORE display (display_highscore, highscore.asm:8)
  new button press OR highscore music finished (songOver) -> TITLE (highscore.asm:30-31, 88-101)

OPTIONS (display_options, options.asm:20)
  BACK selected + button -> save options -> TITLE (options.asm:177-187)
```

### 1.3 Button input (`getButtonState`, VRelease.asm:1902-1953)

- Reads PSG register 14 (joystick 1 buttons, bits 0..3 = buttons 1..4, **0 = pressed**). Returned in `A` (raw, low nibble).
- "Any button pressed" is a single bit. `current_button_state = (old << 1) | anyPressed`; returns `B = current_button_state & 3`:
  - `0` = not pressed, was not pressed
  - `1` = pressed now, was not pressed (**new press**)
  - `3` = held
  - `2` = **released** this frame
- Also advances two homebrew RNG bytes `my_random`/`my_random2` (VRelease.asm:1908-1925). The port can use any PRNG; only the value ranges matter (documented where used).
- **All four buttons act the same in game** (any button = shield).
- After returning to title from game/highscore/options, `current/last_button_state` are forced to 3 so a held button does not immediately trigger (VRelease.asm:2136-2138, highscore.asm:97-99, options.asm:184-186).

### 1.4 In-game frame order (`main`, VRelease.asm:302-412)

1. `decode_1ChannelRest` (music; ignore).
2. `shield_delay`: if `>= 0` then `shield_delay--` (VRelease.asm:304-306).
3. `check_stage` – phase advance (§3.4).
4. Wait for frame (`MY_WAIT_RECAL`), sound update.
5. Draw score (`draw_Score_game`) and bonus icon (`drawBonus`) (§4.5).
6. `base_angle += 2` (mod 720) → base rotates **1°/frame (50°/s)**; `starletAngle = 720 - base_angle` (stars rotate the opposite way) (VRelease.asm:323-333).
7. Read buttons; in demo mode replace with demo AI (§1.5).
8. Shield state machine: `B==1 → newShield`, `B==3 → continueShield`, `B==2 → finishShield` (VRelease.asm:395-401). Shield is drawn here.
9. `drawPlayerHome` (base polygon) (VRelease.asm:405).
10. `timerObject` behaviour if set (bonus timer or gimmick sprite) (VRelease.asm:406-408).
11. `check_spawn` (§3.5) – also the load analysis.
12. Run every object's behaviour in linked-list (spawn) order: move + draw; an enemy reaching the base jumps to `gameOver` (VRelease.asm:411-412). The list's terminator jumps back to `main`.

Note: the release kill test (step 8) happens before objects move in step 12.

### 1.5 Demo mode (VRelease.asm:265-270, 341-392)

- Started from title after the 4th scroller finishes: `demo_mode=1`, `demoWaitShieldToActivate=250`, `demoWaitShieldToDeActivate=0`, then normal `game_start`.
- AI: wait 250 frames, then loop: produce `B=1` (press) once, hold (`B=3`) for `(rand & 31) + 1` frames, produce `B=2` (release), idle (`B=0`) for `(rand & 31)+1` frames, repeat.
- Score area shows "DEMO" blinking (intensity toggles 0/$5F every 21 frames, `demo_blink_Counter` reload 20, VRelease.asm:2303-2314).

### 1.6 Load management (optional for port)

`no_spawn_now` (VRelease.asm:446-488) adapts to `t2_rest`: `starfield_max` (10 normally, 8/3/1 under load), `score_display_time` (50 normally, 25 or 12 under load). Squads need `t2_rest > $20` (VRelease.asm:655-657). **Port: always use the "plenty of time" branch: starfield_max = 10, score_display_time = 50, squad CPU check passes.**

### 1.7 Title / options / highscore details (short)

- Title: rotating "RELEASE" letters + scrollers 1..4 (scroll speeds -$80/frame (8.8 fixed), -2, -3, -4) + every 200 frames (`INITIAL_TITLE_PHASE_LENGTH`, title.asm:29) the centre sprite cycles X, Hidden X, Hunter, Bomber, Dragon, Bonus, Starlet (title.asm:19-28, 63-135).
- Options (options.asm): joystick up/down selects `MUSIC(0) / SFX(1) / RESET(2) / BACK(3)`, initial selection 3. Any new button press activates: MUSIC toggles ON/OFF; SFX cycles ON → HALF → OFF → ON (options.asm:384-405); RESET clears options + highscores to defaults and turns music on (options.asm:377-381); BACK saves and returns. Selected entry pulses letter scale 16..26 and uses intensity $7F.
  - **Hidden start-phase select:** with MUSIC selected push the stick up 5 more times (`veryUp >= 5`); a 2-digit number appears and left/right move `initialPhase` through `phaseList` (options.asm:58-78, 215-263). `initialPhase` is not saved. Game start uses it (VRelease.asm:1852-1863).
- Defaults (`defaultValuesEeprom`, VRelease.asm:143-149): options bytes `0,0,0,1,0,0,0,0` → music on, sfx on, `halfnoise=1` (SFX "HALF"). Highscores: MAL 100000, TOM 50000, VTK 30000, ALX 20000, VEC 10000 (BCD `$10,$00,$00` etc.).
- Persistence block: options (8 bytes) + highscore table (5 × 6 + 2 filler) (inBothBanks1.i:186-207). Port: save to a file.

### 1.8 Game over (gameover.asm:87-212, 559-636)

- Sequence: clear sound, explosion SFX, spiral "arms of dots" animation in stages with lengths `STAGE_1..7_LENGTH` = 96, 96, 96, 128, 80, 160, 300 frames (gameover.asm:60-66); entering stage 7 switches to the 3D "GAME OVER" letters + game-over music. Holding all four buttons (raw `A==0`) skips straight to the 3D part (gameover.asm:114-116). The 3D part ends on music end or button state != 0 (gameover.asm:595-601).
- Then `test_highscore` → optional `edit_highscore` → save → `initGame` (title) (VRelease.asm:2124-2139).

---

## 2. Player base and shield

### 2.1 Relevant variables (inBothBanks1.i:149-157 unless noted)

| var | meaning |
|---|---|
| `base_angle` | rotation of base/shield polygon, doubled degrees 0..718, +2 per frame |
| `sided` | polygon side count 5..8 (shared by base and shield) |
| `shieldStart` | outer shield radius, scale units |
| `shieldWidth` | shield thickness (outer − inner), scale units |
| `innerShield` | drawn inner radius (display only) |
| `osc_forth`, `osc_back`, `current_forth_dif`, `current_back_dif` | two animated "oscillator" rings drawn between inner and outer wall (display only) |
| `shieldActive` | 1 while a shield is growing |
| `shield_delay` | cooldown after release |
| `shield_max` | max outer radius, init `SHIELD_MAX_SCALE_INIT = $D0 = 208` (inBothBanks1.i:334, VRelease.asm:1823-1824) |
| from phase block | `shieldWidthGrowth`, `shieldSpeed`, `shieldMinorIncreaseCounterReset`, `shieldMinorIncrease`, `shield_width_adder` (§3.2 fields 1-5) |
| `shieldMinorIncreaseCounter`, `shieldWidthCounter` | dec/bpl counters (inBothBanks1.i:41-42) |

Constants (inBothBanks1.i:328-338): `BASE_SCALE=5`, `SHIELD_START_SCALE=1`, `SHIELD_START_WIDTH=3`, `SHIELD_VARIANCE=10`, `SHIELD_DELAY_TICKS=7`.

### 2.2 Base shape

- Regular `sided`-gon, vertices at `circle[(base_angle + k*ADD) mod 720]`, k=0..n−1, with `ADD = (360/n)*2` integer: 5→144, 6→120, 7→102 (360/7 = 51 truncated → 7-gon does not close exactly; last edge is forced back to the first vertex), 8→90 (`buildRotatedNSidedFigure`, VRelease.asm:1303-1370).
- Drawn at scale `BASE_SCALE = 5` → radius 5 scale units (VRelease.asm:1057-1061).
- `sided` starts at 8 (`initGame`, VRelease.asm:1818-1820), and **every new button press** increments it: 8→5, then 5,6,7,8,5,... (VRelease.asm:796-802). So the first shield of a game is a pentagon.
- Rotation: +1°/frame counter-clockwise (§5.2).
- Death: an enemy whose SCALE after its move is `<= BASE_SCALE+3 = 8` (or underflows) calls `gameOver` (e.g. `xBehaviour`, objects.asm:236-247).

### 2.3 Shield press (`newShield`, VRelease.asm:776-812)

On `B==1` (new press):
```
shieldStart = 1; osc_forth = osc_back = 1
current_forth_dif = shieldSpeed*2
current_back_dif  = shieldSpeed/2          (integer)
shieldWidth = 3
shieldWidthCounter = shieldWidthGrowth
sided = (sided==8) ? 5 : sided+1
currentMaxOffset = maxOffsetValues[sided-5] = {21,16,11,10}   (only used by a disabled check, see 2.6)
-> falls through into continueShield (same frame)
```
Note: `newShield` runs even during the cooldown; only the growth below is blocked.

### 2.4 Shield growth (`continueShield`, VRelease.asm:813-877), every frame while held

```
if shield_delay >= 0: no shield this frame (nothing drawn, shieldActive not set)   ; cooldown
shieldActive = 1
; width
if --shieldWidthCounter < 0:                       ; period = shieldWidthGrowth+1 frames
    shieldWidthCounter = shieldWidthGrowth
    shieldWidth += shield_width_adder
; radius
a = shieldStart + shieldSpeed
if --shieldMinorIncreaseCounter < 0:               ; period = shieldMinorIncreaseCounterReset+1
    shieldMinorIncreaseCounter = shieldMinorIncreaseCounterReset
    a += shieldMinorIncrease
if a > shield_max:                                 ; reached maximum
    current_forth_dif = current_back_dif = shieldSpeed
    if shieldWidth != $80: current_forth_dif = current_back_dif = shieldWidth >> 3   ; see ambiguity A4
    a = shield_max
shieldStart = a
if shieldStart <= shieldWidth: shieldWidth = shieldStart - 1
drawShield
```
- `shieldMinorIncreaseCounter` is **not** reset per shield, only at phase init (`initPhase`, VRelease.asm:2411-2412), so the minor step phase carries over between shields.
- The outer radius keeps being clamped at `shield_max`; the width keeps growing while held (`noShieldGrowthVar` is never set, only cleared – VRelease.asm:792), so a long hold eventually fills the disc down to inner radius 10 (2.5).
- Average outward speed = `shieldSpeed + shieldMinorIncrease/(reset+1)` scale units/frame. Frames from 1 to 208:
  - phases with speed 2, minor 0 (phase0_x…phase1, squad_raid_1): 104 frames ≈ 2.1 s
  - phase2 (2 + 1/4): ≈ 92 frames; phase3 (2 + 1/3): ≈ 89 frames
  - phase4 / squad_raid_2a (3, minor 0): 69 frames
  - phase5 / dragon_raid_2 (3 + 1/3): ≈ 62 frames; phase6 / squad_raid_2 / hunter_raid_2 (3 + 1/2): ≈ 59 frames
  - phase7, phase8, bomber_raid_2, squad_raid_3 (3 + 1): ≈ 52 frames
  - FASTER bonus doubles `shieldSpeed` (§2.8).

### 2.5 Shield drawing (`drawShield`, VRelease.asm:1091-1285)

Four concentric copies of the current `sided`-gon (same vertices as the base, same rotation), drawn at radii:
1. outer: `shieldStart`
2. inner: `innerShield = shieldStart - shieldWidth`; if `shieldStart >= shield_max` and `innerShield <= 9` then `shieldWidth = shieldStart - 10` and inner = 10 (code has a one-frame glitch here, ambiguity A5)
3. `osc_forth`: `+= current_forth_dif` per frame; when it reaches `>= shieldStart` (or overflows 255) it restarts at `shieldStart - shieldWidth` (0 if that is ≥ $F0) – a ring moving outward from inner to outer wall
4. `osc_back`: `-= current_back_dif` per frame (clamped at 0); when `< innerShield` it restarts at `shieldStart` – a ring moving inward
Oscillators are cosmetic. Intensity uses whatever was set before (score $5F).

### 2.6 Release / area kill (`finishShield`, VRelease.asm:879-909; `onShield`, VRelease.asm:977-1054)

On `B==2`:
```
noTimerCheck++           ; skip load measurement this frame
shield_delay = 7         ; SHIELD_DELAY_TICKS
shieldActive = 0
deadEnemies = 0          ; multiplier counter (§4.2)
if shieldStart <= 9: no kill test        ; shield still "inside" the base
else for each object u in list order:
    if onShield(u): exchangeToExplosion(u)
```
Note this runs even if the press happened during cooldown; then `shieldStart` is 1 (from `newShield`) and nothing is killed, but the cooldown restarts.

**Cooldown timing:** release frame sets 7; main decrements at the start of each later frame; growth is allowed only when `shield_delay < 0` → frames F+1..F+7 blocked, growth possible again at F+8 (holding through the cooldown is fine; growth starts then from radius 1).

**`onShield(u)`** – returns hit if the object centre lies inside the polygon ring:
```
if TYPE(u) > TYPE_BOUNDARY: miss          ; only shootable objects: X, HiddenX, Hunter,
                                          ; Bomber, Shot, Dragon, DragonChild, Starlet, Bonus(?)
                                          ; (explosions, scores, starfield, letters, timer excluded)
r     = SCALE(u)
outer = shieldStart + 10   (if shieldStart != 255)          ; SHIELD_VARIANCE
if outer <= r: miss
(rough inner test: effectively disabled – see ambiguity A6)
d     = ANGLE(u) - base_angle;  if d < 0: d += 720
deg   = d >> 1                                                ; 0..359 (can be up to 381, see 5.3)
P     = {5:72, 6:60, 7:51, 8:45}[sided]                       ; degrees per polygon side
idx   = deg mod P                                             ; 0 = at a vertex
tab   = polygon_{sided}_div_correction[idx]
outerC = outer - shrink(outer, tab)
if outerC <= r: miss
inner = shieldStart - shieldWidth - 10;  if that would be < 0: inner = shieldStart - shieldWidth
innerC = inner - shrink(inner, tab)
if innerC >= r: miss
hit
```
So: **hit ⇔ innerC < SCALE < outerC**, all unsigned 8-bit. The ring is widened by 10 units outward and 10 inward to account for object size.

`shrink(v, code)` (collisionRoutines.i:9-208), `v` unsigned 0..255, `>>` = integer shift, each term truncated separately:

| code | name | shrink(v) | ≈ fraction |
|---|---|---|---|
| 0 | NO_DIV | 0 | 0 |
| 1 | DIV_BY32 | v>>5 | 0.031 |
| 2 | DIV_BY16 | v>>4 | 0.0625 |
| 3 | DIV_BY12 | (v>>4) + (v>>6) | 0.078 |
| 4 | DIV_BY11 | (v>>3) − (v>>5) | 0.094 |
| 5 | DIV_BY10 | (v>>3) − (v>>6) | 0.109 |
| 6 | DIV_BY9 | (v>>3) − (v>>7) | 0.117 (unused) |
| 7 | DIV_BY8 | v>>3 | 0.125 |
| 8 | DIV_BY7 | (v>>3) + (v>>6) | 0.141 |
| 9 | DIV_BY6 | (v>>3) + (v>>5) | 0.156 |
| 10 | DIV_BY52 | (v>>2) − (v>>4) − (v>>6) | 0.172 |
| 11 | DIV_BY53 | (v>>2) − (v>>4) | 0.1875 |
| 12 | DIV_BY55 | (v>>2) − (v>>5) − (v>>6) | 0.203 |
| 13 | DIV_BY5 | (v>>2) − (v>>5) | 0.219 |

(The header comments in collisionRoutines.i have some wrong numbers – e.g. div32 says 1/16 – the formulas above are what the code does. In div11/div10/div9 the second term is derived from `(v>>3)` shifted again, i.e. `(v>>3)>>2` = `v>>5` etc.; truncation: compute `t=v>>3; result = t - (t>>2)` to match exactly. Same for the others: div12 = `t=v>>4; t + (t>>2)`; div7 = `t=v>>3; t+(t>>3)`; div6 = `t=v>>3; t+(t>>2)`; div52 = `t=v>>2; t - (t>>2) - (t>>4)`; div53 = `t=v>>2; t-(t>>2)`; div55 = `t=v>>2; t-(t>>3)-(t>>4)`; div5 = `t=v>>2; t-(t>>3)`; div9 = `t=v>>3; t-(t>>4)`; div10 = `t=v>>3; t-(t>>3)`.)

Correction tables (VRelease.asm:941-972), index = degrees from the nearest preceding vertex. Codes as numbers:
```
poly5 (72): 0,0,1,3,3,4,5,7,7,8, 8,9,9,9,9,9,10,10,10,11, 11,11,11,11,12,12,12,12,12,12,
            12,12,12,13,13,13,13,13,12,12, 12,12,12,12,12,12,12,11,11,11, 11,11,10,10,10,9,9,9,9,9,
            9,8,8,7,7,5,4,3,3,1, 0,0
poly6 (60): 0,1,1,1,2,2,2,3,3,3, 5,5,5,5,7,7,8,8,9,9, 9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,
            9,9,8,8,7,7,5,5,5,5, 3,3,3,2,2,2,1,1,1,0
poly7 (52, idx 0..50 used): 0,0,1,1,1,2,2,3,3,3, 3,4,4,4,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5,
            4,5,5,5,5,5,5,5,4,4, 3,3,3,3,3,2,2,1,1,1, 0,0
poly8 (45): 0,1,1,1,2,2,2,3,3,3, 3,4,4,4,4,4,4,4,4,4, 5,5,5,5,4,4,4,4,4,4, 4,4,4,3,3,3,3,3,2,2,
            2,1,1,1,0
```
(Verified line by line against VRelease.asm:944-972. Code numbers as in collisionRoutines.i:180-193.)

**Port alternative (cleaner, nearly identical):** the polygon edge radius at angle φ from a vertex is `R·cos(π/n) / cos(φ − π/n)` for φ ∈ [0, 2π/n). The tables approximate `1 − that ratio`; for faithful feel use the tables, for a cleaner port the analytic form is acceptable. Either way: hit if `inner_poly(θ) < r < outer_poly(θ)` with the ±10 margin applied *before* the polygon correction.

**What a hit does** (`exchangeToExplosion`, VRelease.asm:1379-1605):
- Full-life Dragon: becomes half-life (`dragonBehaviour_half`), "first hit" SFX, **no score, not removed, no multiplier increment** (VRelease.asm:1382-1389).
- Bonus "?" (mystery) destroyed: §4.4.
- Starlet destroyed: `starletCount--`, its bit cleared in `star_active_flag`, star explosion, **no score, no multiplier increment** (VRelease.asm:1431-1456, 1592-1597).
- Everything else: score (§4.2) + explosion object replaces the enemy.
- If `object_count >= MAX_OBJECTS-3 (35)` and `explosionActiveCounter >= 4`, the object is removed without explosion (score is still added) (VRelease.asm:1579-1590).

### 2.7 Other shield facts

- Only one shield at a time; there is no shot/projectile – the release is the only attack.
- Shield does not protect while held: enemies that reach the base while you hold still kill you (they are only removed on release).
- Max outer radius `shield_max` is never changed after `initGame` (=208).

### 2.8 Shield upgrades = mystery bonus (`initBonus`, VRelease.asm:1956-2057)

Triggered when a Bonus "?" object reaches the base (`bonusBehaviour` → `get_bob` → `initBonus`, objects.asm:1402-1408). Not game over.
```
T = bonusActiveTime (phase field 23, seconds; 30 or 15)
bonusCounter = 50*T ticks; bonus_time_1/_0 = decimal digits of T (display)
bonusDestroyedCounter = 0
type = (rand2 & 3) + 1; if type == 4: type = SHIELD      ; SHIELD 50%, EXPAND 25%, FASTER 25%
FASTER (3): shieldSpeed <<= 1; shield_width_adder <<= 1
EXPAND (2): shieldWidthGrowth = 1 (width grows every 2nd frame); shield_width_adder = shieldSpeed
SHIELD (1): no parameter change; protection flag = bonusActiveType
bonusIconMoveScale = 1; bonusIconList = icon for type
timerObject = countdown display inside the base; "?" object removed; BonusGot SFX
```
- **SHIELD (protection):** next time any enemy/shot reaches the base, instead of game over the enemy is removed, `bonusActiveType = 0`, `bonusCounter = 1` → bonus ends next frame (VRelease.asm:2096-2098, 2141-2145). Only one hit absorbed.
- **Timer** (`timerBehaviour`, objects2.asm:460-529): `bonusCounter--` every frame; when it reaches 0 → `endBonus` (objects2.asm:531-563) restores from the current phase record: FASTER → `shieldSpeed = phase[2]`, `shield_width_adder = phase[5]`; EXPAND → `shieldWidthGrowth = phase[1]`, `shield_width_adder = phase[5]`; then `bonusActiveType = 0`. Display digits count down once per 50 frames.
- **Phase change while a bonus is active** re-applies the modifier to the freshly loaded phase values (VRelease.asm:2441-2463).
- Only one mystery/bonus at a time: a Bonus can spawn only while `bonusCounter == 0` (VRelease.asm:594-596); spawning sets `bonusCounter = 1` (objects.asm:1356) and destroying the "?" resets it to 0.
- Bonus "?" movement quirk: initial tick counter from `Bonus_add_delay`, but reloaded from `X_add_delay` (objects.asm:1400); step = `Bonus_addi`.

---

## 3. Phase system

### 3.1 Phase list (`phaseList`, phaseDefinitions.i:3-40)

`phaseList` has **34 entries (index 0..33) followed by a 0 terminator**. The README speaks of "33 phases" (i.e. 33 phase changes after the start) – see ambiguity A1.

| idx | record | | idx | record |
|---|---|---|---|---|
| 0 | phase0_x | | 17 | phase4 |
| 1 | phase0_hx | | 18 | bomber_raid_2 |
| 2 | hunter_raid_1 | | 19 | phase5 |
| 3 | phase1 | | 20 | squad_raid_2 |
| 4 | squad_raid_1 | | 21 | phase5 |
| 5 | phase1 | | 22 | dragon_raid_2 |
| 6 | bomber_raid_1 | | 23 | phase6 |
| 7 | phase2 | | 24 | squad_raid_3 |
| 8 | squad_raid_1 | | 25 | phase6 |
| 9 | phase2 | | 26 | bomber_raid_2 |
| 10 | dragon_raid_1 | | 27 | dragon_raid_2 |
| 11 | phase3 | | 28 | phase7 |
| 12 | squad_raid_2a | | 29 | squad_raid_3 |
| 13 | phase3 | | 30 | phase7 |
| 14 | hunter_raid_2 | | 31 | hunter_raid_2 |
| 15 | phase4 | | 32 | hunter_raid_2 |
| 16 | squad_raid_2 | | 33 | phase8 (next_phase_at = 0 → final, forever) |

### 3.2 Record layout (30 bytes, copied verbatim to `phaseBlockStart..phaseBlockEnd`, inBothBanks1.i:106-144)

| off | RAM variable | meaning (verified against usage) | units |
|---|---|---|---|
| 0 | `spawn_allowed` | allowed spawn bitmask: $01 STAR, $02 X, $04 HIDDEN_X, $08 HUNTER, $10 BOMBER, $20 DRAGON, $40 BONUS, $80 SQUAD (inBothBanks1.i:411-419) | bits |
| 1 | `shieldWidthGrowth` | width grows every (v+1) frames | dec/bpl reload |
| 2 | `shieldSpeed` | outer radius growth per frame | scale/frame |
| 3 | `shieldMinorIncreaseCounterReset` | minor growth every (v+1) frames; also initial counter (VRelease.asm:2411) | dec/bpl reload |
| 4 | `shieldMinorIncrease` | extra radius added on minor tick | scale |
| 5 | `shield_width_adder` | width added per width tick | scale |
| 6 | `shot_add_delay` | bomber shot: move every (v+1) frames | dec/bpl |
| 7 | `shot_addi` | bomber shot: scale step per move | scale |
| 8 | `X_add_delay` | Terrible X: move every (v+1) frames (also reload for Bonus "?") | dec/bpl |
| 9 | `X_addi` | Terrible X scale step | scale |
| 10 | `HX_add_delay` | Hidden X move every (v+1) frames (comment in data says "x move delay") | dec/bpl |
| 11 | `Hunter_addi` | Hunter scale step (Hunter delay is constant 0 → every frame) | scale |
| 12 | `Bomber_add_delay` | bomber orbit: angle step every (v+1) frames (step fixed −2 = −1°) | dec/bpl |
| 13 | `Bomber_addi` | stored into bomber `ANGLE_DELTA` but **unused** by `bomberBehaviour` (objects.asm:1536-1539) | – |
| 14 | `bomber_delay_start` | frames until a bomber's first shot (dec/bne); each later reload −25, min field 20 | frames |
| 15 | `Dragon_Scale_delay` | dragon inward step every (v+1) frames | dec/bpl |
| 16-17 | `Dragon_Angle_addi` (word) | dragon angle step (doubled degrees) | 2×deg |
| 18 | `Dragon_Angle_delay` | dragon angle step every (v+1) frames | dec/bpl |
| 19 | `dragonchild_addi` | freed dragon child: scale step per frame | scale/frame |
| 20 | `minimum_bomb_reload` | floor for bomber shot reload | frames |
| 21 | `Bonus_add_delay` | "?" initial move delay | dec/bpl |
| 22 | `Bonus_addi` | "?" scale step | scale |
| 23 | `bonusActiveTime` | upgrade duration (max 99) | seconds |
| 24 | `phase_spawn_reset` | if ≠ $FF: `spawn_reset = spawn_timer = v` | frames |
| 25 | `phase_spawn_increase_delay` | if ≠ $FF: `spawn_increase_delay = v` | spawns |
| 26 | `phase_min_spawn_reset` | if ≠ $FF: `min_spawn_reset = v` | frames |
| 27 | `phase_max_spawn_increase_delay` | if ≠ $FF: `max_spawn_increase_delay = v` | spawns |
| 28-29 | `next_phase_at` (word) | phase ends after more than this many spawns; 0 = never | spawns |

After the copy, `initPhase` also forces `Hunter_add_delay = 0` (INITIAL_HUNTER_MOVE_DELAY) and `HX_addi = 1` (INITIAL_HX_MOVE_STRENGTH) (VRelease.asm:2437-2440). The comments in phaseDefinitions.i are often stale (e.g. "; 9" next to bonus time 30); the values below are the assembled values.

### 3.3 Decoded phase records

Constants resolved from inBothBanks1.i:328-402. Allowed mask hex. `-1` = $FF = keep current value.

| record | 0 allow | 1 wGr | 2 spd | 3 mRst | 4 mInc | 5 wAdd | 6 shDly | 7 shStp | 8 xDly | 9 xStp | 10 hxDly | 11 huStp | 12 boAngDly | 13 boAddi | 14 boShot | 15 drScDly | 16 drAng | 18 drAngDly | 19 dcStp | 20 minBomb | 21 bnDly | 22 bnStp | 23 bnTime | 24 spReset | 25 spIncDly | 26 spMin | 27 spMaxInc | 28 next |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| phase0_x (pD:43) | $47 STAR X HX BONUS | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 50 | 1 | 15 | 10 | 20 |
| phase0_hx (pD:74) | $4B STAR X HUNTER BONUS | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 50 | 1 | 15 | 10 | 20 |
| hunter_raid_1 (pD:104) | $08 HUNTER | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 10 | 1 | 15 | 10 | 10 |
| bomber_raid_1 (pD:134) | $14 BOMBER HX | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 5 | 1 | 15 | 10 | 20 |
| dragon_raid_1 (pD:164) | $20 DRAGON | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 10 | 1 | 15 | 10 | 10 |
| phase1 (pD:195) | $FF all | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 3 | 1 | 1 | 1 | 200 | 8 | 2 | 1 | 1 | 50 | 0 | 2 | 30 | 50 | 1 | 15 | 10 | 50 |
| phase2 (pD:227) | $FF | 4 | 2 | 3 | 1 | 1 | 1 | 1 | 1 | 1 | 2 | 1 | 1 | 1 | 20 | 5 | 2 | 0 | 1 | 200 | 0 | 2 | 30 | 40 | 1 | 15 | 9 | 50 |
| phase3 (pD:256) | $FF | 4 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 200 | 5 | 4 | 0 | 1 | 50 | 0 | 2 | 30 | 35 | 1 | 15 | 8 | 50 |
| phase4 (pD:290) | $FF | 3 | 3 | 20 | 0 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 25 | 5 | 4 | 0 | 1 | 50 | 0 | 2 | 30 | 30 | 1 | 15 | 7 | 100 |
| phase5 (pD:319) | $FF | 3 | 3 | 2 | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 25 | 5 | 4 | 0 | 1 | 50 | 0 | 2 | 30 | 25 | 1 | 25 | 1 | 100 |
| squad_raid_1 (pD:348) | $80 SQUAD | 4 | 2 | 2 | 0 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 25 | 5 | 4 | 0 | 1 | 50 | 0 | 2 | 30 | 25 | 1 | 25 | 1 | 24 |
| phase6 (pD:377) | $FF | 3 | 3 | 1 | 1 | 2 | 3 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 30 | 1 | 25 | 5 | 50 |
| squad_raid_2 (pD:406) | $80 | 3 | 3 | 1 | 1 | 2 | 3 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 25 | 1 | 25 | 1 | 40 |
| phase7 (pD:435) | $FF | 3 | 3 | 0 | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | -1 | -1 | 20 | -1 | 100 |
| phase8 (pD:464) | $FF | 3 | 3 | 0 | 1 | 3 | 0 | 2 | 0 | 1 | 1 | 2 | 0 | 2 | 40 | 1 | 6 | 0 | 2 | 10 | 0 | 3 | 15 | 50 | 1 | 15 | 20 | 0 |
| hunter_raid_2 (pD:494) | $08 | 3 | 3 | 1 | 1 | 2 | 3 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 10 | 1 | 15 | 10 | 10 |
| bomber_raid_2 (pD:524) | $14 | 3 | 3 | 0 | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 10 | 1 | 15 | 10 | 20 |
| dragon_raid_2 (pD:554) | $20 | 3 | 3 | 2 | 1 | 2 | 0 | 2 | 0 | 1 | 1 | 2 | 0 | 2 | 40 | 1 | 6 | 0 | 2 | 10 | 0 | 3 | 15 | 10 | 1 | 15 | 10 | 10 |
| squad_raid_3 (pD:583) | $80 | 3 | 3 | 0 | 1 | 2 | 1 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 25 | 1 | 25 | 1 | 40 |
| squad_raid_2a (pD:612) | $80 | 3 | 3 | 20 | 0 | 2 | 3 | 1 | 1 | 1 | 1 | 2 | 0 | 1 | 25 | 4 | 4 | 0 | 1 | 25 | 0 | 3 | 30 | 25 | 1 | 25 | 1 | 40 |

(pD = phaseDefinitions.i line of the label. Field 16-17 is a word; "drAng" column = its value.)

Port as a C table of 19 records + 34-entry index list. Observations:
- Raids restrict spawn types (hunter_raid = only hunters, bomber_raid = bombers+hidden X, dragon_raid = only dragons, squad_raid = only squads) and set a short `spawn_reset` (5..25), so they are bursts.
- phase5/squads set `min_spawn_reset = 25` and `max_spawn_increase_delay = 1`; phase7 keeps the accumulated spawn rate (`-1`) but lowers the floor to 20; phase8 resets the rate to 50 and lets it climb slowly to the floor 15 (`max_spawn_increase_delay = 20`).
- Bomber `bomber_delay_start = 20` (phase2) followed by reload `20 − 25` wraps to 251 (unsigned) which is > `minimum_bomb_reload` 200 → second shot after 251 frames. Faithful-port quirk (objects.asm:1563-1570).

### 3.4 Phase advance (`check_stage`, VRelease.asm:2383-2398) and jingle

Called once per game frame (VRelease.asm:308):
```
if next_phase_at == 0: return
if next_phase_at - spawn_count >= 0: return          ; i.e. advance when spawn_count > next_phase_at
spawn_count = 0
rec = phaseList[phase_count + 1]; if rec == 0: return  ; (end of list)
phase_count++
play_sfx(phaseChange_Sound)                            ; the "phase jingle" (sound.i:168-170, data sound.i:482)
initPhase(rec)
```
`initPhase` (VRelease.asm:2400-2463): sets `currentPhaseData`, `shieldMinorIncreaseCounter = rec[3]`, applies fields 24-27 unless $FF, copies all 30 bytes, sets `Hunter_add_delay=0`, `HX_addi=1`, re-applies an active FASTER/EXPAND bonus.
The very first phase is applied by `initGame` without jingle (VRelease.asm:1852-1863), starting at `initialPhase` (default index 0; options cheat can change it).

So each phase lasts `next_phase_at + 1` counted spawns (a squad counts 6 and can overshoot; the excess is discarded by the reset to 0).

### 3.5 Spawning and automatic spawn-rate increase (`check_spawn`, VRelease.asm:526-605)

State (initGame, VRelease.asm:1829-1848): `spawn_reset = spawn_timer = 50`, `spawnRest_Counter = spawn_increase_delay = 1`, `min_spawn_reset = 15` (FASTEST_SPAWN_RATE), `max_spawn_increase_delay = 10`; then overridden by the start phase's fields 24-27.

Every frame:
```
if --spawn_timer >= 0: (no spawn this frame) return
a = spawn_reset
if --spawnRest_Counter < 0:
    b = spawn_increase_delay + 1
    if b >= max_spawn_increase_delay: b = max_spawn_increase_delay
    spawn_increase_delay = spawnRest_Counter = b
    a = a - 1;  if a <= min_spawn_reset (signed): a = min_spawn_reset
spawn_reset = spawn_timer = a                 ; next spawn attempt in a+1 frames
spawn_count++
r = rand                                       ; my_random, 0..255
if spawn_allowed == $80 (only SQUAD): -> squad
r <= 15  : Starlet  (needs starletCount < 3, else "not allowed" w/o timer clear)
r <= 80  : Terrible X
r <= 130 : Hidden X
r <= 170 : Hunter
r <= 200 : Bomber
r <= 230 : Dragon
r <= 240 : Squad
r <= 252 : Bonus "?" (needs bonusCounter == 0, else "not allowed" w/o timer clear)
else     : nothing (spawn_count--)
```
- Type chosen but its bit not in `spawn_allowed`: `spawn_timer = 0` (→ new attempt **next frame**, which also re-runs the rate-decrease logic) and `spawn_count--` (e.g. objects.asm:565-568). This means restrictive raids burn through `spawnRest_Counter` quickly and the rate decrease accelerates – port faithfully.
- `spawn_count--` cases ("returnSpawnNotAllowed", VRelease.asm:600-605): disallowed type, star limit, bonus already active, r > 252, squad conditions failed.
- No free object slot: the spawn silently fails but still counts.
- Squad (VRelease.asm:649-762): if not demo, requires `object_count <= 25` and CPU headroom (§1.6); if the SQUAD bit is not allowed → `spawn_timer = 0`, `spawn_count--`. Then by `rand2`: ≤50 Hunter squad, ≤100 Hidden X squad, ≤150 X squad, ≤200 Bomber squad, else nothing. A squad = 6 objects of that type at the same SCALE, angles `a0, a0+120, …` (+60° each, wrap 720), `spawn_count += 5` extra (6 total). Squad members bypass the per-type allow bits.
- Effective sequence: the interval between spawn attempts is `spawn_reset+1` frames. `spawn_reset` drops by 1 whenever `spawnRest_Counter` underflows; the reload value (`spawn_increase_delay`) grows by 1 each time, capped at `max_spawn_increase_delay`; `spawn_reset` is floored at `min_spawn_reset`. Worked example with initGame defaults (reset 50, counter = delay = 1, max 10, min 15), counting spawn *attempts* that passed the timer: attempt 1 counter 1→0 (no change); attempt 2 → −1: delay=2, reset 49; attempt 5: delay 3, reset 48; attempt 9: 47; 14: 46; 20: 45; 27: 44; 35: 43; 44: 42; 54: 41 (delay reaches the cap 10); then every 11 attempts (65, 76, …) one more step until the floor 15. With phase values `spIncDly=1, spMaxInc=1` (phase5, squads) the rate drops by 1 every 2 attempts.
- `spawnRest_Counter` itself is never reset by a phase change (only `spawn_increase_delay` is), so the first decrement after a phase change happens when the old counter runs out.

---

## 4. Scoring

### 4.1 Representation

- Player score: 6 decimal digits, one byte per digit, MSB first (`player_score_5..0`, inBothBanks1.i:66-72), "CSA" format (VRelease.asm:13-27).
- `add_score_x` (VRelease.asm:2162-2220) adds a 3-digit CSA number (hundreds, tens, ones) with carry. Overflow beyond 999999 is not clamped cleanly (top digit stays 9, lower digits wrap) – **port: clamp at 999999**.
- Highscores are stored as 3-byte packed BCD (`csa_to_bcd` / `bcd_to_csa`, VRelease.asm:2468-2501).

### 4.2 Points per kill (`exchangeToExplosion` + `buildscoreXX`, VRelease.asm:1461-1706; macro.i:384-451)

Multiplier from `deadEnemies` (reset to 0 at each release, VRelease.asm:886), read **before** the increment for the current kill: 0 → ×1, 1 → ×2, ≥2 → ×3.

| object | base | ×2 | ×3 | `deadEnemies` increment | file:line |
|---|---|---|---|---|---|
| Terrible X | 10 | 20 | 30 | +1 | VRelease.asm:1490-1497 |
| Hidden X | 15 | 30 | 45 | **+2** (A3) | 1538-1542, 1561-1569 |
| Hunter | 15 | 30 | 45 | +1 | 1530-1536 |
| Bomber | 18 | 36 | 54 | +1 | 1499-1505 |
| Dragon (2nd hit) | 18 | 36 | 54 | +1; frees its children (they rush the base) | 1508-1528 |
| Dragon (1st hit) | 0 | – | – | 0 | 1382-1389 |
| Dragon child | 10 | 20 | 30 | **+2** | 1545-1559 |
| Bomber shot | 10 | 20 | 30 | +1 | 1479-1488 |
| any other shootable type | 10 | 20 | 30 | +2 | 1476-1477 |
| Starlet | 0 | – | – | 0 | 1592-1597 |
| Bonus "?" | 505 × n (4.4) | – | – | +1 | 1393-1426 |

Order of kills = object list order (spawn order). A floating score sprite (`score10/15/18/20/30/36/45/54`, data.i:69-162) is spawned at the victim's position (scale 3, intensity $5F), drifts outward +1 scale every 2nd frame, lives `score_display_time` = 50 frames, hidden if its SCALE ≥ `shield_max` (`scoreBehaviour`, objects2.asm:365-404). Score is added even if no object slot is free for the sprite.

### 4.3 Star (starlet) bonus

- Spawn (`spawnStarlet`, objects.asm:864-971): spawn roll r ≤ 15, `ALLOW_STAR`, and `starletCount < 3`. Each star takes the first free id 0..2 (`star_active_flag` bits). Fixed radius: id0 = `STAR_SCALE` 40, id1 = 25, id2 = 50 (objects.asm:897-920). Start angle offset (doubled deg): id0 `2·(rand2>>3)` = 0..62, id1 `2·((rand2>>3)+150)` = 300..362, id2 `2·((rand2>>3)+90)` = 180..242; position = `circle[(offset + starletAngle) mod 720]` → stars orbit clockwise at 1°/frame together (objects.asm:691-705).
- Star score: per-star 3-digit CSA counter `star_N_score` (bytes `_3,_2,_1`; `_0` unused) set to **2** at spawn (objects.asm:961-969; `STARLET_START_SCORE=2`).
- Payout (objects.asm:719-770): `SCORE_COUNTER` dec/bpl, reload `STARLET_SCORE_DELAY = 75` → **every 76 frames (1.52 s)**, first one 76 frames after spawn: counter `+= 2`, capped at **999** (once 998+2 overflows the hundreds digit, all digits are set to 9); then `buildscoreX` adds the counter to the player score and spawns a 3-digit floating number (leading zeros suppressed) (VRelease.asm:1720-1779).
- So payouts are 4, 6, 8, …, 998, 999, 999, … – total after k payouts = k² + 3k (until the cap). Matches README "4 … 999".
- No points for destroying a star; it is just lost (`starletCount--`).

### 4.4 Mystery "?" bonus

- Spawn: roll 241..252, `ALLOW_BONUS`, `bonusCounter == 0` (§2.8).
- Reaches base → upgrade (§2.8), no points.
- Destroyed by shield (VRelease.asm:1393-1426): `bonusCounter = 0`, `bonusDestroyedCounter++`, then **adds 505 points `bonusDestroyedCounter` times** (CSA `[5,0,5]`, see A2). `bonusDestroyedCounter` is reset to 0 by game start and by collecting a bonus. So consecutive destroyed mysteries pay 505, 1010, 1515, …
- Easter egg: when `bonusDestroyedCounter` (reduced by 15 while ≥ 20) equals 5 / 10 / 15 → Pacman / Worm / Ghost gimmick sprite walks through the base area (`initPacman/initWorm/initGhost`, VRelease.asm:2059-2091; `gimmikBehaviour`, objects2.asm:773). Cosmetic. Triggers at counts 5,10,15,20,25,30,…

### 4.5 Score display

- In game (`draw_Score`, VRelease.asm:2231-2280): move to (y=$7F, x=$10) at scale $7F, then 6 digits at scale 16 (`SCORE_SIZE`), intensity $5F, each digit vector list leaves the beam at the next digit position. All 6 digits always shown (leading zeros drawn).
- Bonus icon (`drawBonus`, VRelease.asm:2350-2373): shown while `bonusCounter > 1`; position (y = s, x = −30) at scale s, where `s = bonusIconMoveScale` goes 1,3,5,…,127 (+2/frame) → slides from the centre to the top-left of the score; icon drawn at scale 4.
- Bonus countdown inside the base (`exchangeToBonusTimer`, VRelease.asm:2030-2057; `timerBehaviour`, objects2.asm:460-529): scale 6, offset (−32,−80) for 2 digits / (−32,−32) for 1 digit; digits at scale 3, intensity $5F.

### 4.6 Highscore table

- 5 entries × {NAME[3] ASCII 'A'..'Z', BCD_SCORE[3]} (inBothBanks1.i:179-206), descending.
- `test_highscore` (VRelease.asm:2566-2603): from entry 4 up to entry 0, while the player score is **strictly greater** than the entry, the entry is shifted down one place and the player's score is written into it (keeping that entry's old name as the initial suggestion). Returns place 0..4 or $FF.
- `edit_highscore` (highscore.asm:112-318): edit 3 letters of the new entry. Joystick left/right (edge-triggered) moves the cursor (letters 0..2), up = next letter (Z→A wraps), down = previous (A→Z). Current letter blinks: visible/blank toggling every 16 frames (`BLINK_LENGTH = 50/3`). A new button press confirms. Then the table is saved.
- Display (`display_highscore`, highscore.asm:8-107): names at `HIGH_SCORE_ZERO_POS = $7090` (y=$70, x=$90 at the game positioning scale `SCALE_FACTOR_GAME = $80-$20 = $60`), letter step 20, scores below; 5 rows; with highscore music.

---

## 5. Coordinate system

### 5.1 Vectrex units

- Beam moves = DAC value (signed byte −128..127) × scale (VIA T1 count, 0..255). A move/position of `(y, x)` drawn at scale `S` lands at `(y·S, x·S)` "integrator units" relative to the screen centre (the game re-zeros the beam before every object).
- The game itself puts the score at `(127, 16)·127`, i.e. the **top edge ≈ 127·127 = 16129 units** (VRelease.asm:2234). Use **16129 as the half-height**.
- x and y units are physically isotropic (circles are drawn round from one table). The Vectrex screen is portrait (~4:5, width:height), so the visible half-width is roughly 0.8 × 16129 ≈ 13000 units. Exact visible extent depends on the monitor calibration (ambiguity A7).
- **Recommended normalisation:** `nx = X_units / 16129`, `ny = Y_units / 16129` (+y up, +x right), i.e. visible area ≈ x ∈ [−0.8, 0.8], y ∈ [−1, 1]. Keep game logic in the original radial units and only convert when drawing.

### 5.2 Polar positions used by the game

Every gameplay object, the base and the shield are positioned in polar form:
```
(y, x) = circle[ANGLE]           ; circle radius 108, data.i:897
pos_units = (y, x) * SCALE       ; SCALE = object's radial distance byte
=> radius_units = 108 * SCALE ; normalised radius ≈ 108*SCALE/16129 = SCALE / 149.3
```
- `circle` table (data.i:897-1284): 360 entries for 0°..359° plus 23 overflow entries repeating 0°..22°; entry `k` = (round(108·cos k°), round(−108·sin k°)) as (y, x). **Angle 0 points up; increasing angle turns counter-clockwise** (90° → x = −108, left).
- Index: byte offset = `ANGLE` (doubled degrees), so `deg = ANGLE / 2`. Port: `y = 108·cos(ANGLE·π/360)`, `x = −108·sin(ANGLE·π/360)`, or copy the table.
- Key radii (scale units → normalised by /149.3):

| what | SCALE | ≈ normalised radius |
|---|---|---|
| base polygon (`BASE_SCALE`) | 5 | 0.033 |
| death threshold (≤ BASE_SCALE+3) | 8 | 0.054 |
| shield start | 1 | 0.007 |
| shield "too small to kill" (≤ 9) | 9 | 0.060 |
| starlets | 25 / 40 / 50 | 0.17 / 0.27 / 0.33 |
| bomber spawn | 100..131 (`rand2&31 + 100`) | 0.67..0.88 |
| enemy spawn (`spawn_max = SPAWN_MAX_SCALE_INIT = $C8`) | 200 | 1.34 (just off-screen top/bottom; further off left/right) |
| shield max (`shield_max = $D0`) | 208 | 1.39 (vertex); pentagon edge ≈ 1.13 |
| music-decode threshold in behaviours (irrelevant) | ≥ $80 | – |

- Object sprite sizes: sprites are drawn at their own small scale after the move (e.g. X: scale 6, intensity $5F, `xBehaviour` objects.asm:276-280; starlets/scores scale 3; bomber shots and dragon children scale 6, intensity $7F). Sprite vertex values are up to ~±$7F, so a scale-6 sprite spans about ±760 units (±0.047 normalised).
- Movement = SCALE decreases by `step` every `delay+1` frames (inward), angle changes for orbiting types. Speed in normalised units/frame = `step / (delay+1) / 149.3`.

### 5.3 Spawn angles

- `ANGLE_0_762` (objects.asm:593-603): `ANGLE = 6·(rand2 & 127)` → 0..762 doubled degrees in steps of 6 (3°). Values 720..762 use the overflow part of the circle table (0°..21°), so the top sector is slightly over-represented. Port: same formula with a 383-entry table, or `ANGLE mod 720` for position (then the shield angle diff in §2.6 must also use the unreduced value to stay faithful – the original does not reduce it).
- Hunter: `ANGLE = 32·((6·(r mod 120)) >> 5)` → multiples of 32 (16°), so the sprite matches one of 24 rotated hunter lists (objects.asm:640-668).
- Bomber: `ANGLE = 6·(mixed random & 127)` (objects.asm:1100-1115).

---

## 6. Ambiguities / quirks to decide on

- **A1 Phase count.** `phaseList` has 34 entries (index 0..33); README says 33 phases. Likely "33 phase changes". Port the list as-is.
- **A2 Mystery destroy score.** Code builds CSA `[5,0,5]` (`ldd #$0500 ; std ,x ; sta 2,x`, VRelease.asm:1398-1401) = **505** points per unit; probably intended 500. Faithful = 505.
- **A3 Multiplier double increment.** Hidden X, dragon child and the generic fallback increment `deadEnemies` twice (`continue_ete` falls into `entry_otherexplosion_type`, VRelease.asm:1561-1569), so a following kill gets ×3 sooner. Faithful port keeps it.
- **A4 Oscillator speed at max size.** `cmpa #$80 ; ble` is a signed compare (VRelease.asm:858-864): oscillator speed becomes `shieldWidth>>3` for every width except exactly 128; comment intends "only if width > 128". Cosmetic only.
- **A5 innerShield at max.** When at max size with inner ≤ 9 the code stores `shieldStart-10` into `innerShield` for one frame (falls through to `noInnerChange`, VRelease.asm:1111-1119), next frame inner = 10. Cosmetic; port as "inner radius floor 10 when at max".
- **A6 Rough inner test disabled.** In `onShield` a `clra` (VRelease.asm:994) zeroes the rough inner bound, so `currentMaxOffset`/`maxOffsetValues` have no effect. Only the precise test matters.
- **A7 Visible screen extent.** Half-height 16129 units is inferred from the score position; half-width depends on the physical monitor (≈0.8 × height). Choose a letterbox for VectorPie.
- **A8 Bonus "?" speed.** Initial delay from `Bonus_add_delay` (0) but reloaded from `X_add_delay` (objects.asm:1400). Bomber `Bomber_addi` (phase field 13) is unused; bomber always orbits −2 (−1°) per step.
- **A9 Bomber reload underflow.** `bomber_delay_start < 25` + reduction 25 wraps (unsigned) → very long second reload (phase2: 251 frames).
- **A10 Load-dependent behaviour.** Squads only spawn with CPU headroom; starfield count and score display time shrink under load (§1.6). Port assumes headroom.
- **A11 Score overflow above 999999** is not clamped cleanly in the original (VRelease.asm:2218-2220); clamp in the port.
- **A12 Demo exit** compares only the low byte of `spawn_count` with 36 (VRelease.asm:355-359); since `spawn_count` resets at each phase change this effectively means "36 spawns within one phase".
