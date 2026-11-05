# Release (Android) – Player, Attack, Spawner, Scoring: rules reconstructed from libil2cpp.so

Source: `libil2cpp.so` (arm64, Unity 2019.4.40 IL2CPP) from the user's own APK, disassembled with capstone.
Every function below was read in full. References are **`RVA`** = virtual address = file offset in `libil2cpp.so`
(code segment maps 1:1), as listed by Il2CppDumper in `dump.cs`/`script.json`.
Serialized values come from `docs/android_values.md` (decoded by another agent) and are repeated here only where a rule needs them.

Tools (scratchpad `scripts/`): `pc_dis.py` + `pc_d.sh` (disassembler with BL names, GOT/RELA resolution of metadata
and string literals, field-offset annotation), `pc_plt.py` (PLT stub → libc symbol), `pc_rd.py` (rodata constants),
`pc_time.py` (TimeManager). Raw listings: scratchpad `pc_out/*.txt`.

Conventions
- **px** = NGUI virtual pixels. Screen height is 720 px, the origin is the player at the screen centre, and 1 Unity world unit = 360 px.
  All gameplay transforms used here (player, attack, enemies) are local positions in px under `UI Root/Camera/Game`.
- **dt** = `Time.deltaTime` (real seconds per rendered frame). **GTS** = static `CoreBehaviour.GameTimeScale`.
  **GameDeltaTime = dt · GTS** (`CoreBehaviour.get_GameDeltaTime` 0x4B9368).
- All gameplay logic runs in **`Update`** (once per rendered frame, variable dt). The game never calls `Application.targetFrameRate`
  in the code read here (`GameState.SetQualitySettings` 0x4C3A6C is an empty `ret`), so the Android default (normally 30 fps) applies. **[?]**
- Hit detection uses **Unity 3D trigger colliders** (`OnTriggerEnter`/`OnTriggerExit`). Physics runs at the fixed step
  **0.02 s** (TimeManager `Fixed Timestep` = 0.02, read from `data.unity3d`). Enemies have a non-kinematic Rigidbody without gravity.
  Their transforms are moved in Update, and `AutoSyncTransforms` = true.
- Messages (`GameMessage`): 0 GAME_OVER, 1 START_GAME, 2 ENEMY_DESTROYED, 3 SCORE_UPDATED, 4 HIGHSCORE_UPDATED, 7 GAME_PAUSED,
  12 EARNED_POWERUP, 14 ALLOW_ATTACK_GROWTH, 16 RESET_ORBITER. `CoreBehaviour.SendMessage` (0x4BD5A0) is a synchronous broadcast
  to every registered handler through `MessageManager`.

---

## 0. Core rules in brief

- **One button.** While you hold it, a polygon ring grows outward from the player. When you let go, the ring fires. Every enemy
  touching the ring at that moment loses `Damage` = 1 HP, and those that reach 0 HP die.
- The player is a regular **n-gon** with n = 5, 6, 7, 8, 5, … The side count goes up by 1 after **every fired attack**
  (`IterationType` = Count). The ring always has the player's current side count and is parented to the player,
  so it rotates with it.
- The player rotates constantly at 5°/s · GTS.
- There are **no lives.** One enemy touching a player side ends the game, unless a Shield is active: the shield absorbs exactly one hit.
- Score: killed enemy = `PointValue · (1 + 0.1·(k−1))`, truncated to int, where k = enemies killed by the same release.
  OrbiterAlly ("star") pays a growing stream of points while it lives: 4, 6, 8, … every 2 scaled seconds.
- Difficulty: GTS = 1 + 0.0075·t (t = real seconds of play). GTS speeds up everything. The spawner table also switches at
  t = 8/15/25/40 s.

---

## 1. Input – `ScreenButton` (full-screen NGUI collider 1324×740)

`ScreenButton.OnPress(bool isPressed)` – RVA **0x544ACC** (NGUI calls it on touch down and touch up)
```c
void ScreenButton_OnPress(bool pressed) {
    if (GameState.IsPaused) goto store;               // IsPaused = _prePauseGameTimeScale > 0   (0x4BC3B0)
    if (PC.DeadDuringTutorial) goto store;
    if (PC.GameIsLive) {
        if (pressed)              PC.SpawnAttack();    // touch down: start a new attack (fires any old one first)
        else if (PC.AllowRelease) PC.EndAttack();      // touch up: fire
    } else {                                           // not live: restart by tap
        if (GameState.SummaryScreenOpen) goto store;   // ignored while the summary menu is open
        if (pressed) _hasPressedDownForRestart = true;
        else if (_hasPressedDownForRestart) SendMessage(START_GAME, null);
    }
store:
    IsPressed = pressed;
}
// ScreenButton.OnStartGame (0x544CB4): _hasPressedDownForRestart = false;
```
Note: a full tap (down and up) while not live starts a game. The summary menu opens 2 s after death
(`SummaryMenu.WaitAfterDeathForMenu` = 2.0), so a tap inside those 2 s restarts at once **[?, depends on menu timing]**.

---

## 2. PlayerController

