# Release (Android, Unity 2019.4.40 / IL2CPP): decoded gameplay values

Source: `data.unity3d` from the user's own APK (files `level0` = the only scene, `sharedassets0.assets` = prefabs).
Decoder scripts (scratchpad `scripts/`): `decode_all.py` (all game MonoBehaviours plus GameObjects/Transforms/colliders/cameras),
`ngui.py` (NGUI UIRoot/UISprite raw), `disasm.py` + `methods.py` (arm64 disassembly of selected methods, so we know how fields are used).
Raw dumps: scratchpad `mb_dump.txt`, `go_dump.txt`, `dis*.txt`.

**Field layout check:** field names and order come from `release_classes.cs`. Only public fields of the game classes are serialized (no
`[SerializeField]` exists in Assembly-CSharp). For **every** decoded component the byte count consumed matched the raw size exactly
(0 bytes left over, 0 mismatches), so the field-to-value mapping is reliable.

Legend: **[code]** = interpretation confirmed by disassembling the method that uses the field. **[?]** = inferred from name or value, not verified.

---

## 1. Coordinate system, cameras, world size

All gameplay objects live under `UI Root/Camera/Game/...` (NGUI). Everything below is in **NGUI virtual pixels ("px")** unless stated otherwise.

| Object | Values | Meaning |
|---|---|---|
| `UI Root` (UIRoot) | scalingStyle=1 (FixedSize), manualHeight=**720**, minimumHeight=320, maximumHeight=1536 | Screen height is always 720 px virtual |
| `UI Root` Transform | pos (0,0,0), scale **0.0027777778 = 1/360** | 1 Unity world unit = **360 px** |
| `UI Root/Camera` (**the game camera**) | orthographic, size **1.0**, near -10, far 10, depth 1, clearFlags 3 (depth only), cullingMask layer 9, local pos (0,0,0) | Renders the game and HUD. Ortho size 1.0 world = 360 px, so the visible area is **720 px tall**, width = 720 x aspect (16:9 = 1280 px). Origin = screen centre = player |
| `UI Root (3D)` | world pos (100,0,0), scale 1/360, UIRoot FixedSize 720 | Separate root for the starfield, moved to x=100 so the two cameras do not see each other's objects |
| `UI Root (3D)/Camera` | **perspective**, FOV 60, near 0.1, far 4.0 (world), depth 0, clearFlags 2 (solid black), local pos (0,0,-700) | Background starfield camera. Far 4.0 world = 1440 px, which matches the star box depth. The ortho size 5.0 is unused because the camera is perspective |
| `UI Root/Camera/Game`, `.../Game/Player`, `.../Game/EnemySpawner` | all local pos (0,0,0), scale 1 | Player is fixed at the screen centre |
| `GameState.get_ScreenFixedSizeRatio` [code] | = manualHeight / Screen.height | Converts device pixels to px |

**Screen edge** [code `BasicEnemy.SetPositionOnScreenEdge`]: half extents = (Screen.w, Screen.h) x ratio x 0.5, which is (640, 360) at 16:9.
A random side 0..3 is picked: 0 gives x=-hw with random y in [-hh, hh]; 1 gives x=+hw with random y; 2 gives y=+hh with random x; 3 gives y=-hh with random x.
Enemies spawn exactly on the screen border (local position).

### Time scaling [code]
- `CoreBehaviour.GameTimeScale` (static) is set to **1.0** at game start (`GameState.OnStartGame`). While the game is live:
  `GameTimeScale += Time.deltaTime * GameSpeedGrowth` (`GameState.Update`), so it reaches 1.45 after 60 s and 1.9 after 120 s.
- `CoreBehaviour.GameDeltaTime = Time.deltaTime * GameTimeScale`. All enemy movement, attack growth and spawner per-type timers use it.
- `GameState.CurrentGameTime += Time.deltaTime` (unscaled real seconds). The spawner waves are keyed on this value.

---

## 2. GameState (`level0`, GO `GameState`)

| Field | Type | Value | Interpretation |
|---|---|---|---|
| GameSpeedGrowth | float | **0.0075** | GameTimeScale grows by 0.0075 per real second [code] |
| PointsPerSecond | int | **0** | Score added every 1.0 scaled second [code]. It is 0, so there are no passive points |

