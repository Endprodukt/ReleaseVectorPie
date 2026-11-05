# Release (Vectrex) — Game Objects Specification

Source: `F:\Release Game\VRelease\` (Malban, 2017, public domain). This document specifies every in-game
object of the 6809 original precisely enough to re-implement it in C for the VectorPie SDK.
Primary sources: `objects.asm` (enemies, spawn routines, list management), `objects2.asm` (explosions,
score popups, timer/gimmick, starfield), `VRelease.asm` (spawn selection, kill handling, scoring, bonus),
`inBothBanks1.i` (RAM layout + constants), `phaseDefinitions.i` (per-phase tuning), `data.i` (vector lists).
Citations are `file:line`. Shield growth/drawing and the phase/difficulty system are only covered where an
object depends on them.

---

## 0. Conventions and units

### 0.1 Time
* One game tick = one frame = one pass of `main` (`VRelease.asm:302`), synchronised by `MY_WAIT_RECAL`
  (`macro.i:82`) to the BIOS refresh timer (30000 cycles) = **50 Hz**. All "delay" values below are in
  frames.
* Most counters use the idiom `dec counter; bpl skip; counter = reload; act` (comment "test for dec -> bpl").
  With reload value `D`, the action happens **every `D+1` frames** (D=0 → every frame, D=1 → every 2nd
  frame). Where a counter uses `bne` instead (Bomber shots) it is noted.
* `RecalCounter` (16-bit, `RecalCounterHi:RecalCounterLow`, `inBothBanks1.i:93`) is incremented once per
  frame in `MY_WAIT_RECAL`. Several animations test `RecalCounterLow & 1` ("every second frame").

### 0.2 Polar object position: ANGLE + SCALE
Every gameplay object lives on a circle around the base (screen centre):
* `ANGLE` is a 16-bit **byte offset into the `circle` table** = degrees × 2. Valid 0..718 (wrap at 720);
  spawn code can produce up to 762 (= 381°) which the table supports (see 0.3).
* `circle` (`data.i:897`) holds `(y, x)` byte pairs: `y = trunc(108·cos θ)`, `x = trunc(−108·sin θ)`
  (truncation toward zero, verified for all 383 entries), θ = 0°..359° then 0°..22° repeated (383 entries).
  θ = 0° is straight **up**, increasing θ goes **counter-clockwise** (90° = left, X = −108).
* `Y_POS, X_POS` = `circle[ANGLE]` (cached copy, radius 108).
* `SCALE` (unsigned byte) is the radial distance: the object is drawn with a BIOS-style "move to
  (Y_POS, X_POS) with timer scale SCALE". In **raw Vectrex integrator units** (DAC value × scale ticks):

  `pos_raw = (X_POS · SCALE, Y_POS · SCALE)`, i.e. radius ≈ `108 · SCALE` raw units.

* Sprites are then drawn relative to that point at a fixed **sprite scale** `S` (usually 6), so a vertex
  `(vx, vy)` from a vector list lands at `pos_raw + (vx·S, vy·S)`.
* Normalisation hint for the port: on a real Vectrex a DAC value of 127 at scale 127 (= 16129 raw) is
  roughly the screen edge. So `screen = raw / 16129` gives approximately ±1.0 at the edge (the tube is
  portrait; X extends a bit less than Y in practice). `spawn_max = 200` → radius 21600 raw, i.e. enemies
  spawn **off-screen** and fly in.

### 0.3 Angle helpers used by spawners
* `ANGLE_0_762` (`objects.asm:593`): `r = my_random2 & 0x7F; ANGLE = r * 6` → 0..762 in steps of 6
  (3°). r ≥ 120 gives 720..762 (= 0°..21° again; the circle table has overflow entries for this, so
  angles 0°..21° are twice as likely). ANGLE is **not** normalised to <720 here.
* `STORE_POS_FROM_ANGLE` (`objects.asm:605`): `Y_POS, X_POS = circle[ANGLE]`.

### 0.4 Draw-update ordering (1-frame lag)
Each behaviour routine pulls `Y_POS, SCALE, CURRENT_LIST` and starts the move **before** it updates the
object. Position/scale/animation-frame changes therefore become visible on the **next** frame. A port
that updates-then-draws is one frame "ahead" — harmless, but keep it in mind for exact timings.

### 0.5 Vector list formats (`drawSubRoutines.i`, `unloop.i`)
| format | used by | layout |
|---|---|---|
| **mode list** (`myDraw_VL_mode*`) | X, Hidden X, Dragon child, "?", score popups, HUD icons, digits | triplets `mode, dy, dx`; `mode = $00` move (beam off), `mode ≥ $80` draw, `$01` (any positive non-zero) = end. Relative coordinates. |
| **MVLC unloop** (`entry_optimized_draw_mvlc_unloop`, `drawSubRoutines.i:338`) | Hunter, Bomber, Dragon, Starlet, base polygon | `DW (12-(n+1))*ONE_LINE_LENGTH` (jump offset encoding the draw count `n`), then `n+1` relative pairs `dy, dx`: the **first pair is a move**, the next `n` are draws. **Forces sprite scale 6** (`LDD #$8006; STB VIA_t1_cnt_lo`). |
| **VLC** (`my_drawVLC_inner`, `drawSubRoutines.i:358`) | Bomber shot, gimmick sprites | same header, `n+1` pairs, **all pairs are draws** (no initial move), starting at the object position. Uses whatever scale was latched before (6 for shots, 3 for gimmicks). |

All coordinates in this document are converted to **absolute `(x, y)`** (note: the source lists are
`(y, x)`), relative to the object position, in sprite units (multiply by the sprite scale for raw units).

### 0.6 Intensities
Vectrex intensity 0..$7F. Values used: `$7F` bright, `$5F` normal, `$4F` dim, `$2F` starfield max.

---

## 1. Object pool and list management

### 1.1 Memory model (`inBothBanks1.i:239-258`)
* `ObjectStruct` = 16 bytes: `Y_POS(1) SCALE(1) CURRENT_LIST(2) BEHAVIOUR(2) X_POS(1) ANGLE(2)
  NEXT_OBJECT(2) + 5 type-specific bytes`. `TYPE` aliases the **high byte of `BEHAVIOUR`**: the behaviour
  routines are placed on distinct 256-byte pages so the page number is the type id (`objects.asm:3-5`,
  `inBothBanks1.i:341-365`). Port: use an explicit `enum type` + function pointer / switch.
* Pool: `MAX_OBJECTS = 38` slots (`inBothBanks1.i:250`). A separate static `timerObject`
  (`inBothBanks1.i:258`) is **not** in the pool (bonus timer / gimmick).
* Two singly-linked lists: *active* (`list_objects_head/tail`) and *free* (`list_empty_head`).
  The terminating "next" of the active list is a ROM pointer (`PC_MAIN`, positive) which doubles as the
  return into `main` (the list is executed by `pulu d,x,pc` chaining).
* `object_count` = number of active slots (includes starfields, explosions, popups).

### 1.2 `newObject` (`objects.asm:458`)
Takes the head of the free list, **appends it at the tail** of the active list, `object_count++`.
Fails (no-op) if the free list is empty — every spawner silently aborts then. Because new objects are
appended at the tail and the object loop runs after spawning (`main` order below), **a newly spawned
object is updated and drawn in the same frame**.

### 1.3 Removal
* `removeObject` (`objects.asm:283`) / `removeObject_rts` (`objects.asm:413`): unlink (linear search for
  the predecessor), push slot to the free-list head, `object_count--`.
* "Anti-glitch" deferred removal (explosions, popups): the behaviour sets the slot's `SCALE = 0`; on the
  next frame the behaviour sees scale 0 and calls `removeObject` without drawing. So such objects live
  one extra (invisible) frame.

### 1.4 Per-frame order in `main` (`VRelease.asm:302-412`)
1. music decode; `shield_delay--` (if ≥0); `check_stage` (phase advance, §9.3); wait for frame.
2. draw HUD score, `drawBonus` (HUD bonus icon, §7.6).
3. `base_angle += 2` (mod 720) → base rotates CCW 1°/frame (50°/s); `starletAngle = 720 − base_angle`.
4. `getButtonState` → **RNG update** (§9.1) + button edge → `newShield` / `continueShield` /
   `finishShield` (the shield kill pass, §8, happens here).
5. draw base (`drawPlayerHome`, also rebuilds `rotList`, used by explosions).
6. `timerObject` behaviour if set (bonus timer or gimmick, §7.4/§7.5).
7. `check_spawn` (§9.2) — may spawn objects (appended to the list) or, on non-spawn frames, do the
   load/starfield management (§9.4).
8. Run every active object's behaviour in list order (update + draw).

---

## 2. Common enemy rules

### 2.1 Reaching the base (= "collision radius with base")
All inward-moving enemies use the same test after decreasing their scale
(`objects.asm:238-248` and copies):
```
newScale = SCALE - delta            (8-bit, unsigned)
if (borrow || newScale <= BASE_SCALE + 3)   // BASE_SCALE = 5  → threshold 8
    gameOver();
else SCALE = newScale;
```
So an enemy hits the base when its radial scale would drop to **≤ 8**, i.e. radius ≤ 8·108 = **864 raw
units** (base polygon radius = 108·5 = 540 raw; sprite half-size ≈ 380 raw). The test only runs on ticks
where the object actually moves. Dragon (step 1) and free dragon children use the same threshold.