Fields used (serialized values): MinNumSides **5**, MaxNumSides **8**, IterationType **0 (Count)**, ScoreToIterateAt 10 (unused),
PlayerRadius **22**, PlayerAccentRadius **26**, RotationSpeed **5.0**, UseAccent true, AccentColorChangeRate 0.1,
PowerupDurations **[45, 15, 15]**, AttackSpeedPowerupMultiplier **2.0**, AttackWidthPowerupMultiplier **2.0**.
Constructor defaults (0x64532C, overwritten by the serialized data): Min 4, Max 8, ScoreToIterateAt 10, radius 22/24, multipliers 1.2.

### 2.1 Lifecycle
```c
void PC_Awake() /*0x643874*/ {
    GameIsLive = false; DeadDuringTutorial = false;
    AllowRotation = true; AllowRelease = true;         // strh 0x0101 at +0x98
    RegisterHandler(GAME_OVER,  OnGameOver);
    RegisterHandler(START_GAME, OnStartGame);
    // CurrentNumSides starts at 0 (field default)
}
void PC_Update() /*0x643958*/ { UpdateAccent(); UpdateRotation(); UpdatePowerup(); }

void PC_OnStartGame(obj) /*0x643EA4*/ {
    if (_currentAttack) ObjectPool.ReturnObject(_currentAttack);
    GameIsLive = true;
    SetupNextAttack();                  // first call: CurrentNumSides==0 -> SetupFirstAttack -> Min (5), spawns the shape
    FireTutorialItem(child tutorial item 0) if present;
}
void PC_OnGameOver(object fireEffect /*bool*/) /*0x6441B4*/ {
    if (_currentAttack) ObjectPool.ReturnObject(_currentAttack);   // the held attack is discarded, NOT fired
    GameIsLive = false;
    if (_currentPowerup == Shield) StopShield();
    _currentPowerup = None; _currentPowerupTimer = -1.0f;
    DestroyPlayerShape();
    if ((bool)fireEffect) FireDeathEffect();          // particle at player position
}
```
`CurrentNumSides` is **not reset** between games. Game 2 starts at (last side count + 1, wrapped).

### 2.2 Rotation – `UpdateRotation` 0x643B70
```c
if (AllowRotation)
    transform.localEulerAngles += Vector3(0, 0, RotationSpeed * dt * GTS);   // +5 deg/s · GTS, counter-clockwise (Unity +z)
```
The rotation is **not** gated on GameIsLive. It runs in menus too, because GTS stays 1 there.

### 2.3 Side count iteration – `SetupNextAttack` 0x643F88, `SetupFirstAttack` 0x64522C
```c
void SetupFirstAttack() {
    switch (IterationType) {
      case Count:         CurrentNumSides = MinNumSides; break;
      case Random:        CurrentNumSides = Random.Range(MinNumSides, MaxNumSides - MinNumSides + 2); break; // literal [?bug]
      case CountAtScore:  CurrentNumSides = MinNumSides; _nextScoreForSideIteration = ScoreToIterateAt; break;
      case RandomAtScore: CurrentNumSides = Random.Range(Min, Max - Min + 2); _nextScore = ScoreToIterateAt; break;
    }
}
void SetupNextAttack() {
    int old = CurrentNumSides;
    if (old <= 0) { SetupFirstAttack(); old = 0; }
    else switch (IterationType) {
      case Count:   if (++CurrentNumSides > MaxNumSides) CurrentNumSides = MinNumSides; break;   // USED: 5,6,7,8,5,...
      case Random:  CurrentNumSides = Random.Range(Min, Max - Min + 2); break;
      case CountAtScore:
        if (GameState.CurrentScore >= _nextScoreForSideIteration) {
            if (++CurrentNumSides > Max) CurrentNumSides = Min;
            _nextScoreForSideIteration += ScoreToIterateAt; }
        break;
      case RandomAtScore: if (score >= _next) { CurrentNumSides = Random.Range(Min, Max-Min+2); _next += ScoreToIterateAt; } break;
    }
    if (CurrentNumSides != old) SpawnPlayerShape(CurrentNumSides);
}
```
SetupNextAttack is called at game start, after **every fired attack** (`EndAttack`), and after `KillAllEnemies`.
So the polygon changes shape as soon as you release.

### 2.4 Player shape – `SpawnPlayerShape(n)` 0x644C5C
```c
DestroyPlayerShape();                       // 0x6442E8: Destroy all child UISprites, clear lists
float t = tanf(PI / n);                     // PLT 0x2F6910 = tanf
for (i = 0; i < n; i++) {
    float a = -90 + i * (360.0f / n);       // degrees, local to the rotating player
    for (k = 0; k < (UseAccent ? 2 : 1); k++) {
        R      = k ? PlayerAccentRadius : PlayerRadius;          // 26 : 22
        prefab = k ? PlayerSideAccentPiecePrefab : PlayerSidePiecePrefab;
        s = Instantiate(prefab); s.parent = player; s.localScale = 1;
        s.localPosition = (-R * sinf(a·DEG2RAD),  R * cosf(a·DEG2RAD), 0);   // a=-90 -> (R, 0)
        s.localEulerAngles = (0, 0, a);
        s.depth = k ? 8 : 9;
        s.width = (int)(2 * R * t + 1);       // side length of an n-gon with apothem R, +1 px overlap
        if (k == 0 && _currentPowerup == Shield) s.color = green;
        (k ? _currentSideAccents : _currentSides).Add(s);
    }
}
```
So R is the **apothem** (centre to side midpoint): 22 px for the solid polygon, 26 px for the accent outline.
The side sprite `PlayerSideSegment` is 6 px high with pivot Top, so it extends inward from radius R. `autoResizeBoxCollider` = 1,
so its trigger BoxCollider is the same width × 6 bar. Only the main sides have a collider (`PlayerControllerSide`). The accent has none.