## 3. PlayerController (`level0`, GO `UI Root/Camera/Game/Player`, tag 6)

| Field | Type | Value | Interpretation |
|---|---|---|---|
| AttackPrefab | PPtr | Attack (prefab `Attack`) | |
| IterationType | enum | **0 = Count** | [code] Each new attack: sides += 1, and after MaxNumSides it wraps back to MinNumSides: **5,6,7,8,5,...** |
| MinNumSides | int | **5** | |
| MaxNumSides | int | **8** | |
| ScoreToIterateAt | int | 10 | Used only by the *AtScore iteration types, so unused here |
| PlayerSidePiecePrefab | PPtr | UISprite `PlayerSideSegment` | |
| PlayerSideAccentPiecePrefab | PPtr | UISprite `PlayerSideAccentSegment` | |
| DeathEffectPrefab | PPtr | PoolableParticleEffect `PlayerDE` | |
| PowerupTimerLabel | PPtr | UILabel `PowerupTimerLabel` | |
| PlayerRadius | float | **22.0** px | [code `SpawnPlayerShape`] Polygon of n sides at radius 22. Side sprite width = 2·R·tan(π/n) + 1 px; first side at -90°, step 360/n (tan inferred) |
| PlayerAccentRadius | float | **26.0** px | Outer accent polygon, built the same way |
| AccentColorChangeRate | float | 0.1 | Colour cycling speed of the accent [?] |
| UseAccent | bool | true | |
| RotationSpeed | float | **5.0** | [code] localEuler.z += 5 · dt · GameTimeScale, i.e. **5°/s** (only while AllowRotation is set) |
| PowerupDurations | List<float> | **[45, 15, 15]** s | Index = PowerupType-1: Shield 45 s, AttackSpeed 15 s, AttackWidth 15 s [?] |
| AttackSpeedPowerupMultiplier | float | **2.0** | Attack expand speed x2 during AttackSpeed [code: the multiplier getter checks the current powerup] |
| AttackWidthPowerupMultiplier | float | **2.0** | Width-growth speed x2 during the AttackWidth powerup (powerup==3) [code] |

`PowerupType`: 0 None, 1 Shield, 2 AttackSpeed, 3 AttackWidth.

Player side prefab `PlayerSideSegment`: UISprite 'BlankSquare', pivot Top, 100x6 px, local pos (0,121), BoxCollider trigger size (100,6), centre (0,-3). The width is overwritten at runtime, so the collider is a thin bar along each side.
`PlayerSideAccentSegment`: same sprite, colour (0.993, 0.515, 1, 1), no collider.

## 4. Attack (prefab `Attack`, layer 9) + NumToKillForMultiplier

| Field | Type | Value | Interpretation |
|---|---|---|---|
| AttackExpandSpeed | float | **1.5** | [code `UpdateAttack`] `_currentAttackPower += 1.5 · mult · GameDeltaTime`, then `localScale = one · _currentAttackPower` |
| AttackWidthExpandSpeed | float | **12.5** | [code] `_currentAttackWidth += 12.5 · mult · GameDeltaTime`. Every piece's sprite height = (int)(width + 6) |
| AttackPiecePrefab | PPtr | AttackPiece `AttackLineSegment` | |
| NumSides | int | 8 (prefab default) | Overwritten at Initialize with the player's current side count [code] |
| Damage | int | **1** | HP removed per hit |
| UseAccent | bool | true | |
| AccentRadiusDifference | float | **4.0** px | Accent ring radius = 120 + 4 |
| MultiEnemyKillMultiplier | List<NumToKillForMultiplier> | [{NumToKill 0, Multiplier 1.0}, {NumToKill 5, Multiplier 1.2}] | Used only when UseSimpleMultiKillMultiplier=false, so unused |
| UseSimpleMultiKillMultiplier | bool | **true** | |
| PerKillMultiplier | float | **0.1** | [code `GetKillMultiplier`] multiplier = 1 + 0.1·(numKilled − 1) |