### 2.2 `gameOver` (`VRelease.asm:2095`)
* If the **Shield bonus** is active (`bonusActiveType == BONUS_TYPE_SHIELD`): the base survives once —
  `bonusActiveType = 0`, `bonusCounter = 1` (the timer hits 0 next frame and the bonus ends normally),
  the offending object is removed (no explosion, no score). Play continues.
* Demo mode: the object is simply removed.
* Otherwise: real game over (game-over animation, highscore). The `INVINCIBLE` build flag also just
  removes the object.

### 2.3 Spawn radius
`spawn_max` is set once to `SPAWN_MAX_SCALE_INIT = $C8 = 200` (`VRelease.asm:1821`) and never changed.
X, Hidden X, Hunter and "?" spawn at `SCALE = 200`.

---

## 3. Enemies

Per-phase speed variables (RAM, copied from the phase record, §9.3) are named like `X_add_delay`.
Values for the first phase (`phase0_x`) are given as an example; see the phase table in §9.3.

### 3.1 Terrible X (`TYPE_X`) — the "+"
| item | value / source |
|---|---|
| spawn | `spawnX` `objects.asm:564`; requires `ALLOW_X`; sound `SpawnX_Sound` |
| start | `SCALE = spawn_max (200)`; `ANGLE = ANGLE_0_762`; `TICK_COUNTER = X_add_delay`; `SCALE_DELTA = X_addi`; `ANIM_COUNTER = X_ANIM_DELAY (2)`; list `enemyXList_0` |
| movement | `xBehaviour` `objects.asm:221`. Purely radial: every `X_add_delay+1` frames `SCALE -= SCALE_DELTA` (delta frozen at spawn, delay read live from the global). Angle never changes. Phase 0: delay 1, delta 1 → 25 scale units/s → 200→8 in 384 frames (7.7 s). |
| animation | every 3 frames (`X_ANIM_DELAY=2`) cycle `enemyXList_0 → 1 → 2 → 3 → 0` (arm length 63, 50, 38, 50: pulsing "+") |
| draw | mode list, sprite scale 6, intensity `$5F` (`objects.asm:269`) |
| hit points | 1 |
| killed by shield | popup `score10/20/30` (+10/20/30 pts, §8.2); `deadEnemies += 1`; explosion type 0 = "implosion" (§6.1) |

### 3.2 Hidden X (`TYPE_HIDDEN_X`) — fading "+"
| item | value / source |
|---|---|
| spawn | `spawnHiddenX` `objects.asm:790`; requires `ALLOW_HIDDEN_X`; sound `SpawnX_Sound` |
| start | as Terrible X but `TICK_COUNTER = HX_add_delay`, `SCALE_DELTA = HX_addi` (`HX_addi` is **not** from the phase record: `initPhase` forces it to `INITIAL_HX_MOVE_STRENGTH = 1`, `VRelease.asm:2439`) |
| movement | `hiddenXBehaviour` `objects.asm:488`: radial, every `HX_add_delay+1` frames `SCALE -= 1`. Phase 0: `HX_add_delay = 3` → 1 unit per 4 frames (12.5/s). |
| animation | same 4-frame "+" cycle every 3 frames |
| fade (`objects.asm:532-548`) | `s` = scale after this frame's move. `if (s >= 128) I = 0 (invisible); else { I = 143 - s; if (I > 127) I -= 16; }` → s=127: 16, s=64: 79, s=16: 127, s=15..9: 112..118. Drawn with intensity `I`. |
| draw | mode list, sprite scale 6 |
| hit points | 1 |
| killed by shield | popup `score15/30/45`; `deadEnemies += 2` (quirk: `explode_typeHiddenX` falls into `continue_ete`, which increments, then into `entry_otherexplosion_type`, which increments again; `VRelease.asm:1538,1561-1569`); explosion type `$FF` = plain dots (§6.2); `Explosion_Sound` played twice |

### 3.3 Hunter (`TYPE_HUNTER`) — triangle
| item | value / source |
|---|---|
| spawn | `spawnHunter` `objects.asm:624`; requires `ALLOW_HUNTER`; sound `SpawnHunter_Sound` |
| angle | `r = my_random2 & 0x7F; if (r >= 120) r -= 120;` `i = (r*6) >> 5` (0..22); `ANGLE = i * 32` (snapped to 16° steps: 23 possible directions); `CURRENT_LIST = HunterList[i]` (`objects.asm:645-678`) |
| start | `SCALE = 200`; `TICK_COUNTER = Hunter_add_delay` — **not** from the phase record, forced to `INITIAL_HUNTER_MOVE_DELAY = 0` by `initPhase` (`VRelease.asm:2437`); `SCALE_DELTA = Hunter_addi` (phase: 1 early, 2 from phase 6) |
| movement | `hunterBehaviour` `objects.asm:367`: radial dive, every frame `SCALE -= Hunter_addi` → 50 (or 100) units/s, 200→8 in 3.8 s (1.9 s). **No lateral tracking** — "tracks you down" = fast straight dive pointing at the base. |
| sprite | fixed per object (no animation). `HunterList_i` points the apex toward the centre. The 24 lists are 15° apart but are indexed with `ANGLE/32` (16° steps), so orientation drifts up to ~22° off-radial at high indices; `HunterList_23` is only reachable through squads. Reproduce by using index `ANGLE >> 5`. |
| draw | MVLC, scale 6, intensity `$5F` |
| hit points | 1 |
| killed by shield | popup `score15/30/45`; `deadEnemies += 1`; explosion type 3 = fading polygon outline (§6.3) |

### 3.4 Bomber (`TYPE_BOMBER`) — rotating octagon
| item | value / source |
|---|---|
| spawn | `spawnBomber` `objects.asm:1068` (body `spb_allowed`); requires `ALLOW_BOMBER`; sound `SpawnBomber_Sound` |
| scale | `SCALE = 100 + (my_random2 & 31)` → 100..131 (comment "63-127" is wrong). Constant for life. |
| angle | `b = my_random`, rotated left through carry 4×, `b ^= my_random2; b += my_random2; b ^= RecalCounterLow; ANGLE = (b & 0x7F) * 6` (0..762) (`objects.asm:1100-1115`) |
| movement | `bomberBehaviour` `objects.asm:1525`: **orbits, never moves inward.** Every `Bomber_add_delay+1` frames `ANGLE -= 2` (1°, clockwise, wrap +720). Phase 0: delay 1 → 25°/s; from phase 6: delay 0 → 50°/s. `Bomber_addi` is copied into `ANGLE_DELTA` but **unused** (step is hard-coded 2). |
| animation | `ANIM_COUNTER` reload `BOMBER_ANIM_DELAY = 2` → every 3 frames `BomberList_0..8` (9 frames, octagon rotating ~5° per step then snapping back) |
| firing | `SHOT_COUNTER = SHOT_COUNTER_RESET = bomber_delay_start` at spawn. Every frame `SHOT_COUNTER--`; when it hits **exactly 0** (`bne`): `next = SHOT_COUNTER_RESET - BOMB_RELOAD_REDUCTION(25)` (8-bit); `if (next <= minimum_bomb_reload) next = minimum_bomb_reload` (**unsigned** compare); `SHOT_COUNTER = SHOT_COUNTER_RESET = next`; then spawn a shot (§3.5) if a slot is free (the timer is reduced even if no slot). Phase 0 (start 200, min 50): shots at t = 200, +175, +150, +125, +100, +75, +50, +50 … frames. Quirk: if start < 25 the subtraction wraps (phase 2: start 20, min 200 → intervals 20, 251, 226, 201, 200, 200 …). |
| draw | MVLC, scale 6, intensity `$5F` |
| hit points | 1 |
| killed by shield | popup `score18/36/54`; `deadEnemies += 1`; explosion type 1 = small diamonds at polygon corners (§6.4) |

### 3.5 Bomber shot (`TYPE_SHOT`)
| item | value / source |
|---|---|
| spawn | inline in `bomberBehaviour` `objects.asm:1570-1598`; sound `BomberShot_Sound` |
| start | copies the bomber's `Y_POS, X_POS, ANGLE, SCALE` (current pre-update values); `TICK_COUNTER = shot_add_delay`; `SCALE_DELTA = shot_addi`; list `ShotList_0` |
| movement | `shotBehaviour` `objects.asm:821`: radial, every `shot_add_delay+1` frames `SCALE -= SCALE_DELTA`. Phase 0: 1 unit / 2 frames → from scale 100..131 reaches the base in ~3.7–4.9 s. Phase 8 / dragon_raid_2: delay 0, delta 2 → 100 units/s. |
| draw | VLC (4 draws from the object point), scale 6, intensity `$7F` — a diamond with one vertex **at** the object position, extending +80 units in x (not centred; faithful to the original) |
| hit points | 1 (can be destroyed by the shield) |
| killed by shield | popup `score10/20/30`; `deadEnemies += 1`; explosion type 4 = fade in place (§6.5) |