### 2.5 Being hit – `PlayerControllerSide.OnTriggerEnter` 0x645450, `PC.OnDamage` 0x64452C
```c
void Side_OnTriggerEnter(Collider other) {
    BasicEnemy e = other.gameObject.GetComponent<BasicEnemy>();
    if (PC.GameIsLive && e != null && !e.HasHitPlayer) PC.OnDamage(e);
}
void PC_OnDamage(BasicEnemy e) {
    e.HasHitPlayer = true;                                  // each enemy can hit only once
    PowerupTarget p = e.GetComponent<PowerupTarget>();
    if (p != null) {                                        // a power-up touching you = collect it
        SendMessage(EARNED_POWERUP, PowerupTarget.GetPowerupString(p.CurrentPowerupType)); // "Shield"/"Attack Speed"/"Attack Width"
        PC.AddPowerup(p.CurrentPowerupType);
        e.Kill(null, fromKill=false);                       // no death FX/label, but ENEMY_DESTROYED -> GameState adds points (see 4)
    } else if (_currentPowerup == Shield) {                 // shield absorbs exactly one hit
        StopShield(); _currentPowerup = None; _currentPowerupTimer = -1;
        e.Kill(null, fromKill=true);                        // enemy dies normally: FX, "+points" label, points
    } else if (TutorialManager.HasCompletedMainTutorial()) {
        SendMessage(GAME_OVER, (object)true);               // => game over, no lives
    } else OnTutorialDeath();
}
```
Tutorial death (`OnTutorialDeath` 0x644824): EndAttack, DeadDuringTutorial = true, death FX, destroy the shape, show the label
"You die if an enemy reaches you.", then `Invoke("OnTutorialDeathComplete", 5.0)` (0x644984), which calls KillAllEnemies and
clears DeadDuringTutorial. `KillAllEnemies` (0x6449A8): SpawnAttack, Attack.KillEnemies(all live enemies), EndAttack, SetupNextAttack.

### 2.6 Attack control – `SpawnAttack` 0x645000, `EndAttack` 0x6448E4
```c
void SpawnAttack() {                       // touch down
    if (_currentAttack != null) EndAttack();               // a still-held attack is fired first
    _currentAttack = ObjectPool.GetObject(AttackPrefab);   // -> Attack.Initialize (3.1)
    _currentAttack.transform.parent = player.transform;    // rotates with the player
    localPosition = 0; localScale = (0.01, 0.01, 0.01); localEulerAngles = 0;
    GameState.AttacksCount++;
}
void EndAttack() {                         // touch up
    if (_currentAttack == null) return;
    _currentAttack.FireAttack();           // 3.3
    _currentAttack = null;
    SetupNextAttack();                     // the side count advances
}
```
There is **no cooldown and no minimum hold time** in the code. You can tap as fast as you like, and each tap fires a tiny ring.

### 2.7 Power-ups – `AddPowerup` 0x64476C, `UpdatePowerup` 0x643CB8, `EndPowerup` 0x643E6C, `StartShield` 0x644A54, `StopShield` 0x644B58
```c
void AddPowerup(PowerupType t) {
    if (_currentPowerup != None) { if (_currentPowerup == Shield) StopShield(); _currentPowerup = None; _timer = -1; }
    _currentPowerup = t;
    _currentPowerupTimer = PowerupDurations[t - 1];        // Shield 45 s, AttackSpeed 15 s, AttackWidth 15 s
    if (t == Shield) StartShield();                        // all main side sprites -> Color.green
}
void UpdatePowerup() {
    if (_timer > 0) {
        _timer -= dt * GTS;                                // scaled time
        PowerupTimerLabel.text = ((int)ceilf(_timer)).ToString();
    }
    if (_timer < 0) {                                      // also true for the idle value -1 (cheap no-op)
        PowerupTimerLabel.text = ""; _timer = 0;
        if (_currentPowerup == Shield) StopShield();       // sides back to white
        _currentPowerup = None; _timer = -1;
    }
    PowerupTimerLabel.transform.eulerAngles = 0;           // keep the label upright on the rotating player
}
```
Only one power-up is active at a time. A new one replaces the old one and restarts the timer.
Effects:
- **Shield (1):** the player turns green and the next enemy contact kills that enemy instead of you (that enemy scores).
  The shield then ends immediately. Otherwise it lasts 45 scaled seconds.
- **AttackSpeed (2):** ring expansion speed × 2 (`Attack.get_CurrentAttackSpeedMultiplier` 0x4B8CA4).
- **AttackWidth (3):** ring thickness growth × 2 (`Attack.get_CurrentAttackWidthSpeedMultiplier` 0x4B8D68).