Attack geometry [code `SpawnAttackPieces`, `PlayerController.SpawnAttack`]:
- The attack polygon has n sides at a **fixed local radius of 120 px** (hard-coded constant 120.0). Side length = 240·tan(π/n) + 1, angles start at -90°, step 360/n. The accent ring sits at radius 120 + 4.
- On spawn the Attack is parented to the player with localScale = **0.01** (hard-coded), localPosition 0, rotation 0.
- Effective ring radius = 120 px x `_currentAttackPower` (it grows by 1.5/s·GameTimeScale, so about +180 px/s at scale 1). Line thickness in local units = width+6, also scaled by the parent scale. **[?]** I did not confirm whether `_currentAttackPower` starts at 0.01 or 0 (the field is private, not serialized).

`AttackLineSegment` prefab: UISprite 'BlankSquare', pivot Top, 100x6, pos (0,121), BoxCollider trigger (100,6) centre (0,-3).
`AttackPiece` has no serialized fields.

## 5. Enemy base classes

`BasicEnemy` fields (serialized in this order): MoveSpeed f, PointValue i, StartHealth i, DeathEffectPrefab PPtr, SpawnSound PPtr(AudioSource), DeathSound PPtr(AudioClip), DamagedColor Color, DamagedSpriteName string, LifeSpan f.
`BasicOrbitEnemy` adds: SpeedPulledToCenter f, MinStartFromEdgeDistance f, MaxStartFromEdgeDistance f.

Movement semantics [code]:
- **BasicEnemy.UpdatePosition** (Fighter, Stealth, Kamakazi, children): `position += normalize(player.worldPos − worldPos) · MoveSpeed · dt · GameTimeScale`. This uses **world** positions, so MoveSpeed is in **world units/s (x360 = px/s)**.
- **BasicOrbitEnemy**: `_currentRadius -= SpeedPulledToCenter · dt · GameTimeScale` (px/s); `_currentAngle ± = MoveSpeed · dt · GameTimeScale` (**degrees/s**, direction random 50/50, wrapped to ±360); localPosition = (r·cos a, r·sin a) (cos/sin order [?]).
- **BasicOrbitEnemy.SetInitialPosition**: put on the screen edge, then `_currentRadius = min(Screen.w, Screen.h)·ratio·0.5 − Random(MinStartFromEdge, MaxStartFromEdge)`, which is **360 px** (both offsets are 0). The angle comes from the edge position.
- `DistanceToPlayer` = distance between localPositions (px).
- LifeSpan −1 means it lives forever.

All enemies: DamagedColor (1,1,1,1). All have a Rigidbody (mass 1, no gravity, non-kinematic, no constraints) and a trigger BoxCollider (z size 0), and are on layer 9, tag 20001.

## 6. Enemy prefabs

| Prefab (class) | MoveSpeed | → px/s or °/s | Points | HP | SpeedPulled ToCenter (px/s) | Sprite (size, colour) | Prefab scale | BoxCollider (local) → effective px | Death FX |
|---|---|---|---|---|---|---|---|---|---|
| FighterEnemy | 0.1 | **36 px/s** homing | 10 | 1 | – | 'Cross' 48x48, white | 1 | 48x48 → **48** | FighterDE |
| FighterEnemy_TutorialDummy | 0.0 | 0 (stationary) | 10 | 1 | – | 'Cross' 48x48 | 1 | 48x48 | FighterDE |
| KamakaziEnemy ("Diver") | 0.3 | **108 px/s** | 15 | 1 | – | 'kamakazi' 48x48, white | 1 | 32x32 → **32** | KamakaziDE |
| StealthFighterEnemy | 0.1 | **36 px/s** homing | 15 | 1 | – | 'Cross' 48x48, colour (0.224,0.224,0.224,0.784) | 1 | 48x48 → **48** | FighterDE |
| KiteEnemy (head) | 25.0 | **25 °/s** orbit | 15 | **2** | **25** | 'Kite' 48x48 | 1 | 48x48 → **48** | KiteDE (legacy) |
| KiteChildEnemy (tail) | 0.3 | **108 px/s** (when homing) | 10 | 1 | – | 'Kite Cross' 48x48 | **0.5** | 32x32 → **16** | KiteChildDE |
| CarrierEnemy | 25.0 | **25 °/s** orbit | 15 | 1 | **8** | 'Circle' 48x48, colour (0.82,0.81,0.82) | 1 | 48x48 → **48** | CarrierDE |
| CarrierChildEnemy | 0.2 | **72 px/s** homing | 20 | 1 | – | 'Circle' 48x48, red (1,0,0) | **0.35** | 32x32 → **11.2** | CarrierChildDE |
| OrbiterEnemy | 25.0 | **25 °/s** | **0** | 1 | 0 | 'Star Orbiter' 48x48 | 1 | 32x32 → **32** | none |
| OrbiterChildEnemy | 0.2 | **72 px/s** homing | 1 | 1 | – | 'Circle' 48x48, red | **0.35** | 32x32 → **11.2** | none |
| OrbiterAlly | 25.0 | **25 °/s** orbit | 0 | 1 | 0 | 'Star Orbiter' 48x48 | 1 | 48x48 → **48** | OrbiterDE |
| PowerupTarget | 0.0 | 0 °/s | 10 | 1 | **65** | 'Power-up' 24x24 | 1 | 23.25x23.25 → **23.25** | PowerUpDE |