### 3.6 Dragon (`TYPE_DRAGON`) — diamond, 2 hits, with two escorts
| item | value / source |
|---|---|
| spawn | `spawnDragon` `objects.asm:1130`; requires `ALLOW_DRAGON`; sound `SpawnDragon_Sound`; needs up to **3 slots** (dragon + 2 children; if the pool runs out, it spawns with 0 or 1 children) |
| start | `SCALE = 100 + (my_random2 & 31)` (100..131); `ANGLE = ANGLE_0_762`; `DRAGON_COUNTER = (Dragon_Angle_delay << 4) | (Dragon_Scale_delay & 15)`; behaviour `dragonBehaviour_full`; list `DragonList_0` |
| spiral (`objects.asm:1228-1313`) | Two independent counters packed in nibbles of `DRAGON_COUNTER`: **angle**: every `Dragon_Angle_delay+1` frames `ANGLE += Dragon_Angle_addi` (16-bit; `if >= 720: -= 720`), counter-clockwise; **scale**: every `Dragon_Scale_delay+1` frames `SCALE -= 1`, base test `≤ 8` (on base hit both children are switched to free behaviour, then `gameOver`). Phase 0: angle +2 (1°) every 2 frames = 25°/s, scale −1 every 9 frames = 5.6 units/s → from ~115 to 8 in ~19 s (≈ 1.3 turns). Phase 3+: angle +4 every frame (100°/s), scale −1 every 6 frames. Phase 8: +6/frame (150°/s), −1 every 2 frames. Delay values must stay ≤ 15 (nibble). |
| animation | on frames where `RecalCounterLow` is odd, advance `DragonList_0 → 3` (pulsing diamond, half-size 74/70/66/70) |
| draw | MVLC, scale 6; intensity `$7F` while undamaged, `$4F` after the first hit (`objects.asm:1325-1331`) |
| hit points | **2**. First shield hit (`exchangeToExplosion`, `VRelease.asm:1381-1389`): behaviour switches `dragonBehaviour_full → dragonBehaviour_half` (same code, one `nop` apart — the switch is the "damaged" flag), `DragonFirstHit_Sound`, no score, no explosion, **`deadEnemies` not incremented**. |
| killed (2nd hit) | both living children switched to free behaviour (`DRAGON` ptr = 0); popup `score18/36/54`; `deadEnemies += 1`; explosion type 2 = small "x" crosses at polygon corners (§6.6) |

### 3.7 Dragon child (`TYPE_DRAGONCHILD`) — small "x" escort
`DragonChildObjectStruct` (`objects.asm:43`): `WIGGLE`, `WIGGLE_DIRECTION` occupy the CURRENT_LIST bytes;
`ANGLE_OFFSET(2)`, `DRAGON(2)` parent pointer, `SCALE_OFFSET(1)`.

| item | value / source |
|---|---|
| creation | `initDragonChild` `objects.asm:1198`. Child 1: `ANGLE_OFFSET = -20` (−10°), `SCALE_OFFSET = 10`, `WIGGLE = +4, DIR = 0`. Child 2: `ANGLE_OFFSET = -40` (−20°), `SCALE_OFFSET = 20`, `WIGGLE = -4, DIR = 1`. (`SCALE_OFFSET = (-ANGLE_OFFSET_low) >> 1`.) Children trail **behind** the dragon (it moves CCW) and further out. |
| bound behaviour (`dragonchildBoundBehaviour` `objects.asm:973`), every frame | `SCALE = parent.SCALE + SCALE_OFFSET`; `ANGLE = parent.ANGLE + ANGLE_OFFSET` (+720 if negative); pos from circle; wiggle: `if DIR: { WIGGLE++; if WIGGLE == 4: DIR = 0 } else { WIGGLE--; if WIGGLE == -4: DIR = 1 }`; `SCALE += WIGGLE`. → radial triangle-wave wobble ±4 scale units, period 16 frames, the two children in opposite phase. No base test while bound. |
| bound draw | `Dragonchild_List`, mode list, scale 6, intensity `$5F` |
| freed (parent killed or parent reached base) | behaviour `dragonchildFreeBehaviour` `objects.asm:1029`: **every frame** `SCALE -= dragonchild_addi` (phase: 1, later 2) straight inward on its current angle; base test ≤ 8 → `gameOver`. Phase 0: 50 units/s — "rush in". Intensity `$7F`. |
| killed by shield | if parent still alive its `CHILD_1/2` pointer is cleared (`VRelease.asm:1545-1556`); popup `score10/20/30`; `deadEnemies += 2` (same double-increment quirk as Hidden X); explosion type `$FF` dots |

### 3.8 Squads (6 enemies in a ring)
`spawnSquad` (`VRelease.asm:649-762`).
* Triggered when `spawn_allowed == ALLOW_SQUAD ($80)` exactly (squad-raid phases), or when the type roll
  is 231..240 (§9.2).
* Preconditions (skipped in demo mode): `object_count <= 25` **and** `t2_rest > $20` (CPU headroom
  > ~10000 cycles — for the port treat as always true). Failing either: no spawn, `spawn_count` not
  incremented. If the phase lacks `ALLOW_SQUAD`: no spawn and `spawn_timer = 0` (retry next frame).
* Type by `my_random2`: 0..50 Hunter squad, 51..100 Hidden-X squad, 101..150 X squad, 151..200 Bomber
  squad, 201..255 none (dragon squad commented out). Per-type `ALLOW_*` bits are **not** checked.
* `spawn_count += 5` (+1 already counted = 6).
* Object 1 is spawned normally (random angle). Objects 2..6: `ANGLE = previous.ANGLE + 120` (60°, wrap
  −720) and position recomputed (`CORRECT_SQUAD` `VRelease.asm:607`) → 6 enemies evenly spaced on a full
  ring. All use the same `my_random2`, so a bomber squad shares one scale. Hunter squads re-pick the
  sprite: `HunterList[ANGLE >> 5]` (`CORRECT_HUNTER_ANGLE`, `VRelease.asm:624`; angles are not re-snapped).
* If the pool runs out mid-squad the remainder is silently skipped. Each member plays its spawn sound.

---

## 4. Starlets (bonus stars) (`TYPE_STARLET`)