Accent colour (`UpdateAccent` 0x643984, cosmetic): read the HSV of the accent colour, H += AccentColorChangeRate · dt · GTS
(wraps to 0 at H ≥ 1), and write it back to all accent sprites. The attack's accent ring copies `PC.CurrentAccentColor`.

---

## 3. Attack and AttackPiece

Serialized (prefab `Attack`): AttackExpandSpeed **1.5**, AttackWidthExpandSpeed **12.5**, Damage **1**, UseAccent true,
AccentRadiusDifference **4**, UseSimpleMultiKillMultiplier **true**, PerKillMultiplier **0.1**,
MultiEnemyKillMultiplier [{0, 1.0}, {5, 1.2}] (unused because the simple mode is on). NumSides is overwritten at spawn.
Constructor defaults (0x4BA800): 0.3 / 0.1 / sides 4 / damage 1 / accent diff 4 / per-kill 0.1.

### 3.1 Spawn – `Attack.Initialize` 0x4BA3A0, `SpawnAttackPieces` 0x4B9D3C, `Reset` 0x4BA53C
```c
void Attack_Reset() { UnregisterHandlers(); DestroyAttackPieces(); _currentAttackPower = 0; _currentAttackWidth = 0; AllowAttackGrowth = true; }
void Attack_Initialize() {
    NumSides = PC.CurrentNumSides;            // same polygon as the player at the moment of touch-down
    SpawnAttackPieces();
    RegisterHandler(ALLOW_ATTACK_GROWTH, OnAllowAttackGrowth);   // tutorial: AllowAttackGrowth = (bool)arg
}
void SpawnAttackPieces() {
    n = NumSides; t = tanf(PI / n);
    for (i = 0; i < n; i++) {
        a = -90 + i * 360.0f / n;
        for (k = 0; k < (UseAccent ? 2 : 1); k++) {
            R = k ? 120 + AccentRadiusDifference : 120;           // 120 is a hard-coded constant (rodata 0x9F1BB4)
            p = ObjectPool.GetObject(AttackPiecePrefab); p.parent = attack; p.localScale = 1;
            p.localPosition = (-R*sinf(a), R*cosf(a), 0); p.localEulerAngles = (0,0,a);
            p.depth = k ? 50 : 51;
            p.width = (int)(2 * R * t + 1);                       // 240·tan(pi/n)+1 for the main ring
            (k ? _currentAccentPieces : _currentAttackPieces).Add(p);
        }
    }
}
```
Both rings have the same orientation as the player polygon (same −90° start, same parent rotation).
The ring is concentric with the player and **parallel** to its sides.

### 3.2 Growth – `Attack.Update` 0x4B8E2C, `UpdateAttack` 0x4B8F04, `UpdateAccent` 0x4B91E0
```c
void Attack_Update() {
    if (PC.GameIsLive && AllowAttackGrowth && !PC.DeadDuringTutorial) { UpdateAttack(); UpdateAccent(); }
}
void UpdateAttack() {
    speedMul = AttackExpandSpeed      * (PC.CurrentPowerup == AttackSpeed ? PC.AttackSpeedPowerupMultiplier : 1);
    widthMul = AttackWidthExpandSpeed * (PC.CurrentPowerup == AttackWidth ? PC.AttackWidthPowerupMultiplier : 1);
    _currentAttackPower += speedMul * GameDeltaTime;          // starts at 0 (Reset)
    _currentAttackWidth += widthMul * GameDeltaTime;          // starts at 0
    transform.localScale = Vector3.one * _currentAttackPower; // the WHOLE ring object is scaled
    foreach (p in attackPieces + accentPieces) p.height = (int)(_currentAttackWidth + 6);   // local px, before scaling
}
// UpdateAccent: every accent piece colour = PC.CurrentAccentColor
```
Geometry in screen px (P = power, W = width):
- outer apothem of the main ring = **120·P**. The piece pivot is Top and the sprite extends **inward**, so the band covers apothem
  **[ (120 − (W+6))·P , 120·P ]**, and its thickness is (W+6)·P.
- the accent ring sits at 124·P, also extending inward by (W+6)·P. It uses the same AttackLineSegment prefab, so it has a
  trigger collider too (see the note below).
- P grows by 1.5·GTS per second (radius +180 px/s·GTS), and W by 12.5·GTS per second.
  Example at GTS 1: after 0.5 s, radius 90 px and band 12.25·0.75 ≈ 9 px; after 1 s, radius 180 px and band 18.5·1.5 ≈ 28 px;
  after 2 s, radius 360 px (screen half-height) and band 31·3 = 93 px.
- **There is no maximum size and no time limit.** The ring grows as long as the button is held. Once it is larger than the screen
  it can no longer hit anything on screen.
- In the first frame after SpawnAttack the scale is the hard-coded 0.01. From the first Update on it is P (≈ 1.5·dt).