(Sprite px size = UISprite width/height x prefab scale. KiteChild draws at 24 px, Carrier/Orbiter child at 16.8 px.)

### Class-specific fields

**StealthFighterEnemy**
| Field | Value | Interpretation |
|---|---|---|
| StealthColor | (0.2235, 0.2235, 0.2235, 0.7843) | Colour while hidden (dark grey) |
| UnstealthColor | (1,1,1,1) | |
| UnstealthDistance | **200** px | [code] Becomes visible when DistanceToPlayer < 200 |
| UnstealthDuration | **0.5** s | Fade/tween time [?] |

**KiteEnemy**
| Field | Value | Interpretation |
|---|---|---|
| KiteFollowerPrefab | KiteChildEnemy | |
| DistanceForFollowersToFollow | **[50, 100]** px | 2 tail segments, following 50 and 100 px behind the head [?] |
| DamagedMoveSpeed | **40** | Orbit speed (°/s) after the head is hit once (HP 2 → 1) [?] |
| (DamagedSpriteName) | 'Kite Damaged' | Sprite after the first hit |

[code] The head orbits like BasicOrbitEnemy, stores the move direction, sets `transform.right = direction`, and adds +45° to the sprite rotation. A tail child moves toward the kite at its follow distance; when the kite dies it homes on the player at 0.3 world/s [?].

**CarrierEnemy**
| Field | Value | Interpretation |
|---|---|---|
| CarrierChildPrefab | CarrierChildEnemy | Projectile/drone it launches |
| SpawnRate | AnimationCurve, 2 keys: (t −0.1299, v 0.3721, slopes 0.0274) and (t 14.914, v 1.4985, slopes 0.2577); pre/post wrap 2 (ClampForever) | [code `SetupTimeToNextSpawn`] Next spawn delay = **1 / SpawnRate(_timeAlive)** (1.0 s if the curve ≤ 0). That is about 2.7 s at birth, falling to about 0.67 s after 15 s, then clamped. Hermite curve, almost linear: rate ≈ 0.372 + 0.0755·t |

**OrbiterEnemy**
| Field | Value | Interpretation |
|---|---|---|
| OrbiterChildPrefab | OrbiterChildEnemy | Projectile it shoots |
| EllipseSquishiness | **170** | [code] Moves on an ellipse around the player: a = startRadius − 170 (≈190 px), b = 2·PlayerRadius·1.2 = **52.8** px (a is clamped to at least b). Radius per angle r = ab / sqrt((b·cos θ)² + (a·sin θ)²), θ relative to the spawn angle. It shoots children at certain angles (`_nextShootAngle`, 180° checks) [?] |