| item | value / source |
|---|---|
| spawn | roll 0..15 (§9.2) → `doStarlet` `VRelease.asm:764`: only if `starletCount < 3` (else nothing this spawn period, no retry) → `spawnStarlet` `objects.asm:864` (requires `ALLOW_STAR`, else retry next frame). Sound `SpawnStar_Sound`. |
| identity | `I_AM_STAR_NO` = lowest free bit of `star_active_flag` (bits 0..2); `starletCount++`. |
| radius (fixed) | `STAR_SCALE = 40`: star 0 → `SCALE 40`, star 1 → `25` (−15), star 2 → `50` (+10). Radius 4320 / 2700 / 5400 raw — close around the base, inside the shield's reach. |
| angle | `b = my_random2 >> 3` (0..31); star 0: `+0`, star 1: `+150`, star 2: `+90`; `ANGLE_base = (2*b) mod 720` → star 0 at 0..31°, star 2 at 90..121°, star 1 at 150..181° (`objects.asm:926-948`). `ANGLE_base` never changes. |
| movement (`starletBehaviour` `objects.asm:688`) | every frame `world = ANGLE_base + starletAngle (mod 720)`, `pos = circle[world]`. `starletAngle = 720 − base_angle`, so starlets circle **clockwise at 1°/frame (50°/s, 7.2 s per turn)**, opposite to the base. They never move inward and never touch the base. |
| animation | `STARLET_ANIM_DELAY = 1` → every 2 frames `StarletList_0 → 10` (11 frames, a rotating 5-pointed star). |
| draw | MVLC, intensity `$5F`. The code loads scale 3 ("tiny starlet") but `entry_optimized_draw_mvlc_unloop` overwrites the latch with **6** — actual size is scale 6 (see open questions). |
| bonus score | `SCORE_COUNTER` starts at `STARLET_SCORE_DELAY = 75`, `dec/bpl` → fires every **76 frames (1.52 s)**. Each star owns a 3-digit decimal counter `star_N_score` (hundreds, tens, ones), initialised to 002 (`STARLET_START_SCORE`). On each fire: value += 2; if hundreds overflow → clamp to **999** (`objects.asm:719-763`). Then `buildscoreX` (`VRelease.asm:1720`): add the star's value to the player score and spawn a ScoreX popup (§5.2) at the star's position. Sequence: 4, 6, 8, … 998, 999, 999 … (README's "4 points per second" is approximate). |
| killed by shield | `starletCount--`, its flag bit cleared; **no score, no popup, `deadEnemies` not incremented**; explosion type 5 (fast bright dots, §6.7). Note the starlets sit at scale 25..50, so every shield released past ~60 kills them. |

---

## 5. Score popups

### 5.1 Kill score popup (`scoreBehaviour`, `objects2.asm:365`)
* Created by `buildscore10/15/18` (`VRelease.asm:1613-1706`) via `newObject` at the victim's `Y_POS, X_POS,
  ANGLE, SCALE`; `CURRENT_LIST` = one of `score10, score15, score18, score20, score30, score36, score45,
  score54`; `SCORE_COUNTDOWN = 0`. If no slot is free the points are still awarded, just not shown.
* Each frame: if `SCALE >= shield_max (208)` → scheduled for removal. `SCORE_COUNTDOWN++`; when it
  reaches `score_display_time` (50 normally; 25 / 12 under CPU load, §9.4) → scheduled for removal.
  On frames where `RecalCounterLow` is odd, `SCALE++` (drifts outward 25 scale units/s).
* Visible for `score_display_time` frames (≈1 s), removed the frame after.
* Draw: mode list, scale 3, intensity `$5F`.

### 5.2 Starlet score popup (`scoreXBehaviour`, `objects2.asm:410`)
* `ScoreXObjectStruct` holds three digit-list pointers. Digits come from `NumberList` (`data.i:163`, font
  by reference only). Hundreds digit omitted if 0; tens omitted if tens and hundreds are 0; ones always.
  Drawn hundreds → tens → ones; each digit list ends with a blank move that advances to the next digit
  cell, so they are chained from the popup position.
* Same timing/drift as 5.1 (no `shield_max` check). Scale 3, intensity `$5F`.

---

## 6. Explosions (`explosionBehaviour`, `objects2.asm:9`)

### 6.1 Conversion (`exchangeToExplosion`, `VRelease.asm:1379`)
The killed object's slot is **re-used**: `BEHAVIOUR = explosionBehaviour`, `EXPLOSION_TYPE` set, position
(`Y_POS, X_POS, SCALE`) unchanged, `EXPLOSION_SCALE = 0`, `explosionActiveCounter++`.
Exception (`VRelease.asm:1579-1590`): if `object_count >= MAX_OBJECTS-3 (35)` **and**
`explosionActiveCounter >= 4`, the object is removed immediately without an explosion (the score/popup
was already handled).

All explosions are centred on the victim and use **`rotList`** — the current base polygon (`sided` = 5..8
corners, radius 108, rotated by `base_angle`, rebuilt every frame by `buildRotatedNSidedFigure`
`VRelease.asm:1307`). Corner `k` = `circle[(base_angle + k·720/sided) mod 720]` (`720/sided` =
144/120/102/90). The polygon is drawn at scale `EXPLOSION_SCALE`, so the ring radius is
`108·EXPLOSION_SCALE` raw units: an expanding ring that rotates with the base and has as many corners as
the base currently has sides. Intensity `$7F` unless stated.

### 6.2 Growth / lifetime rules
Let `e = EXPLOSION_SCALE`, `n = explosionActiveCounter` (includes this one), `M = explosionMax`
(40 = `INITIAL_EXPLOSION_MAX`, reset every non-spawn frame; +5 temporarily each time an explosion ends in
a frame).

| type | victim | growth per frame | ends when | frames |
|---|---|---|---|---|
| 0 | Terrible X | **implosion**: `e += 2` while `e < 30`; then `e -= 3` each frame | `e - 3 < 0` | 0→30 in 15 frames, 30→0 in 10, end on frame 26 |
| 1, 2, 3, 6, `$FF` | Bomber, Dragon, Hunter, "?", Hidden X / Dragon child | `e += 2` | `e + n > M` (signed) → `M += 5`, `n--` | single explosion: 20 frames (e = 40); with more active ones they die sooner ("the more explosions, the less far they reach") |
| 4 | Shot | `e += 2` (only used as a lifetime) | same as above, ~20 frames | see 6.5 |
| 5 | Starlet | `e += 4` | `e > 80` | 21 frames (e = 84) |

On the end frame the explosion is still drawn, its `SCALE` is zeroed and it is removed on the next frame.

### 6.3 Visuals per type (`objects2.asm:72-357`)
* **Dots** (types 0 and `$FF`, `doXExplosion`): one dot at each polygon corner (`sided` dots).
* **Type 3 Hunter** (`doHunterExplosion`): the full closed polygon outline (move to corner 0, `sided`
  lines), intensity `$7F − 3·e` (fades; wraps negative if `e > 42`, normally unreachable).
* **Type 1 Bomber** (`doBomberExplosion`): at each corner a small diamond drawn at **scale 6**: lines
  `(+40,+40), (+40,−40), (−40,−40), (−40,+40)` as `(x,y)` deltas — i.e. the `ShotList_0` diamond.
* **Type 2 Dragon** (`doDragonExplosion`): at each corner a small "x" at scale 6: draw `(+36,+36)`,
  move `(−36,0)`, draw `(+36,−36)`, move `(−36,0)` (`(x,y)` deltas) — the `Dragonchild_List` cross,
  anchored at its lower-left corner.
* **Types 5 Starlet and 6 "?"** (`doBonusExplosion`): dots with a ~20-loop dwell → noticeably brighter
  dots.
* **Type 4 Shot** (`doShotExplosion`): does not expand; redraws the shot diamond at the shot's position at
  scale 6 with intensity `EXPLOSION_INTENSITY` (starts `$7F`, `−4` per frame).

### 6.4 Shot fade — bug note
The intensity-underflow path clears `SCALE+u_offset1,u` **after** `u` was already advanced to the next
object (`objects2.asm:176`), so it zeroes byte 11 of whatever slot precedes the next object in RAM
instead of ending the fade. In practice the generic `e + n > M` rule ends the shot explosion after ~20
frames (intensity then ≈ 47) before the intensity underflows (32 frames). Port: end it by the
`e`-rule (and optionally also at intensity < 0); do not reproduce the stray write.

---

## 7. Mystery "?" (bonus object) (`TYPE_BONUS`)

### 7.1 Spawn
Roll 241..252 (§9.2) → only if `bonusCounter == 0` (no bonus active and no "?" in flight; otherwise
nothing, no retry) → `spawnBonus` `objects.asm:1347` (requires `ALLOW_BONUS`). Sets `bonusCounter = 1`
(blocks further "?" spawns; `drawBonus` ignores values ≤ 1). Sound `SpawnBonus_Sound`.
Start: `SCALE = 200`, `ANGLE = ANGLE_0_762`, `TICK_COUNTER = Bonus_add_delay (0)`,
`SCALE_DELTA = Bonus_addi` (2; 3 from phase 6), `ANIM_COUNTER = BONUS_ANIM_DELAY (1)`, list `BonusList_0`.

### 7.2 Movement / animation (`bonusBehaviour` `objects.asm:1384`)
Radial: on tick `SCALE -= SCALE_DELTA`. The tick counter is **reloaded from `X_add_delay`** (not
`Bonus_add_delay` — quirk), so after the first step it moves every `X_add_delay+1` frames: phase 0 →
2 units / 2 frames = 50 units/s, 200→8 in ~4 s. Animation reloads with `X_ANIM_DELAY (2)` → every 3
frames `BonusList_0 → 16` (17 frames, a "?" spinning about its axis). Draw: mode list, scale 6, intensity
`$5F`. Reaching `SCALE ≤ 8` (or borrow) = **collected** (not game over) → `initBonus`.

### 7.3 Collected: `initBonus` (`VRelease.asm:1956`)
* `bonusDestroyedCounter = 0`.
* Duration `bonusActiveTime` seconds (phase: 30; 15 in `phase8`/`dragon_raid_2`; max 99):
  `bonusCounter = 50 · bonusActiveTime` frames; decimal display digits `bonus_time_1:bonus_time_0`.
* Type: `t = (my_random2 & 3) + 1; if (t == 4) t = SHIELD` → **Shield 50 %, Expand 25 %, Faster 25 %**
  (`BONUS_TYPE_SHIELD = 1, EXPAND = 2, FASTER = 3`).

| bonus | effect while active | HUD icon |
|---|---|---|
| Shield (protection) | base survives one enemy hit (§2.2); that hit ends the bonus | `BonusShieldList` |
| Faster | `shieldSpeed <<= 1`, `shield_width_adder <<= 1` | `BonusFasterList` |
| Expand (wider) | `shieldWidthGrowth = 1`, `shield_width_adder = shieldSpeed` | `BonusExpandList` |

* The "?" object is removed; `timerObject` becomes the timer (§7.4); `BonusGot_Sound`.
* `endBonus` (`objects2.asm:531`), when the counter reaches 0: restores the changed shield parameters
  from the current phase record (Faster: bytes 2 and 5; Expand: bytes 1 and 5), `bonusActiveType = 0`,
  `timerObject` disabled. A phase change during a bonus re-applies the bonus modification
  (`VRelease.asm:2441-2463`).

### 7.4 Bonus timer (`timerBehaviour`, `objects2.asm:460`)
Static `timerObject`, called once per frame from `main`. Shown inside the base: move scale 6 to
`(y,x) = (−32, −80)` for two digits, `(−32, −32)` for one digit (raw ×6). Digits from `NumberList` at
scale 3, intensity `$5F`. `bonusCounter--` each frame (0 → `endBonus`); every 50 frames the displayed
seconds decrement; when the tens digit reaches 0 it switches to the one-digit position.

### 7.5 "?" destroyed by the shield (`VRelease.asm:1391-1426`)
* `bonusCounter = 0` (a new "?" may spawn), `bonusDestroyedCounter++` (= N).
* Score: **505 × N** points (adds decimal 505 N times; N resets to 0 when a "?" is collected). No popup.
* Gimmick: let `g = N; while (g >= 20) g -= 15;` → if `g == 5` Pac-Man, `10` Worm, `15` Ghost
  (i.e. on the 5th, 10th, 15th, 20th… consecutive destroyed "?"). Then explosion type `$FF`
  (`deadEnemies += 2`). Otherwise explosion type 6 (bright dots, `deadEnemies += 1`) + `Explosion_Sound`.

### 7.6 Gimmick (`gimmikBehaviour`, `objects2.asm:773`) and HUD icon
* Gimmick uses `timerObject`: lifetime 255 frames (5.1 s); move scale 6 to Pac-Man `(y,x)=(43,−13)`,
  Worm `(0,−64)`, Ghost `(−32,40)`; animation step every 3 frames (Pac-Man 4 frames, Ghost 3, Worm 7);
  the Worm also does `X_POS += 1` per step (crawls right 6 raw units / 3 frames). Drawn VLC at scale 3,
  intensity `$4F`. Purely decorative; it is replaced if a bonus timer starts.
* HUD icon (`drawBonus`, `VRelease.asm:2350`, only while `bonusCounter > 1`): moves with
  `y = s, scale = s, x = −30` where `s` starts at 1 and grows +2 per frame up to 127 (icon flies from the
  centre to the top, slightly left), then the icon list is drawn at scale 4.

---

## 8. Shield kill pass (how objects die)

`finishShield` (`VRelease.asm:879`) runs on button release: `deadEnemies = 0`; if `shieldStart <= 9`
nothing happens; else every active object is tested with `onShield` (`VRelease.asm:977`) and, on hit,
passed to `exchangeToExplosion`.

### 8.1 `onShield` hit test (summary; full shield spec elsewhere)
Hittable = types with page < `TYPE_BOUNDARY ($60)`: X, Hidden X, Hunter, Bomber, Shot, Dragon, Dragon child,
Starlet, "?". Explosions, popups, starfields, letters are immune.
1. `outer = shieldStart + 10` (`SHIELD_VARIANCE`, not added if 255). Miss if `outer <= SCALE`.
   (A first-stage inner test exists but is neutralised by a stray `clra`, `VRelease.asm:994`.)
2. Polygon correction: `d = ((ANGLE − base_angle) mod 720) / 2` degrees, `k = d mod (360/sided)`
   (72/60/51/45); `code = polygon_<sided>_div_correction[k]` (`VRelease.asm:944-972`); the radii are
   reduced by the fraction for `code` (integer shift arithmetic in `collisionRoutines.i`: e.g. `div8`:
   `r − (r>>3)`, `div5`: `r − (r>>2) + (r>>5)`, `NO_DIV`: unchanged) to approximate the polygon edge.
3. Hit iff `corr(outer) > SCALE` **and** `corr(inner) < SCALE`, with
   `inner = shieldStart − shieldWidth − 10` (if that borrows: `shieldStart − shieldWidth`).
So the object's "hitbox" is just its centre point (radius = `SCALE`), tested against a polygonal ring
widened by 10 scale units on each side.

### 8.2 Score multiplier
`deadEnemies` counts kills in the current release (incremented after the score is built). Popup/score =
base × `min(deadEnemies_before + 1, 3)`:

| base | 1st | 2nd | 3rd+ | lists | victims |
|---|---|---|---|---|---|
| 10 | 10 | 20 | 30 | `score10/20/30` | X, Shot, Dragon child |
| 15 | 15 | 30 | 45 | `score15/30/45` | Hidden X, Hunter |
| 18 | 18 | 36 | 54 | `score18/36/54` | Bomber, Dragon (2nd hit) |

Note the double increments (Hidden X, Dragon child, gimmick "?") push later kills to ×3 sooner; starlets
and a first dragon hit do not count. `score50` exists in `data.i` but is unused.

---

## 9. Spawning, RNG and difficulty

### 9.1 RNG (`getButtonState`, `VRelease.asm:1902-1925`)
Two 8-bit generators, `my_random` and `my_random2`; **only one is updated per frame**, alternating by
the parity of `RecalCounterLow`:
```c
uint8_t a = RecalCounterLow + RecalCounterHi;          // 8-bit wrap
uint16_t t = a + current_button_state;                  // C = carry out
uint8_t c = t >> 8; a = (uint8_t)t;
if (RecalCounterLow & 1) { a ^= my_random;  my_random  = (a << 1) | c; }   // ROLA through carry
else                     { a ^= my_random2; my_random2 = (a << 1) | c; }
```
`current_button_state` is an 8-frame button history shift register (bit 0 = this frame's previous state,
`VRelease.asm:1943-1951`), so player input feeds the RNG. Extra mixing:
* `spawnStarfield` (`objects.asm:1459-1480`) three times: `a = rol(my_random); a ^= my_random2;
  a += my_random2; a ^= RecalCounterLow; my_random2 = a`.
* Bomber angle (`objects.asm:1100-1107`) mixes without storing.
Because each value is reused for 2 frames and several spawns in one frame share it, rolls are correlated
(squads share scale; "?" and Bomber placement correlate). For the port any decent PRNG is acceptable; to
be faithful keep the "one 8-bit value for selection (`my_random`), one for placement (`my_random2`)"
split.

### 9.2 Spawn timer and type selection (`check_spawn`, `VRelease.asm:526-605`)
```
if (--spawn_timer >= 0) { no_spawn_now(); return; }          // §9.4
a = spawn_reset;
if (--spawnRest_Counter < 0) {
    spawn_increase_delay = min(spawn_increase_delay + 1, max_spawn_increase_delay);
    spawnRest_Counter = spawn_increase_delay;
    a = max(a - 1, min_spawn_reset);                           // signed compare
}
spawn_reset = spawn_timer = a;          // → one spawn attempt every spawn_reset+1 frames
spawn_count++;
r = my_random;
if (spawn_allowed == ALLOW_SQUAD) squad();
else if (r <=  15) starlet();      // 16/256  (only if starletCount < 3)
else if (r <=  80) X();            // 65/256
else if (r <= 130) hiddenX();      // 50/256
else if (r <= 170) hunter();       // 40/256
else if (r <= 200) bomber();       // 30/256
else if (r <= 230) dragon();       // 30/256
else if (r <= 240) squad();        // 10/256
else if (r <= 252) mystery();      // 12/256 (only if bonusCounter == 0)
else nothing;                       //  3/256, spawn_count-- 
```
* Type not allowed by the phase's `spawn_allowed` mask (`ALLOW_STAR $01, X $02, HIDDEN_X $04, HUNTER
  $08, BOMBER $10, DRAGON $20, BONUS $40, SQUAD $80`, `inBothBanks1.i:411-418`): `spawn_count--`,
  `spawn_timer = 0` → **re-roll next frame** (which also runs the `spawn_reset` decay logic again).
* Starlet with 3 stars, "?" with `bonusCounter != 0`, roll 253..255, squad preconditions failing:
  `spawn_count--`, wait for the next period.
* Pool full: the attempt is lost but `spawn_count` stays incremented.
* Initial values (`initGame`, `VRelease.asm:1842-1848`, then overridden by the phase): `spawn_reset =
  spawn_timer = 50`, `spawnRest_Counter = spawn_increase_delay = 1`, `min_spawn_reset = 15`,
  `max_spawn_increase_delay = 10`.

### 9.3 Phases (`check_stage` / `initPhase`, `VRelease.asm:2383-2463`)
`spawn_count` counts spawned enemies (squads count 6). When `spawn_count > next_phase_at` (and
`next_phase_at != 0`), `spawn_count = 0`, advance to the next record in `phaseList`
(`phaseDefinitions.i:4`), `phaseChange_Sound`. `initPhase` copies the 30-byte record into RAM
(`phaseBlockStart`, `inBothBanks1.i:106-144`), applies spawn-rate bytes 24..27 unless `$FF` (keep),
then forces `Hunter_add_delay = 0`, `HX_addi = 1`.

Record layout (byte offset → RAM variable): 0 `spawn_allowed`, 1 `shieldWidthGrowth`, 2 `shieldSpeed`,
3 `shieldMinorIncreaseCounterReset`, 4 `shieldMinorIncrease`, 5 `shield_width_adder`, 6 `shot_add_delay`,
7 `shot_addi`, 8 `X_add_delay`, 9 `X_addi`, 10 `HX_add_delay`, 11 `Hunter_addi`, 12 `Bomber_add_delay`,
13 `Bomber_addi` (unused), 14 `bomber_delay_start`, 15 `Dragon_Scale_delay`, 16-17 `Dragon_Angle_addi`
(word), 18 `Dragon_Angle_delay`, 19 `dragonchild_addi`, 20 `minimum_bomb_reload`, 21 `Bonus_add_delay`,
22 `Bonus_addi`, 23 `bonusActiveTime` (s), 24 `spawn_reset`, 25 `spawn_increase_delay`, 26
`min_spawn_reset`, 27 `max_spawn_increase_delay`, 28-29 `next_phase_at` (0 = final).

Order: `phase0_x, phase0_hx, hunter_raid_1, phase1, squad_raid_1, phase1, bomber_raid_1, phase2,
squad_raid_1, phase2, dragon_raid_1, phase3, squad_raid_2a, phase3, hunter_raid_2, phase4, squad_raid_2,
phase4, bomber_raid_2, phase5, squad_raid_2, phase5, dragon_raid_2, phase6, squad_raid_3, phase6,
bomber_raid_2, dragon_raid_2, phase7, squad_raid_3, phase7, hunter_raid_2, hunter_raid_2, phase8`
(34 entries; `phase8` has `next_phase_at = 0`). The start phase is `initialPhase` (options menu).

Extracted values (decimal; `allow` hex; −1 = keep). Columns are record bytes 0..28 in order:
`allow`=0, `shWGrowth`=1, `shSpeed`=2, `shMinCnt`=3, `shMinInc`=4, `shWAdd`=5, `shot_dly`=6,
`shot_add`=7, `X_dly`=8, `X_add`=9, `HX_dly`=10, `Hun_add`=11, `Bmb_dly`=12, `Bmb_add`=13,
`bombStart`=14, `DrScDly`=15, `DrAngAdd`=16-17, `DrAngDly`=18, `DcFree`=19, `minBomb`=20, `Bon_dly`=21,
`Bon_add`=22, `bonTime`=23, `spReset`=24, `spIncDly`=25, `minSpRst`=26, `maxIncDly`=27, `nextAt`=28-29.

|phase|allow|shWGrowth|shSpeed|shMinCnt|shMinInc|shWAdd|shot_dly|shot_add|X_dly|X_add|HX_dly|Hun_add|Bmb_dly|Bmb_add|bombStart|DrScDly|DrAngAdd|DrAngDly|DcFree|minBomb|Bon_dly|Bon_add|bonTime|spReset|spIncDly|minSpRst|maxIncDly|nextAt|
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|phase0_x|47|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|50|1|15|10|20|
|phase0_hx|4B|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|50|1|15|10|20|
|hunter_raid_1|08|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|10|1|15|10|10|
|bomber_raid_1|14|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|5|1|15|10|20|
|dragon_raid_1|20|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|10|1|15|10|10|
|phase1|FF|4|2|2|0|1|1|1|1|1|3|1|1|1|200|8|2|1|1|50|0|2|30|50|1|15|10|50|
|phase2|FF|4|2|3|1|1|1|1|1|1|2|1|1|1|20|5|2|0|1|200|0|2|30|40|1|15|9|50|
|phase3|FF|4|2|2|1|1|1|1|1|1|1|1|1|1|200|5|4|0|1|50|0|2|30|35|1|15|8|50|
|phase4|FF|3|3|20|0|2|1|1|1|1|1|1|1|1|25|5|4|0|1|50|0|2|30|30|1|15|7|100|
|phase5|FF|3|3|2|1|2|1|1|1|1|1|1|1|1|25|5|4|0|1|50|0|2|30|25|1|25|1|100|
|squad_raid_1|80|4|2|2|0|1|1|1|1|1|1|1|1|1|25|5|4|0|1|50|0|2|30|25|1|25|1|24|
|phase6|FF|3|3|1|1|2|3|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|30|1|25|5|50|
|squad_raid_2|80|3|3|1|1|2|3|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|25|1|25|1|40|
|phase7|FF|3|3|0|1|2|1|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|-1|-1|20|-1|100|
|phase8|FF|3|3|0|1|3|0|2|0|1|1|2|0|2|40|1|6|0|2|10|0|3|15|50|1|15|20|0|
|hunter_raid_2|08|3|3|1|1|2|3|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|10|1|15|10|10|
|bomber_raid_2|14|3|3|0|1|2|1|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|10|1|15|10|20|
|dragon_raid_2|20|3|3|2|1|2|0|2|0|1|1|2|0|2|40|1|6|0|2|10|0|3|15|10|1|15|10|10|
|squad_raid_3|80|3|3|0|1|2|1|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|25|1|25|1|40|
|squad_raid_2a|80|3|3|20|0|2|3|1|1|1|1|2|0|1|25|4|4|0|1|25|0|3|30|25|1|25|1|40|

### 9.4 Load management on non-spawn frames (`no_spawn_now`, `VRelease.asm:446-521`)
Uses `t2_rest` (spare CPU time measured at frame end). Thresholds: ≤ `$04` → `starfield_max = 1`,
`score_display_time = 12`; ≤ `$13` → `starfield_max = 3`, `score_display_time = 25`; ≤ `$23` →
`starfield_max = 8`, time 50 (only if no shield and no explosion active — otherwise nothing is reset and,
quirk, `explosionMax`/`score_display_time` receive the stale A/B registers: `explosionMax = t2_rest`
(20..35), display time = undefined; port: use 40/50); else `starfield_max = 10`, time 50.
`explosionMax = 40` in all other cases. Then the object budget:
`free = 38 − object_count + starfield_max − 10; if (free <= 10) starfield_max = max(free, 1)`.
Finally spawn one starfield (`starFieldCounter < starfield_max`) or remove one (prefer one that has not
yet shown a star). **Port:** there is no CPU limit, so treat `t2_rest` as large (max starfields 10,
popups 50 frames) but keep the object-budget rule so the 38-slot pool behaves the same.

---

## 10. Starfield (background) (`starfieldBehaviour`, `objects2.asm:572-698`)
Each starfield object carries 4 independent stars (`StarfieldObjectStruct`, `objects.asm:185`): per star
a `SCALE_n` (radius) and `POS_n` (angle code). Not hittable.
* Spawn (`spawnStarfield`, `objects.asm:1441`): all `POS_n = $FF` (not yet born); `SCALE_n` = random
  0..127 used as **birth delay** in frames (0 → 256).
* Birth / rebirth: `SCALE = 8`, `POS = (my_random2 & 127) * 2` (0..254).
* Per frame per live star: position = `circle[POS * 1.5 degrees]` (byte offset `3·POS`) at scale `SCALE`
  → drawn as a single dot; then `POS += star_swirl` (wrap: >240 → −240; negative → 240); radius grows
  `SCALE += 5` if new value > 176, `+4` if > 128, `+3` if > 64, `+2` if > 32, else `+1` (accelerating
  outward); intensity `min(SCALE + 10, $2F)`; when `SCALE > spawn_max (200)` → rebirth.
* `star_swirl` ∈ {−2, 0, +2} (POS units = ±3°/frame): when `RecalCounterLow == 0` and
  `(RecalCounterHi & 2) == 0` it steps by ±2 (direction from `my_random2` bit 7, clamped), and
  `RecalCounterLow` is incremented to fire only once (yes, it bumps the frame counter).

---

## 11. Vector shapes (absolute coordinates)

Converted mechanically from `data.i` (script-verified). Format: each object's polylines as `(x,y)`
points relative to the object position, sprite units; consecutive polylines are separated by a blank
move. Multiply by the sprite scale for raw units. The digit font `NumberList_0..9` (`data.i:163-244`) and
the letter font (`abc.i`) are referenced only (digits: mode lists, cell advance encoded as a trailing
blank move, drawn at scale 3 for popups/timer).

Procedural shapes (not in `data.i`):
* **Base / explosion polygon** `rotList`: `sided` (5..8) corners `circle[(base_angle + k·720/sided) mod 720]`
  (radius 108); base drawn at scale `BASE_SCALE = 5`, explosions at `EXPLOSION_SCALE`.
* **Bomber-explosion diamond** = `ShotList_0`; **Dragon-explosion cross**: `(0,0)-(36,36)`,
  `(36,0)-(0,36)` at scale 6.

#### Terrible X / Hidden X — `enemyXList_0..3` (mode list, drawn at sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `enemyXList_0` | 45 | `(-63,0) (63,0)`<br>`(0,63) (0,-63)` |
| `enemyXList_1` | 51 | `(-50,0) (50,0)`<br>`(0,50) (0,-50)` |
| `enemyXList_2` | 57 | `(-38,0) (38,0)`<br>`(0,38) (0,-38)` |
| `enemyXList_3` | 63 | `(-50,0) (50,0)`<br>`(0,50) (0,-50)` |

#### Hunter — `HunterList_0..23` (MVLC unloop list: first pair = move, then 3 draws; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `HunterList_0` | 270 | `(-26,62) (0,-62) (26,62) (-26,62)` |
| `HunterList_1` | 276 | `(-41,53) (16,-60) (9,67) (-41,53)` |
| `HunterList_2` | 282 | `(-54,41) (31,-54) (-8,67) (-54,41)` |
| `HunterList_3` | 288 | `(-62,25) (44,-44) (-25,62) (-62,25)` |
| `HunterList_4` | 294 | `(-67,8) (54,-31) (-41,54) (-67,8)` |
| `HunterList_5` | 300 | `(-67,-9) (60,-16) (-53,41) (-67,-9)` |
| `HunterList_6` | 306 | `(-62,-26) (62,0) (-62,26) (-62,-26)` |
| `HunterList_7` | 312 | `(-53,-41) (60,16) (-67,9) (-53,-41)` |
| `HunterList_8` | 318 | `(-41,-54) (54,31) (-67,-8) (-41,-54)` |
| `HunterList_9` | 324 | `(-25,-62) (44,44) (-62,-25) (-25,-62)` |
| `HunterList_10` | 330 | `(-8,-67) (31,54) (-54,-41) (-8,-67)` |
| `HunterList_11` | 336 | `(9,-67) (16,60) (-41,-53) (9,-67)` |
| `HunterList_12` | 342 | `(26,-62) (0,62) (-26,-62) (26,-62)` |
| `HunterList_13` | 348 | `(41,-53) (-16,60) (-9,-67) (41,-53)` |
| `HunterList_14` | 354 | `(54,-41) (-31,54) (8,-67) (54,-41)` |
| `HunterList_15` | 360 | `(62,-25) (-44,44) (25,-62) (62,-25)` |
| `HunterList_16` | 366 | `(67,-8) (-54,31) (41,-54) (67,-8)` |
| `HunterList_17` | 372 | `(67,9) (-60,16) (53,-41) (67,9)` |
| `HunterList_18` | 378 | `(62,26) (-62,0) (62,-26) (62,26)` |
| `HunterList_19` | 384 | `(53,41) (-60,-16) (67,-9) (53,41)` |
| `HunterList_20` | 390 | `(41,54) (-54,-31) (67,8) (41,54)` |
| `HunterList_21` | 396 | `(25,62) (-44,-44) (62,25) (25,62)` |
| `HunterList_22` | 402 | `(8,67) (-31,-54) (54,41) (8,67)` |
| `HunterList_23` | 408 | `(-9,67) (-16,-60) (41,53) (-9,67)` |

#### Bomber — `BomberList_0..8` (MVLC: move + 8 draws; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `BomberList_0` | 620 | `(0,69) (51,51) (69,0) (51,-51) (0,-69) (-51,-51) (-69,0) (-51,51) (0,69)` |
| `BomberList_1` | 631 | `(-5,69) (46,56) (69,5) (56,-46) (5,-69) (-46,-56) (-69,-5) (-56,46) (-5,69)` |
| `BomberList_2` | 642 | `(-11,69) (41,59) (69,11) (59,-41) (11,-69) (-41,-59) (-69,-11) (-59,41) (-11,69)` |
| `BomberList_3` | 653 | `(-18,68) (36,63) (68,18) (63,-36) (18,-68) (-36,-63) (-68,-18) (-63,36) (-18,68)` |
| `BomberList_4` | 664 | `(-23,65) (30,66) (65,23) (66,-30) (23,-65) (-30,-66) (-65,-23) (-66,30) (-23,65)` |
| `BomberList_5` | 675 | `(-29,63) (24,69) (63,29) (69,-24) (29,-63) (-24,-69) (-63,-29) (-69,24) (-29,63)` |
| `BomberList_6` | 686 | `(-34,60) (18,70) (60,34) (70,-18) (34,-60) (-18,-70) (-60,-34) (-70,18) (-34,60)` |
| `BomberList_7` | 697 | `(-39,57) (12,72) (57,39) (72,-12) (39,-57) (-12,-72) (-57,-39) (-72,12) (-39,57)` |
| `BomberList_8` | 708 | `(-44,53) (6,73) (53,44) (73,-6) (44,-53) (-6,-73) (-53,-44) (-73,6) (-44,53)` |

#### Bomber shot — `ShotList_0` (VLC: all 4 pairs are draws starting AT the object position; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `ShotList_0` | 719 | `(0,0) (40,40) (80,0) (40,-40) (0,0)` |

#### Dragon — `DragonList_0..3` (MVLC: move + 4 draws; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `DragonList_0` | 423 | `(-74,0) (0,74) (74,0) (0,-74) (-74,0)` |
| `DragonList_1` | 430 | `(-70,0) (0,70) (70,0) (0,-70) (-70,0)` |
| `DragonList_2` | 437 | `(-66,0) (0,66) (66,0) (0,-66) (-66,0)` |
| `DragonList_3` | 444 | `(-70,0) (0,70) (70,0) (0,-70) (-70,0)` |

#### Dragon child — `Dragonchild_List` (mode list; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `Dragonchild_List` | 39 | `(-18,-18) (18,18)`<br>`(-18,18) (18,-18)` |

#### Starlet (bonus star) — `StarletList_0..10` (MVLC: move + 10 draws; drawn at scale 6, see note)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `StarletList_0` | 467 | `(-20,36) (-76,34) (-41,-8) (-56,-63) (-3,-42) (42,-73) (39,-17) (82,16) (28,30) (8,84) (-20,36)` |
| `StarletList_1` | 480 | `(-24,33) (-79,25) (-40,-13) (-48,-69) (2,-42) (50,-68) (41,-12) (80,25) (24,33) (-2,84) (-24,33)` |
| `StarletList_2` | 493 | `(-28,31) (-82,16) (-38,-17) (-40,-74) (7,-42) (57,-62) (42,-8) (76,34) (20,36) (-11,84) (-28,31)` |
| `StarletList_3` | 506 | `(-31,27) (-83,6) (-36,-21) (-32,-78) (11,-41) (64,-55) (42,-3) (72,43) (16,38) (-21,82) (-31,27)` |
| `StarletList_4` | 519 | `(-34,23) (-83,-3) (-33,-25) (-22,-81) (16,-39) (70,-47) (42,2) (67,51) (12,39) (-30,79) (-34,23)` |
| `StarletList_5` | 532 | `(-36,19) (-82,-12) (-30,-29) (-13,-83) (20,-37) (75,-39) (42,7) (60,58) (7,40) (-39,75) (-36,19)` |
| `StarletList_6` | 545 | `(-38,15) (-80,-22) (-27,-32) (-3,-84) (24,-34) (79,-30) (41,12) (53,64) (3,41) (-47,70) (-38,15)` |
| `StarletList_7` | 558 | `(-40,11) (-77,-31) (-23,-35) (6,-84) (28,-31) (82,-21) (39,16) (46,70) (-2,41) (-55,64) (-40,11)` |
| `StarletList_8` | 571 | `(-41,6) (-73,-39) (-19,-37) (16,-83) (31,-28) (83,-11) (37,21) (37,75) (-7,40) (-62,58) (-41,6)` |
| `StarletList_9` | 584 | `(-41,1) (-68,-48) (-14,-39) (25,-80) (34,-24) (84,-2) (35,25) (29,78) (-11,39) (-68,50) (-41,1)` |
| `StarletList_10` | 597 | `(-41,-3) (-62,-55) (-10,-41) (34,-77) (37,-20) (84,8) (32,28) (20,81) (-16,38) (-73,42) (-41,-3)` |

#### Mystery "?" — `BonusList_0..16` (mode list; sprite scale 6)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `BonusList_0` | 743 | `(-29,36) (0,60) (29,36) (0,3) (0,-36)`<br>`(0,-61) (4,-61)` |
| `BonusList_1` | 752 | `(-40,24) (-20,56) (13,45) (0,3) (12,-34)`<br>`(21,-57) (26,-54)` |
| `BonusList_2` | 761 | `(-47,7) (-40,44) (-2,47) (-2,2) (24,-27)`<br>`(40,-44) (44,-40)` |
| `BonusList_3` | 770 | `(-46,-8) (-54,26) (-19,43) (-3,1) (33,-15)`<br>`(54,-26) (57,-20)` |
| `BonusList_4` | 779 | `(-40,-24) (-60,4) (-34,32) (-3,0) (36,-2)`<br>`(61,-4) (61,0)` |
| `BonusList_5` | 788 | `(-28,-38) (-57,-15) (-44,17) (-3,0) (36,9)`<br>`(59,15) (57,22)` |
| `BonusList_6` | 797 | `(-11,-46) (-48,-36) (-47,0) (-2,-2) (29,22)`<br>`(49,36) (44,40)` |
| `BonusList_7` | 806 | `(4,-47) (-31,-50) (-44,-15) (-1,-3) (19,31)`<br>`(31,52) (26,54)` |
| `BonusList_8` | 815 | `(20,-42) (-9,-59) (-36,-31) (0,-3) (5,36)`<br>`(10,60) (4,61)` |
| `BonusList_9` | 824 | `(35,-31) (9,-59) (-21,-42) (0,-3) (-5,36)`<br>`(-10,60) (-15,59)` |
| `BonusList_10` | 833 | `(44,-15) (31,-50) (-4,-47) (1,-3) (-19,31)`<br>`(-31,52) (-36,49)` |
| `BonusList_11` | 842 | `(47,0) (48,-36) (11,-46) (2,-2) (-29,22)`<br>`(-49,36) (-52,31)` |
| `BonusList_12` | 851 | `(44,17) (57,-15) (28,-38) (3,0) (-36,9)`<br>`(-59,15) (-60,9)` |
| `BonusList_13` | 860 | `(34,32) (60,4) (40,-25) (3,0) (-36,-2)`<br>`(-61,-4) (-60,-10)` |
| `BonusList_14` | 869 | `(19,43) (54,26) (47,-9) (3,1) (-33,-15)`<br>`(-54,-26) (-52,-32)` |
| `BonusList_15` | 878 | `(2,47) (40,44) (47,7) (2,2) (-24,-27)`<br>`(-40,-44) (-36,-49)` |
| `BonusList_16` | 887 | `(-12,44) (20,56) (40,24) (0,3) (-12,-34)`<br>`(-21,-57) (-15,-59)` |

#### Kill-score popups — `score10..score54` (mode list; scale 3)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `score10` | 69 | `(0,63) (63,63) (63,-63) (0,-63) (0,63)`<br>`(-63,63) (-63,-63)` |
| `score15` | 78 | `(0,0) (0,127)`<br>`(31,0) (126,0) (126,63) (31,63) (31,127) (126,127)` |
| `score18` | 87 | `(0,0) (0,-127)`<br>`(63,-127) (158,0) (63,0) (158,-127) (63,-127)` |
| `score20` | 95 | `(0,0) (96,0) (0,-127) (96,-127)`<br>`(158,-127) (254,-127) (254,0) (158,0) (158,-127)` |
| `score30` | 105 | `(0,0) (96,0) (32,-64) (96,-127) (0,-127)`<br>`(127,-127) (222,-127) (222,0) (127,0) (127,-127)` |
| `score36` | 116 | `(0,0) (96,0) (32,-64) (96,-127) (0,-127)`<br>`(127,-64) (127,-127) (222,-127) (222,-64) (127,-64) (127,0)` |
| `score45` | 128 | `(0,0) (0,127) (-96,31) (31,31)`<br>`(94,0) (189,0) (189,63) (94,63) (94,127) (189,127)` |
| `score50` | 139 | `(0,0) (96,0) (96,63) (0,63) (0,127) (96,127)`<br>`(158,127) (254,127) (254,0) (158,0) (158,127)` |
| `score54` | 151 | `(0,0) (95,0) (95,63) (0,63) (0,127) (95,127)`<br>`(158,31) (253,127) (253,0)`<br>`(158,31) (285,31)` |

#### Bonus HUD icons — `BonusShieldList`, `BonusFasterList`, `BonusExpandList` (mode list; drawn with myDraw_VL_mode2 at scale 4)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `BonusShieldList` | 31 | `(0,0) (-93,124) (-93,251) (0,251) (93,251) (93,124) (0,0)` |
| `BonusFasterList` | 21 | `(0,0) (76,51) (76,151) (0,202)`<br>`(0,101) (-76,51)`<br>`(-76,151) (0,101) (-127,101)` |
| `BonusExpandList` | 7 | `(0,0) (63,64) (63,126) (0,190)`<br>`(95,95) (158,95) (127,126)`<br>`(127,64) (158,95)`<br>`(95,222) (190,158) (190,32) (95,-32)` |

#### Gimmicks — `PacmanSmall_0..3`, `GhostSmall_0..2`, `WormSmall_0..6` (VLC: every pair is a draw from the start point; scale 3)

| label | data.i line | polylines `(x,y)` (blank move between polylines) |
|---|---|---|
| `PacmanSmall_0` | 1295 | `(0,0) (56,0) (98,-42) (98,-70) (98,-98) (56,-140) (0,-140) (-42,-98) (-42,-42) (0,0)` |
| `PacmanSmall_1` | 1306 | `(0,0) (56,0) (98,-56) (70,-70) (98,-84) (56,-140) (0,-140) (-42,-98) (-42,-42) (0,0)` |
| `PacmanSmall_2` | 1317 | `(0,0) (56,0) (98,-42) (42,-70) (98,-98) (56,-140) (0,-140) (-42,-98) (-42,-42) (0,0)` |
| `PacmanSmall_3` | 1328 | `(0,0) (56,0) (98,-28) (28,-70) (98,-112) (56,-140) (0,-140) (-42,-98) (-42,-42) (0,0)` |
| `GhostSmall_0` | 1339 | `(0,0) (0,60) (-12,120) (-72,120) (-132,120) (-144,60) (-144,0) (-96,24) (-60,0) (-24,24) (0,0)` |
| `GhostSmall_1` | 1351 | `(0,0) (0,60) (-12,120) (-72,120) (-132,120) (-144,60) (-144,0) (-120,24) (-84,0) (-48,24) (0,0)` |
| `GhostSmall_2` | 1363 | `(0,0) (0,60) (-12,120) (-72,120) (-132,120) (-144,60) (-144,0) (-84,24) (-48,0) (-12,24) (0,0)` |
| `WormSmall_0` | 1375 | `(0,0) (24,0) (36,0) (60,0) (84,0) (108,0) (120,12)` |
| `WormSmall_1` | 1383 | `(0,0) (12,0) (36,0) (84,24) (96,0) (108,0) (120,12)` |
| `WormSmall_2` | 1391 | `(0,0) (36,0) (48,24) (72,36) (84,0) (96,0) (108,12)` |
| `WormSmall_3` | 1399 | `(0,0) (24,0) (36,36) (60,24) (72,0) (84,0) (96,12)` |
| `WormSmall_4` | 1407 | `(0,0) (24,0) (24,24) (48,36) (48,0) (72,12) (84,24)` |
| `WormSmall_5` | 1415 | `(0,0) (12,0) (24,0) (36,24) (72,12) (96,24) (108,36)` |
| `WormSmall_6` | 1423 | `(0,0) (24,0) (36,0) (60,12) (84,0) (108,12) (120,24)` |

---

## 12. Constants quick reference (`inBothBanks1.i:328-424`)
| name | value | meaning |
|---|---|---|
| `MAX_OBJECTS` | 38 | pool slots |
| `BASE_SCALE` | 5 | base polygon scale; enemy base-hit threshold `BASE_SCALE+3 = 8` |
| `SPAWN_MAX_SCALE_INIT` | 200 (`$C8`) | spawn radius (scale) |
| `SHIELD_MAX_SCALE_INIT` | 208 (`$D0`) | max shield radius; popups vanish beyond |
| `SHIELD_VARIANCE` | 10 | shield hit tolerance (scale units) |
| `STAR_SCALE` | 40 | starlet orbit (stars at 40 / 25 / 50) |
| `STARLET_ANIM_DELAY` | 1 | starlet frame every 2 frames |
| `STARLET_SCORE_DELAY` | 75 | starlet bonus every 76 frames |
| `STARLET_START_SCORE` | 2 | first bonus shown = 4 |
| `X_ANIM_DELAY` | 2 | X / Hidden X / "?" frame every 3 frames |
| `BOMBER_ANIM_DELAY` | 2 | bomber frame every 3 frames |
| `BONUS_ANIM_DELAY` | 1 | first "?" anim step only |
| `BOMB_RELOAD_REDUCTION` | 25 | bomber reload shrink per shot |
| `INITIAL_EXPLOSION_MAX` | 40 | explosion max scale |
| `INITIAL_SCORE_DISPLAY_TIME` | 50 | popup lifetime (frames) |
| `INITIAL_STARFIELD_MAX` | 10 | starfield objects (×4 stars) |
| `BUFFER_OBJECT_COUNT` | 10 | free slots kept besides starfields |
| `SHIELD_DELAY_TICKS` | 7 | frames after release before a new shield |

---

## 13. Ambiguities, quirks and open questions
1. **Starlet size**: `starletBehaviour` loads scale 3 ("tiny starlet") but the MVLC draw routine forces
   scale 6. On hardware the star is therefore scale 6 (~±84 units ×6). Decide whether to match the
   hardware (6, recommended) or the evident intent (3).
2. **Hunter sprite index** uses `ANGLE/32` (16° steps) on a 24-entry (15° step) table → visible
   misalignment of up to ~22°. Faithful port: keep `HunterList[ANGLE>>5]`.
3. **"?" speed** reloads its tick counter from `X_add_delay` instead of `Bonus_add_delay`.
4. **`Bomber_addi`** is loaded but unused; bomber orbit step is hard-coded 1°.
5. **Double `deadEnemies` increment** for Hidden X, Dragon child and gimmick-"?" kills (affects the score
   multiplier) — likely unintended but part of the original scoring.
6. **Shot-explosion stray write** (§6.4) — do not reproduce.
7. **`onShield` stage-1 inner test** disabled by `clra` (§8.1); only the corrected stage-2 test matters.
8. **Bomber reload wrap** when `bomber_delay_start < 25` (phase 2: first shot after 20 frames, then
   251, 226, 201, 200…). Faithful but odd.
9. Spawn angles 720..762 are stored un-normalised (only matters if the port indexes a 360-entry table;
   wrap with `% 720` before lookup).
10. `timerBehaviour`/`gimmikBehaviour` write the intensity DAC while the move to the timer position is
    still running, which on hardware slightly distorts the X position of the timer digits. Use the
    nominal positions given in §7.4/§7.6.
11. Load-adaptive behaviour (`t2_rest`, squad CPU check) has no equivalent on the Pi; recommended
    constants are given in §9.4/§3.8.
12. `README` says starlet bonus is "4 points per second"; the code gives +2 per 1.52 s starting at 4.
13. Hidden X fade formula yields a slight intensity discontinuity at scale 16 (127 → 112 at 15).