Note on the accent ring collider: both rings use the same `AttackLineSegment` prefab, which has a trigger BoxCollider and
`autoResizeBoxCollider` = 1. So the accent pieces also register enemies, and the effective hit band is the union
[ (120−(W+6))·P , 124·P ] (4 px · P wider than the main ring). The accent pieces are added to `_currentAccentPieces`, and
`FireAttack` collects enemies from **every** AttackPiece child, accent included (see 3.3). **Confirmed by the code path; the
collider being active on the accent instance is inferred from the prefab.**

### 3.3 Hit registration – `AttackPiece.OnTriggerEnter` 0x4BA8FC, `OnTriggerExit` 0x4BAA88, `OnGameOver` 0x4BAB44, `Reset` 0x4BAC1C
```c
void Piece_OnTriggerEnter(Collider other) {
    if (other.gameObject.GetComponent<BasicEnemy>() != null && !_internalEnemies.Contains(other.gameObject)) {
        _internalEnemies.Add(other.gameObject);
        /* tutorial item 1 on the parent attack, if present */
    }
}
void Piece_OnTriggerExit(Collider other) { _internalEnemies.Remove(other.gameObject); }
// OnGameOver / Reset: _internalEnemies.Clear();  Reset also sets the sprite colour to white.
```
So each piece keeps the set of enemies **currently overlapping** it (physics step 0.02 s). This is plain collider overlap of the
rotated rectangle "side bar" with the enemy's BoxCollider. There is no distance or polygon test in game code.

### 3.4 Release – `Attack.FireAttack` 0x4B9458, `KillEnemies` 0x4B991C, `GetKillMultiplier` 0x4B9B78
```c
void FireAttack() {
    List<GameObject> hit = new();
    foreach (AttackPiece p in GetComponentsInChildren<AttackPiece>())       // main AND accent pieces
        foreach (GameObject g in p.CurrentEnemies) {
            if (!hit.Contains(g)) hit.Add(g);
            BasicEnemy e = g.GetComponent<BasicEnemy>();
            if (e.LifeTied != null && !hit.Contains(e.LifeTied.gameObject)) hit.Add(e.LifeTied.gameObject); // linked enemy dies too
        }
    List<BasicEnemy> dead = new();
    foreach (g in hit) {
        e = g.GetComponent<BasicEnemy>();
        e.CurrentHealth -= Damage;                 // Damage = 1
        e.OnDamaged(this);                         // virtual: damaged colour/sprite; KiteEnemy overrides (speed change)
        if (e.CurrentHealth <= 0) dead.Add(e);
    }
    KillEnemies(dead);
    ObjectPool.ReturnObject(this);                 // the ring disappears instantly (no expanding shock-wave after release)
}
void KillEnemies(List<BasicEnemy> list) {
    float m = GetKillMultiplier(list.Count);
    foreach (e in list) { e.CurrentMultiKillMultiplier = m; e.Kill(this, fromKill=true); }
    /* tutorial items, achievements: count==1 -> tutorial item 2, count>=2 -> item 3;
       ReportProgress(16, count/15*100); if AttackWidth active also ReportProgress(17, ...) */
}
float GetKillMultiplier(int n) {
    if (UseSimpleMultiKillMultiplier) return 1 + PerKillMultiplier * (n - 1);      // USED: 1.0, 1.1, 1.2, ...
    float m = 1;
    for (i = MultiEnemyKillMultiplier.Count - 1; i >= 0; i--)
        if (MultiEnemyKillMultiplier[i].NumToKill <= n) { m = MultiEnemyKillMultiplier[i].Multiplier; break; }
    return m;
}
```
- An enemy hit but not killed (Kite head with 2 HP) does not count toward k and is not given a multiplier.
- **Friendly objects are hit too.** OrbiterAlly (HP 1) dies if it is in the band at release, which ends its point stream.
  PowerupTarget (HP 1) dies if shot: you get its 10 points, but **no power-up**.

---

## 4. Enemy death and scoring

### 4.1 `BasicEnemy.Kill(Attack murderer, bool fromKill = true)` 0x4B9C7C
```c
SendMessage(ENEMY_DESTROYED, gameObject);    // -> GameState.OnEnemyDestroyed (score) and EnemySpawner.OnEnemyDestroyed (bookkeeping)
if (fromKill) OnDeath(murderer);             // virtual
ObjectPool.ReturnObject(this);
```
`BasicEnemy.OnDeath` 0x4BD400: spawn DeathEffectPrefab at its position, then `ShowPointLabel((int)(PointValue * CurrentMultiKillMultiplier))`
(virtual. PowerupTarget overrides it with an empty function, 0x645F9C), then play DeathSound.
`BasicEnemy.Reset` 0x4BDB5C: CurrentHealth = StartHealth, CurrentMultiKillMultiplier = 1.0, colour and sprite restored, LifeTied = null, _timeAlive = 0.
`BasicEnemy.Initialize` 0x4BDAC0: SetInitialPosition (virtual), spawn sound, HasHitPlayer = false, AllowMovement = true.
`BasicEnemy.Update` 0x4BCDCC: `if (PC.GameIsLive && AllowMovement) { UpdatePosition(); UpdateTimeAlive(); }`, so everything freezes after game over.
`UpdateTimeAlive` 0x4BCE74: `_timeAlive += GameDeltaTime; if (LifeSpan > 0 && _timeAlive >= LifeSpan) Kill(null, false);`
(LifeSpan = −1 for every prefab, so nothing expires).