**OrbiterAlly** (friendly; "Let Orbiters live")
| Field | Value | Interpretation |
|---|---|---|
| MinDistance / MaxDistance | **110 / 160** px | [code] Orbit radius = Random(110,160) after it spawns |
| PointsPerTimeFrame | **2** | Points awarded at each tick [code: AddPoints] |
| PointsEverySeconds | **2.0** s | Tick interval (scaled time) |
| PointsIncreasedEveryTimeFrame | **2** | Points per tick go up by 2 each tick: 2, 4, 6, ... [?] |
| EndTutorialHintAfterTimeFrames | 3 | Tutorial only |

**PowerupTarget**
| Field | Value | Interpretation |
|---|---|---|
| PowerupTypeSpawnRates | **[0.4, 0.3, 0.3]** | Weights for Shield / AttackSpeed / AttackWidth |
| MinDistance / MaxDistance | **440 / 1000** px | [code partly] Spawns on the screen edge; radius = Random(…, DistanceToPlayer − 30), clamped. The exact use of 440/1000 is **[?]** |
| SpeedPulledToCenter | 65 px/s | Drifts inward fast; MoveSpeed 0 means no angular motion. The player collects it by touch (the tutorial says "When Power-Ups hit you") |

## 7. EnemySpawner (`level0`, GO `UI Root/Camera/Game/EnemySpawner`)

`EnemyPrefabs` (index → prefab): **0 FighterEnemy, 1 KamakaziEnemy, 2 KiteEnemy, 3 CarrierEnemy, 4 OrbiterAlly, 5 StealthFighterEnemy, 6 PowerupTarget**.
`EnemyGainedPointsLabelPrefab` = EnemyGainedPointsLabel.

Logic [code, partly]:
- `GetCurrentSpawnRates`: searches from the last entry down and returns the first entry with `CurrentGameTime >= AtGameTime` (real seconds since spawning started).
- Per-type timers `_currentTimeSinceSpawns[i] += GameDeltaTime` (`Update`).
- `NextRandomEnemyIndex`: a type is excluded while `LiveEnemyTypeCount >= MaxLiveEnemiesByType[i]` (−1 = unlimited) or while its time since last spawn < `MinTimeBetweenEnemySpawnsByType[i]`. The rest are picked by weighted random from `SpawnRates`, renormalised by (1 − excluded weights).
- `CanSpawnNextGuy`: also checks `MaxLiveEnemies` (−1 = unlimited) against the live child count.
- `SpawnGuyRepeat`: if allowed, spawns, then schedules the next call after `NextRandomEnemySpawnTime` = Random.Range(MinSpawnTime, MaxSpawnTime) s. **[?]** It is probably Invoke-based, i.e. Unity time, *not* scaled by GameTimeScale.

### Wave table (EnemiesSpawnRates, all fields)

SpawnRates (weights) per type:

| # | AtGameTime (s) | Fighter | Kamakazi | Kite | Carrier | OrbiterAlly | Stealth | Powerup | MinSpawnTime | MaxSpawnTime | MaxLiveEnemies |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0.60 | 0.10 | 0 | 0 | 0 | 0 | 0.30 | 1.0 | 2.0 | −1 |
| 1 | 8 | 0.60 | 0.05 | 0.05 | 0.05 | 0.05 | 0.05 | 0.15 | 1.0 | 1.9 | −1 |
| 2 | 15 | 0.60 | 0.05 | 0.05 | 0.05 | 0.05 | 0.05 | 0.15 | 0.9 | 1.5 | −1 |
| 3 | 25 | 0.40 | 0.10 | 0.10 | 0.10 | 0.10 | 0.10 | 0.10 | 0.7 | 1.1 | −1 |
| 4 | 40 | 0.30 | 0.15 | 0.10 | 0.10 | 0.15 | 0.10 | 0.10 | 0.3 | 0.8 | −1 |

MaxLiveEnemiesByType (−1 = unlimited):

| # | Fighter | Kamakazi | Kite | Carrier | OrbiterAlly | Stealth | Powerup |
|---|---|---|---|---|---|---|---|
| 0 | −1 | −1 | −1 | −1 | −1 | −1 | 1 |
| 1 | −1 | −1 | −1 | −1 | 2 | −1 | 1 |
| 2 | −1 | −1 | −1 | −1 | 2 | −1 | 1 |
| 3 | −1 | −1 | −1 | −1 | 2 | −1 | 1 |
| 4 | −1 | −1 | −1 | −1 | 3 | −1 | 1 |