### 4.2 `GameState.OnEnemyDestroyed(GameObject g)` 0x4C4A08
```c
BasicEnemy e = g.GetComponent<BasicEnemy>();
CurrentScore += (int)(e.PointValue * e.CurrentMultiKillMultiplier);  // fires SCORE_UPDATED
_enemyKillCounts[e.EnemyTypeName]++;                                  // summary/achievements
```
Points are added for **every** ENEMY_DESTROYED, including: a power-up collected by touching it (PowerupTarget PointValue 10, ×1),
an enemy that hit the shield (×1), and KillAllEnemies in the tutorial. Points per kill (multiplier ×1):
Fighter 10, Kamakazi 15, Stealth 15, Kite head 15, KiteChild 10, Carrier 15, CarrierChild 20, OrbiterChild 1, OrbiterAlly 0, PowerupTarget 10.
Example: 3 Fighters with one release give (int)(10·1.2) = 12 each, 36 in total. The truncation matters: Kamakazi ×1.1 = 16.

### 4.3 "+N" label – `BasicEnemy.ShowPointLabel` 0x4BD7E0, `EnemyGainedPointsLabel.SetPoints` 0x4BD9FC, `Initialize` 0x4BFDEC
The label is taken from the pool (`EnemySpawner.EnemyGainedPointsLabelPrefab`). It is parented to the enemy's grandparent and given
the enemy's localScale and world position. Its text is `"+" + N` (or "-" for negative N). Then `TweenAlpha.Begin(go, ShowTime = 1.5 s, 0)`
fades it out, and `OnFadeComplete` returns it to the pool. It does not move.

### 4.4 GameState (time, passive score, difficulty) – `Update` 0x4C3A70, `OnStartGame` 0x4C4854, `OnGameOver` 0x4C49A0
```c
void GS_Update() {
    if (!PC.GameIsLive) return;
    GTS += dt * GameSpeedGrowth;                       // 0.0075/s  -> GTS = 1 + 0.0075·t
    _timeToNextPointSecond -= dt * GTS;
    if (_timeToNextPointSecond <= 0) { CurrentScore += PointsPerSecond; _timeToNextPointSecond = 1.0; }  // PointsPerSecond = 0
    CurrentGameTime += dt;                             // real seconds of live play (waves are keyed on this)
}
void GS_OnStartGame() { CurrentScore = 0; _timeToNextPointSecond = 1.0; _prePauseGameTimeScale = 0; GTS = 1.0;
                        _enemyKillCounts.Clear(); _powerupsEarned = new int[4]; AttacksCount = 0; UnstealthedStealthKills = 0;
                        CurrentGameTime = 0; }
void GS_OnGameOver()  { TrySetHighScore(CurrentScore) /*PlayerPrefs "HIGH_SCORE", HIGHSCORE_UPDATED*/; ReportAchievementProgress(); GTS = 1.0; }
// OnEarnedPowerup (0x4C4B80): _powerupsEarned[GetPowerupType(str)]++  (statistics only)
// AddPoints(p) (0x4C3C9C): CurrentScore += p; plus achievements (score >= 10000 without any OrbiterAlly kill; score >= 5000 with <= 50 attacks)
// PauseGame (0x4C3E20): if GTS > 0 { _prePause = GTS; GTS = 0; PC.GameIsLive = false; SendMessage(GAME_PAUSED, true); }
// ResumeGame (0x4C3F4C): if GTS <= 0 { GAME_PAUSED(false); OPEN_SETTINGS(false); title; GTS = _prePause; _prePause = 0; PC.GameIsLive = true; }
```
Everything that uses GameDeltaTime or dt·GTS speeds up together: enemy movement, ring growth (radius and thickness),
player rotation, power-up timers, OrbiterAlly ticks, Carrier fire rate (via _timeAlive), and the spawner's per-type cooldowns.
**Not** scaled: CurrentGameTime (wave switching) and the spawn interval (`Invoke`, real time).

---

## 5. EnemySpawner

Prefab index: 0 Fighter, 1 Kamakazi, 2 Kite, 3 Carrier, 4 OrbiterAlly, 5 StealthFighter, 6 PowerupTarget.
Wave table: see `android_values.md` §7 (AtGameTime 0/8/15/25/40 s, weights, MinSpawnTime/MaxSpawnTime, MaxLiveEnemies −1,
MaxLiveEnemiesByType: OrbiterAlly 2–3 and Powerup 1, MinTimeBetween: Powerup 10–12 s).

```c
void ES_Awake() /*0x4C010C*/ {
    CheckValidRates(); ResetLiveEnemies();
    SpawningEnabled = true;
    _currentTimeSinceSpawns = [99999] * EnemyPrefabs.Count;   // only here, NOT reset per game
    RegisterHandler(START_GAME, OnStartGame); RegisterHandler(GAME_OVER, OnGameOver); RegisterHandler(ENEMY_DESTROYED, OnEnemyDestroyed);
}
void ES_Update() /*0x4C0968*/ { for (i) _currentTimeSinceSpawns[i] += GameDeltaTime; }   // runs always, also when not live
void ES_OnStartGame() /*0x4C0A20*/ { RemoveEnemies(); Invoke("SpawnGuyRepeat", 0.1f); }   // first spawn attempt 0.1 s after start
void ES_OnGameOver()  /*0x4C0A84*/ { CancelInvoke(); ResetLiveEnemies(); }   // enemies stay frozen on screen until the next start
void RemoveEnemies()  /*0x4BEF9C*/ { ResetLiveEnemies(); foreach (e in GetAllLiveEnemies) ObjectPool.ReturnObject(e); } // no points
void ES_OnEnemyDestroyed(go)       { TryRemoveEnemy(go.GetComponent<BasicEnemy>()); }   // _liveEnemies[typeName].Remove(e)

void SpawnGuyRepeat() /*0x4C0C1C*/ {
    if (CanSpawnNextGuy()) SpawnRandomGuy();
    Invoke("SpawnGuyRepeat", NextRandomEnemySpawnTime());      // Random.Range(MinSpawnTime, MaxSpawnTime) of the CURRENT wave, real seconds
}
EnemySpawnRates GetCurrentSpawnRates() /*0x4C1734*/ {
    for (i = EnemiesSpawnRates.Count - 1; i >= 0; i--)
        if (GameState.CurrentGameTime >= EnemiesSpawnRates[i].AtGameTime) return EnemiesSpawnRates[i];
    LogWarning(...); return null;
}
bool TypeBlocked(r, i) {
    if (i < r.MinTimeBetweenEnemySpawnsByType.Count && r.MinTimeBetween[i] > _currentTimeSinceSpawns[i]) return true;
    if (i < r.MaxLiveEnemiesByType.Count && r.MaxLiveEnemiesByType[i] >= 0
        && LiveEnemyTypeCount(EnemyPrefabs[i]) >= r.MaxLiveEnemiesByType[i]) return true;
    return false;
}
bool CanSpawnNextGuy() /*0x4C0C8C*/ {
    if (!SpawningEnabled || GameState.IsPaused) return false;
    r = GetCurrentSpawnRates();
    for (i = 0; i < EnemyPrefabs.Count; i++)
        if (!TypeBlocked(r, i)) {                              // at least one type available
            if (r.MaxLiveEnemies < 0) return true;
            return r.MaxLiveEnemies > GetComponentsInChildren<BasicEnemy>().Length;   // includes children (KiteChild, ...)
        }
    return false;
}
int NextRandomEnemyIndex() /*0x4C0F08*/ {
    r = GetCurrentSpawnRates(); excluded = []; float total = 1.0f;
    for (i < r.SpawnRates.Count) if (TypeBlocked(r, i)) { excluded.Add(i); total -= r.SpawnRates[i]; }
    float x = Random.Range(0f, total), acc = 0;
    for (i < r.SpawnRates.Count) if (!excluded.Contains(i)) { acc += r.SpawnRates[i]; if (x < acc) return i; }
    LogWarning("Random enemy index didn't find correct spawn rate..."); return -1;
}
PoolableBehaviour SpawnRandomGuy() /*0x4BF3C8*/ { return SpawnGuy(EnemyPrefabs[NextRandomEnemyIndex()]); }
PoolableBehaviour SpawnGuy(prefab) /*0x4C1338*/ {
    obj = ObjectPool.GetObject(prefab, parent = spawner.transform);   // -> BasicEnemy.Initialize -> SetInitialPosition
    obj.GetComponent<UIWidget>().depth = 100;
    AddEnemy(obj); _currentTimeSinceSpawns[IndexOfEnemy(prefab)] = 0;
    return obj;
}
```
One enemy per spawn tick. The interval is chosen *after* the spawn from the wave that is current then.
Spawn position: `BasicEnemy.SetPositionOnScreenEdge` 0x4BD10C picks a random side (Random.Range(0,4)) and a uniform position
along it. The half extents are (Screen.width, Screen.height) · (720/Screen.height) · 0.5, i.e. (640, 360) at 16:9.
The enemy appears exactly on the screen border. Orbit types then derive radius and angle from it (see android_values §5).
Note: because the per-type timers start at 99999 only in Awake and keep counting between games, the first power-up
of any game may spawn at once.

---

## 6. PowerupTarget (power-up pickup)

`Initialize` 0x64604C: `BasicEnemy.Initialize()`. Then `x = Random.Range(0,1)` and cumulative over `PowerupTypeSpawnRates` [0.4, 0.3, 0.3].
The first i with x < sum gives `CurrentPowerupType = i + 1`: **Shield 40 %, AttackSpeed 30 %, AttackWidth 30 %**.
`SetInitialPosition` 0x645ED0:
```c
SetPositionOnScreenEdge(); SetAngleFromPosition();
d = DistanceToPlayer;                                   // 360..734 px at 16:9
_currentRadius = Clamp(Random.Range(MinDistance=440, MaxDistance=1000), 0, d - 30);
UpdatePosition();                                       // -> it pops in 30+ px inside the edge (often exactly d-30)
```
Movement (BasicOrbitEnemy): radius −= SpeedPulledToCenter (65 px/s) · dt · GTS. MoveSpeed 0 means no angular motion, so it slides
straight to the centre. When it touches a player side you **collect** it (2.5). If the ring kills it, you get 10 points and no power-up.
`GetPowerupString` 0x644704: 1 "Shield", 2 "Attack Speed", 3 "Attack Width", else "Unknown". `GetPowerupType` 0x645FA0 is the reverse.
Max 1 alive at a time, and at least 10–12 scaled seconds between spawns (wave table).