MinTimeBetweenEnemySpawnsByType (s): every wave 0 for all types except Powerup: wave0 **10**, wave1 **11**, wave2 **10**, wave3 **12**, wave4 **10**.

Wave 4 (from 40 s) is the last one and lasts forever. After that only GameTimeScale keeps the difficulty rising.
Note: OrbiterEnemy is **not** in the spawner list. It only enters the game through the ObjectPool, and nothing found here spawns it (no tutorial step either), so in this build it is probably unused **[?]**. KiteChild, CarrierChild and OrbiterChild are spawned by their parents.

## 8. Background / BackgroundStar

**Background** (`UI Root (3D)/Camera/Background`)
| Field | Value | Interpretation |
|---|---|---|
| BackgroundStarPrefab | BackgroundStar | |
| NumStars | **180** | |
| StarLiveArea | BoxCollider `StarSpawnArea` | local pos (0,0,**731**) under Background (under the camera at z −700); size **3350 x 3350 x 1445**, so stars live at z ∈ [8.5, 1453.5] in front of the camera (camera-relative) |
| RandomStarColor1 | (1,1,1,0.494) | Star colour A (white, alpha ~0.5) |
| RandomStarColor2 | (1,0.537,0.537,0.494) | Star colour B (pinkish) |
| NoSpawnDistanceFromCenter | (200, 100) | No star spawns within x ±200, y ±100 of the centre [?] |
| RotateCameraEveryXBeats | **24** | At 120 BPM: every 12 s… |
| RotateThroughXBeats | **8** | …rotate for 8 beats = 4 s… |
| RotateSpeed | **20** | …at 20 °/s (about 80° per rotation) [?] |

**BackgroundStar** (prefab): MoveSpeed **250** (px/s toward the camera along z [?]), PulseMoveSpeed **800** (speed during a music beat pulse [?]). Sprite 'BlankSquare' 4x4, prefab scale 0.2, colour (1,1,1,0.498). When a star passes the camera it goes back to the far end (`MoveStarToBeginning`, FarZ).

## 9. AudioManager (`level0`, GO `AudioManager`)
| Field | Value | Interpretation |
|---|---|---|
| GameMusic | AudioClip 'dubChip' | |
| MenuMusic | AudioClip 'opening' | |
| PauseVolume | 1.0 | |
| CalculatePulseBeats | true | |
| BeatsPerMinute | **120** | Beat every 0.5 s (drives star pulses and camera rotation) |
| PulseTimeAroundBeat | 0.0 | Window around the beat |

Enemy SpawnSound: AudioSource with clip 'Spawn', volume 1, no loop, on every enemy. DeathSound: clip 'Death'.

## 10. CinematicManager (`level0`)
| Field | Value | Interpretation |
|---|---|---|
| SpawnDelay | **0.08** s | Title/attract cinematic spawns one enemy every 0.08 s… |
| SpawnCount | **100** | …100 enemies in total [?] |

## 11. ObjectPool (`UI Root/Camera/ObjectPool`)
NumToAddWhenEmpty = 1. PrefabsToPool (prefab: initial count): Attack 1, AttackLineSegment 16, FighterEnemy 22, KamakaziEnemy 15, KiteEnemy 15, CarrierEnemy 15, OrbiterEnemy 15, StealthFighterEnemy 15, KiteChildEnemy 15, CarrierChildEnemy 15, OrbiterChildEnemy 15, CarrierDE 6, FighterDE 15, KamakaziDE 5, EnemyGainedPointsLabel 25, OrbiterAlly 10, PowerupTarget 2, FighterEnemy_TutorialDummy 2, CarrierChildDE 8, KiteChildDE 8, PlayerDE 1, OrbiterDE 3, PowerUpDE 2, KiteDE 6.
(Gives a feel for the expected maximum on screen: about 22 fighters.)

## 12. UI labels / menus
| Component | Field | Value | Interpretation |
|---|---|---|---|
| EnemyGainedPointsLabel (prefab) | ShowTime | **1.5** s | "+points" popup lifetime |
| ScoreLabel (`UI/Score Label`, pos (−643, 324)) | ScaleTime / ScaleSize | **0.2** s / **1.1** | Punch scale on a score change |
| HighScoreLabel (`UI/High Score Label`, pos (642.7, 324)) | – | no fields | |
| PowerupLabel (`UI/Powerup Label`, pos (0, 68.3)) | – | no fields | |
| SummaryMenu | FadeTime 0.3, WaitAfterDeathForMenu **2.0** s; labels RoundScore (0,97), Highscore (0,19), FriendScore (0,−72) | Game-over screen appears 2 s after death |
| GameMenu / SettingsMenu / CreditsMenu / HelpMenu / PauseButton | FadeTime | 0.3 s | |
| TitleLabel | ColorChangeRate | 0.1 | |
| TutorialLabel (pos (0,−331)) | FadeTime 0.3, BackgroundExtraSpace 16 | |
| SettingsMenu | ActiveAudioColor (1,1,1,1), InactiveAudioColor (1,0,0,1) | |
| ScreenButton | BoxCollider 1324 x 740 | Whole-screen touch area (press & hold = grow attack, release = fire) |

## 13. Tutorial (for reference)
TutorialManager.MinimumMainTutorialID = 10. Chain (each item fires the next with FireDelay 5 s, requirement "all enemies cleared"):
1. Player/TutorialItems (ID 0): label "Press & Hold Anywhere", stops the spawner, spawns FighterEnemy_TutorialDummy at **(300, 100)**.
2. Attack/TutorialItem_ReleaseToFire (ID 1): "RELEASE!" (delay 0.4), attack growth off, rotation off, release off, then release on after 0.7 s.
3. Attack/TutorialItem_ReleaseKill (ID 2): "Destroy enemies before they hit you", two life-tied Fighters at **(450, 50)** and **(450, −50)**, rotation on.
4. 1_FighterTutorial (ID 4): "Fighters are basic, slow moving enemies.", Fighters at t = 0.8 / 1.6 / 2.4 / 3.2 s.
5. 2_TriangleTutorial (ID 5): "Divers move quickly. Watch out!", Kamakazi at 1.6 / 3.2 s.
6. 3_StealthTutorial (ID 6): "Stealth Fighters are nearly invisible until they're next to you!", Stealth at 0.8 / 1.6 / 2.4 / 3.2 s.
7. 4_KiteTutorial (ID 7): "Destroy the tail of Kites before the head.", Kites at 2.0 / 5.0 s.
8. 5_CarrierTutorial (ID 8): "The longer Carriers live, the faster they deploy.", Carriers at 1.0 / 5.0 s.
9. 6_OrbiterTutorial (ID 9): "Let Orbiters live. You'll be rewarded.", OrbiterAlly at 1.5 / 3.0 s.
10. 7_PowerupTutorial (ID 10): "When Power-Ups hit you, you become stronger.", PowerupTarget at 0.5 / 2.0 s; at 5 s "Get as many points as you can!" (shown 5 s), reset game time, reset score, reset orbiters, start the spawner.
(Attack/TutorialItem_ReleaseMultiKill ID 3 has no actions.) Spawn position (0,0,0) means "use the normal SetInitialPosition".

## 14. Open questions / not verified
- Initial values of the private fields `_currentAttackPower` / `_currentAttackWidth` (scale starts at 0.01 via SpawnAttack; the power probably starts at 0.01 too). The attack lifetime, maximum size and the release/fire rules are not serialized.
- How the player side colliders and AttackPiece triggers interact (kill when an enemy touches the ring line; player hit when an enemy touches a side). Collider sizes: sides and pieces are thin bars of 6 px x side length.
- Exact OrbiterEnemy shooting rule, how Kite DamagedMoveSpeed is used, how PowerupTarget uses Min/MaxDistance, and whether `SpawnGuyRepeat` uses scaled time. These are only partly disassembled.
- Whether OrbiterEnemy is spawned anywhere: it is not in the EnemySpawner list.
- UnstealthDuration, AccentColorChangeRate and Background rotation details are inferred from field names only.