## 7. OrbiterAlly ("star", friendly)

`SetInitialPosition` 0x640FA0: BasicOrbitEnemy start (edge, random direction), then `_currentRadius = Random.Range(MinDistance 110, MaxDistance 160)`
(it **jumps** to that radius), `_nextPointTime = PointsEverySeconds (2.0)`. SpeedPulledToCenter = 0, so the radius stays fixed.
It orbits at MoveSpeed 25°/s · GTS and **never reaches the player**.
`Reset` 0x640FDC / `OnResetOrbiter` 0x6410AC (RESET_ORBITER, end of tutorial): `_currentPointsPerTimeFrame = PointsPerTimeFrame (2)`, `_numTimeFrames = 0`.
`UpdatePosition` 0x640DF8:
```c
BasicOrbitEnemy.UpdatePosition();
_nextPointTime -= dt * GTS;
if (_nextPointTime <= 0) {
    _nextPointTime = PointsEverySeconds;                                  // 2.0
    _currentPointsPerTimeFrame += PointsIncreasedEveryTimeFrame;          // +2  -> first payout 4, then 6, 8, 10, ...
    GameState.AddPoints(_currentPointsPerTimeFrame);                      // score (not via ENEMY_DESTROYED)
    ShowPointLabel(_currentPointsPerTimeFrame);                           // "+4" etc. at the star
    if (++_numTimeFrames >= EndTutorialHintAfterTimeFrames) TryFireTutorialEnemyHintKill();  // returns false (no-op)
    /* achievement 18: progress = current/500*100 */
}
```
Reward: while one star lives n ticks (2n scaled seconds), it pays 4+6+…+(2n+2) = n² + 3n points in total. Killing it (it is in the
ring band at release) gives 0 points and stops the stream. Up to 2 (3 from 40 s) can be alive at once.

---

## 8. Constants found in code (not serialized)

| Constant | Where | Meaning |
|---|---|---|
| 120.0 px | rodata 0x9F1BB4, `SpawnAttackPieces` | attack ring local apothem |
| 240.0 | 0x9F1BBC | 2·120 for the side length |
| −90°, 360/n | 0x9F1BC4 / 0x9F1BC0 | first side angle, step (player and attack) |
| +1 px | `SpawnPlayerShape`/`SpawnAttackPieces` | side sprite width overlap |
| +6 px | `UpdateAttack` (fmov #6.0) | ring sprite height = width + 6 |
| 0.01 | 0x9F22B0, `SpawnAttack` | initial ring scale (one frame) |
| depths 9/8 (player), 51/50 (ring), 100 (enemies) | | draw order |
| 1.0 s | `GameState.Update`/`OnStartGame` | passive-point interval (PointsPerSecond = 0) |
| 0.1 s | 0x9F1BD8, `EnemySpawner.OnStartGame` | delay of the first spawn attempt |
| 99999 | 0x9F1BDC, `EnemySpawner.Awake` | initial "time since spawn" per type |
| 5.0 s | `OnTutorialDeath` | tutorial respawn delay |
| −30 px | `PowerupTarget.SetInitialPosition` | spawn inset |
| 15 / 500 | achievements | ignore |

## 9. Uncertain / open points

1. **Frame rate:** no `targetFrameRate` call was found, so the Android default (≈30 fps) is assumed. The logic is dt-based, so a port at
   50 or 60 Hz is equivalent apart from collision sampling (trigger events at 50 Hz physics).
2. **Accent ring as a hit area:** the code collects enemies from all AttackPiece children (main and accent). The accent pieces are the
   same prefab with an auto-resized trigger, so the effective band is probably [ (120−W−6)·P , 124·P ]. A simpler, faithful approximation
   for the port: hit if the enemy box overlaps the band between apothem (114−W)·P and 124·P along any side.
3. **Enemy collider size:** NGUI `autoResizeBoxCollider` = 1 on the enemy sprites too. Whether NGUI resizes their colliders to the
   sprite size (48 px) or keeps the serialized box (e.g. 32 px for Kamakazi) depends on NGUI internals that were not traced.
4. `IterationType` Random/RandomAtScore use `Random.Range(Min, Max − Min + 2)` literally (a likely original bug). It is unused, because Count is set.
5. `LifeTied` (FireAttack adds the linked enemy): it is set by the tutorial's life-tied spawns. Other users (e.g. Kite tails) were not traced.
6. KiteEnemy.OnDamaged and the Kite, Carrier, Orbiter and Stealth movement are not covered here (see android_values §5–6).
7. Restart: whether a tap in the 2 s before the summary menu restarts at once depends on SummaryMenu/GameMenu code, which was not read.
