# Release (Android): enemy behaviour reconstructed from the IL2CPP code

Source: `libil2cpp.so` (arm64) from the user's own APK, disassembled with capstone. Method names and RVAs come from Il2CppDumper's
`script.json` (they agree with `dump.cs`). Field offsets come from `dump.cs`. Prefab values come from `docs/android_values.md`
(decoded by a separate pass); values marked *(prefab)* are taken from that file, and values marked *(ctor)* are the C# field initialisers
that the code sets (the prefab overrides them).

Tools (scratchpad `enemy_re/`): `edis.py` (disassembler with BL/GOT/float/field/vtable annotation), `run.sh`, `all.sh`, `show.sh`, `flt.sh`,
`plt.py` (resolves PLT stubs: 0x2f8660 = `cosf`, 0x2f7800 = `sinf`, 0x2f8780 = `fmodf`, 0x2f8270 = `sqrtf`). There is one raw listing per class: `enemy_re/<Class>.txt`.

Legend: **[code]** = read directly from the disassembly. **[?]** = inference or uncertain.

---

## 0. Units, frames and coordinates

* All enemies are NGUI widgets under `UI Root/Camera/Game/EnemySpawner` (local origin = screen centre = the player).
  **px** = NGUI virtual pixels. The screen is always 720 px tall and 720·aspect px wide (1280 at 16:9). 1 Unity world unit = 360 px.
* Everything runs in `Update()` (no FixedUpdate in any enemy class). Physics (trigger callbacks) only does hit detection.
* `dtg = Time.deltaTime * CoreBehaviour.GameTimeScale` (`GameDeltaTime`, RVA 0x4b9368). GameTimeScale starts at 1.0 and grows by 0.0075 per real second
  (see android_values.md §1). **All enemy speeds, timers and lifetimes use the scaled time.**
* Two movement spaces are used:
  * **Homing (BasicEnemy.UpdatePosition)** works on `transform.position` (world). `MoveSpeed` is in **world units/s, and ×360 gives px/s**.
  * **Orbit (BasicOrbitEnemy)** works on `transform.localPosition` (px). `MoveSpeed` is **degrees/s**, and `SpeedPulledToCenter` is **px/s**.
* `ScreenFixedSizeRatio = UIRoot.manualHeight(720) / Screen.height` (GameState, 0x4bd2a8). Screen half extents `hw = Screen.width·ratio/2`, `hh = 360`.

## 1. Shared framework

### 1.1 Object pool (ObjectPool.GetObject 0x64097c / ReturnObject 0x640830) [code]
```
GetObject(prefab, parent):  obj = pop or Instantiate; SetActive(true); if parent: obj.parent = parent;
                            obj.localPosition/eulerAngles/localScale = prefab's; obj.Initialize();   // vtable slot 4
ReturnObject(obj):          obj.Reset(); SetActive(false); reparent to pool                         // vtable slot 5
```
New pool objects are passed through ReturnObject once, so **Reset() always runs before the first Initialize()**.

### 1.2 BasicEnemy (abstract) : PoolableBehaviour : CoreBehaviour
Fields: `MoveSpeed 0x28` (ctor 0.3), `PointValue 0x2C` (ctor 1), `StartHealth 0x30` (ctor 1), `CurrentHealth 0x34`, `CurrentMultiKillMultiplier 0x38`,
`AllowMovement 0x3C`, `DeathEffectPrefab 0x40`, `SpawnSound 0x48`, `DeathSound 0x50`, `DamagedColor 0x58` (ctor white), `DamagedSpriteName 0x68` (ctor ""),
`LifeSpan 0x70` (ctor −1), `LifeTied 0x78`, `_originalColor 0x80`, `_originalSpriteName 0x90`, `_timeAlive 0x98`, `HasHitPlayer 0x9C`.

```c
Awake (0x4bcd50):        _originalColor = UISprite.color; _originalSpriteName = UISprite.spriteName;

Reset (0x4bdb5c):        CurrentHealth = StartHealth; CurrentMultiKillMultiplier = 1.0;
                         sprite.color = _originalColor; sprite.spriteName = _originalSpriteName;
                         LifeTied = null; _timeAlive = 0;

Initialize (0x4bdac0):   SetInitialPosition();                 // virtual, slot 8
                         AudioManager.TryPlaySoundEffect(SpawnSound);
                         HasHitPlayer = false; AllowMovement = true;

Update (0x4bcdcc):       if (!PlayerController.GameIsLive) return;   // everything freezes after game over
                         if (!AllowMovement) return;                 // tutorial can hold enemies still
                         UpdatePosition();                            // virtual, slot 7
                         UpdateTimeAlive();

UpdateTimeAlive (0x4bce74):
                         _timeAlive += dtg;
                         if (LifeSpan > 0 && _timeAlive >= LifeSpan) Kill(null, fromKill=false);
                         // every shipped prefab has LifeSpan −1 [?]: android_values.md does not list LifeSpan

SetInitialPosition (0x4bd108):  SetPositionOnScreenEdge();

SetPositionOnScreenEdge (0x4bd10c):
    hw = Screen.width  * ratio * 0.5;  hh = Screen.height * ratio * 0.5;   // 640 x 360 at 16:9
    switch (Random.Range(0,4)) {                                            // int range, 0..3
      case 0: x = -hw; y = Random.Range(-hh, hh); break;   // left
      case 1: x = +hw; y = Random.Range(-hh, hh); break;   // right
      case 2: x = Random.Range(-hw, hw); y = +hh; break;   // top
      case 3: x = Random.Range(-hw, hw); y = -hh; break;   // bottom
    }
    localPosition = (x, y, 0);

UpdatePosition (0x4bced8)  -- "homing", used by Fighter, Kamakazi, Stealth and all children:
    dir = normalize(Player.transform.position - transform.position);        // world space
    transform.position += dir * MoveSpeed * Time.deltaTime * GameTimeScale;  // world units
    // The player never moves (it sits at the centre), so this is a straight line to the centre at constant speed.

DistanceToPlayer (0x4bcc2c): Vector3.Distance(localPosition, Player.localPosition)   // px

OnDamaged(attack) (0x4bd354):          // called on every hit, including the fatal one
    sprite.color = DamagedColor;
    if (DamagedSpriteName != "") sprite.spriteName = DamagedSpriteName;

Kill(murderer, fromKill=true) (0x4b9c7c):
    SendMessage(ENEMY_DESTROYED /*2*/, gameObject);   // ALWAYS, which also scores (see 1.4)
    if (fromKill) OnDeath(murderer);                  // virtual, slot 10
    ObjectPool.ReturnObject(this);                    // -> Reset()

OnDeath(attack) (0x4bd400):
    if (DeathEffectPrefab) spawn it from the pool (parent = my parent) at my world position;
    ShowPointLabel((int)(CurrentMultiKillMultiplier * PointValue));   // virtual, slot 12
    AudioManager.TryPlaySoundEffect(DeathSound);

ShowPointLabel(points) (0x4bd7e0):
    lbl = pool.Get(EnemySpawner.EnemyGainedPointsLabelPrefab); lbl.SetPoints(points);
    lbl.parent = my parent's parent; lbl.localScale = prefab scale; lbl.position = my position;   // "+N" popup, 1.5 s

Shoot(prefab) (0x4bd630)  -- used by Carrier and Orbiter to launch children:
    c = pool.Get(prefab); c.parent = my parent; c.localScale = prefab.localScale;
    c.localPosition = my localPosition; c.UIWidget.depth = 100; return c;
    // The child is NOT added to EnemySpawner's live list (it does not count toward MaxLiveEnemies) [code: no AddEnemy call]
```

### 1.3 Being hit by the attack [code]
* `AttackPiece.OnTriggerEnter/Exit` (0x4ba8fc / 0x4baa88) keep a list of the enemy GameObjects that currently overlap that ring segment
  (Unity trigger: the enemy has a Rigidbody and a trigger BoxCollider; the segments are thin trigger bars).
* On release, `PlayerController.EndAttack` (0x6448e4) calls `Attack.FireAttack` (0x4b9458):
```c
hit = {};  for each piece: for each go in piece.CurrentEnemies: hit.addUnique(go);
           if (go.BasicEnemy.LifeTied != null) hit.addUnique(LifeTied.gameObject);   // tutorial only (SpawnLifeTiedEnemies)
dead = {};
for each go in hit: e = go.BasicEnemy; e.CurrentHealth -= Attack.Damage /*1*/; e.OnDamaged(attack);
                    if (e.CurrentHealth <= 0) dead.add(e);
KillEnemies(dead);  ObjectPool.ReturnObject(attack);       // the ring disappears after firing

KillEnemies(list) (0x4b991c):
    m = GetKillMultiplier(list.Count);
    for each e: e.CurrentMultiKillMultiplier = m; e.Kill(attack, true);
    (achievement/tutorial bookkeeping for 1 and >=2 kills)

GetKillMultiplier(n) (0x4b9b78):
    if (UseSimpleMultiKillMultiplier /*true*/) return 1 + PerKillMultiplier /*0.1*/ * (n - 1);
    else: last table entry with NumToKill <= n -> Multiplier, default 1.0   (table unused)
```
So only enemies that touch the ring **line** at the moment of release are hit. Each one loses 1 HP. Enemies that survive keep their damaged look.

### 1.4 Score [code GameState.OnEnemyDestroyed 0x4c4a08]
`CurrentScore += (int)(PointValue * CurrentMultiKillMultiplier)` (truncation toward zero), and the per-type kill count `EnemyTypeName` is incremented.
This handler listens to ENEMY_DESTROYED, which `Kill()` always sends. So points are also scored when an enemy is removed with
`fromKill=false` (power-up pickup, LifeSpan expiry). In that case no "+N" label, death effect or sound is shown. `EnemySpawner.RemoveEnemies` returns objects to the pool
without Kill, so it scores nothing.

### 1.5 Reaching the player [code]
The player polygon (radius 22 px) has one trigger BoxCollider per side (`PlayerControllerSide`).
```c
PlayerControllerSide.OnTriggerEnter(other) (0x645450):
    e = other.GetComponent<BasicEnemy>();
    if (PlayerController.GameIsLive && e != null && !e.HasHitPlayer) PlayerController.OnDamage(e);

PlayerController.OnDamage(e) (0x64452c):
    e.HasHitPlayer = true;
    if (e is PowerupTarget p) {                       // GetComponent<PowerupTarget>
        SendMessage(EARNED_POWERUP /*12*/, GetPowerupString(p.CurrentPowerupType));
        AddPowerup(p.CurrentPowerupType);             // 0x64476c
        e.Kill(null, fromKill=false);                 // no FX or label, but +PointValue (10) is scored, see 1.4
    } else if (_currentPowerup == Shield /*1*/) {
        StopShield(); _currentPowerup = None; _currentPowerupTimer = -1.0;   // the shield absorbs exactly ONE hit
        e.Kill(null, fromKill=true);                  // the enemy dies normally: FX, label, points x1
    } else if (TutorialManager.HasCompletedMainTutorial()) {
        SendMessage(GAME_OVER /*0*/, (object)true);   // one touch = game over; GameIsLive=false freezes all enemies
    } else OnTutorialDeath();
```
There is no HP for the player. Any enemy touch (except a power-up) ends the game unless a shield is active. This includes the OrbiterAlly if it ever touches the player
(it cannot in practice, see 9).

### 1.6 BasicOrbitEnemy : BasicEnemy
Fields: `SpeedPulledToCenter 0xA0` (ctor 25), `MinStartFromEdgeDistance 0xA4`, `MaxStartFromEdgeDistance 0xA8` (both 0 in all prefabs),
`_currentRadius 0xAC`, `_currentAngle 0xB0` (degrees), `_movingClockwise 0xB4`.
```c
SetInitialPosition (0x4bdf20):
    SetPositionOnScreenEdge();                       // only used to pick a random angle
    _movingClockwise = Convert.ToBoolean(Random.Range(0,2));      // 50/50
    _currentRadius = ratio * Min(Screen.width, Screen.height) * 0.5     // = 360 px in landscape
                     - Random.Range(MinStartFromEdgeDistance, MaxStartFromEdgeDistance);   // - 0
    SetAngleFromPosition();

SetAngleFromPosition (0x4be084):
    a = Vector2.Angle((1,0), localPosition.xy);      // 0..180
    _currentAngle = (localPosition.y > 0) ? a : -a;  // = atan2 in degrees (y == 0 gives the negative branch)

UpdatePosition (0x4bdcc4):
    UpdateRadius();  UpdateAngle();                  // virtual, slots 13 / 14
    localPosition = (r * cos(_currentAngle*Deg2Rad), r * sin(_currentAngle*Deg2Rad), 0);   // cosf/sinf confirmed

UpdateRadius (0x4bddd8):  _currentRadius -= SpeedPulledToCenter * dtg;      // px/s inward, no lower clamp (it can go negative)
UpdateAngle  (0x4bde60):  _currentAngle += (_movingClockwise ? +1 : -1) * MoveSpeed * dtg;   // deg/s
                          if (_currentAngle > 360) _currentAngle -= 360; else if (_currentAngle < -360) _currentAngle += 360;
```
Notes: "clockwise=true" actually **increases** the angle, which is counter-clockwise on screen (y up) [code; the name is misleading].
The edge position is overwritten on the first UpdatePosition. On a 16:9 screen an orbiter therefore **appears on the 360 px circle** at the angle of a random
edge point. At the left/right edges it appears 280 px inside the screen. (Initialize and the first Update run before the first render [?].)
The movement is an inward Archimedean-like spiral: `r(t) = 360 − SpeedPulledToCenter·t`, `θ(t) = θ0 ± MoveSpeed·t` (scaled time).

---

## 2. FighterEnemy (blue "+")  — class 0x4c3338.., EnemyTypeName "FighterEnemy"
No overrides except the name. It uses BasicEnemy completely.
```c
spawn:   random screen-edge point (1.2)
move:    straight line to the centre, MoveSpeed 0.1 world/s = 36 px/s (prefab), scaled by GameTimeScale
hp:      1          points: 10 (prefab)       collider 48x48 px       death FX FighterDE
player:  touch = game over / shield absorbs (1.5)
```
`FighterEnemy_TutorialDummy`: the same with MoveSpeed 0 (stationary). The tutorial places it at (300,100).

## 3. KamakaziEnemy (red triangle, "Diver") — EnemyTypeName "DiverEnemy"
```c
UpdatePosition (0x6c61d0): BasicEnemy.UpdatePosition();     // straight homing
SetInitialPosition (0x6c61d8):
    SetPositionOnScreenEdge();
    v   = -localPosition.xy;                                  // direction to the centre
    ang = Vector2.Angle((0,-1), v);
    eulerAngles = (0, 0, (localPosition.x > 0) ? -ang : ang); // rotate so the sprite's "down" (0,-1) points at the player
```
MoveSpeed 0.3 world/s = **108 px/s**, HP 1, **15** points, collider 32x32 px (sprite 48), death FX KamakaziDE. The rotation is set once and never updated
(not needed, because the path is a straight line).

## 4. StealthFighterEnemy (fading "+")
Fields: `StealthColor 0xA0`, `UnstealthColor 0xB0`, `UnstealthDistance 0xC0`, `UnstealthDuration 0xC4` (ctor 0.5), `_unstealthed 0xC8`.
```c
Reset (0x548b94):          base.Reset(); widget.color = StealthColor /*(0.224,0.224,0.224,0.784)*/; _unstealthed = false;
UpdatePosition (0x548a34): BasicEnemy.UpdatePosition();                // homing, 0.1 world/s = 36 px/s
                           if (!_unstealthed && DistanceToPlayer <= UnstealthDistance /*200 px*/) Unstealth();
Unstealth (0x548a88):      _unstealthed = true;
                           TweenColor.Begin(gameObject, UnstealthDuration /*0.5 s*/, UnstealthColor /*white*/);  // colour fade over 0.5 s (real time [?])
```
HP 1, **15** points, collider 48 px, death FX FighterDE. It is dark grey and semi-transparent until it is 200 px from the centre, then it fades to white over 0.5 s.
It can be hit while stealthed (the collider is always active). GameState tracks `UnstealthedStealthKills` for achievements.

## 5. KiteEnemy (green diamond head) + KiteChildEnemy (tail)

### KiteEnemy : BasicOrbitEnemy — fields `KiteFollowerPrefab 0xB8`, `DistanceForFollowersToFollow 0xC0` (List<float>), `DamagedMoveSpeed 0xC8` (ctor 35),
`CurrentDirection 0xCC` (Vector3), `_children 0xD8` (List<GameObject>)
```c
Initialize (0x6c6e80): base.Initialize(); RegisterHandler(ENEMY_DESTROYED, OnEnemyDestroyed);
SetInitialPosition (0x6c69f0):
    BasicOrbitEnemy.SetInitialPosition();
    foreach (d in DistanceForFollowersToFollow)          // prefab [50, 100] -> 2 tail pieces
        c = pool.Get(KiteFollowerPrefab);                // c.Initialize() puts it on a random edge first (harmless)
        c.parent = my parent; c.localScale = prefab scale (0.5); c.localPosition = my localPosition;
        c.ParentEnemy = this; c.DistanceToFollowParent = d; c.depth = 100;
        _children.Add(c.gameObject);
UpdatePosition (0x6c67f0):
    old = localPosition;  BasicOrbitEnemy.UpdatePosition();          // spiral: 25 deg/s, 25 px/s inward
    CurrentDirection = normalize(localPosition - old);              // per-frame velocity direction
    transform.right = (CurrentDirection.x, CurrentDirection.y, 0);
    localEulerAngles.z += 45;                                         // diamond sprite points along its motion
OnDamaged (0x6c6d30):  base.OnDamaged();  MoveSpeed = DamagedMoveSpeed;   // prefab 40: angular speed 25 -> 40 deg/s after the 1st hit
                       // DamagedColor white; sprite becomes 'Kite Damaged'
OnEnemyDestroyed(go) (0x6c6d5c):
    if (_children.Contains(go)) { _children.Remove(go);
        if (_children.Count <= 0 && CurrentHealth == StartHealth) SocialManager.ReportProgress(23 /*achievement 0x17*/, 100.0); }
Reset (0x6c6f10): base.Reset(); UnregisterHandlers(); _children.Clear();     // MoveSpeed is NOT restored here [?]: the pooled kite keeps 40 deg/s?
```
HP **2**, **15** points, collider 48 px, death FX KiteDE.

**Reset caveat [code]:** `BasicEnemy.Reset` does not restore `MoveSpeed`. After a damaged kite goes back to the pool, its next use orbits at DamagedMoveSpeed (40°/s)
from the start. This looks like an original bug. Reproduce it or not, as you prefer.

### KiteChildEnemy : BasicEnemy — `ParentEnemy 0xA0`, `DistanceToFollowParent 0xA8`
```c
Initialize (0x6c6668): base.Initialize(); RegisterHandler(ENEMY_DESTROYED, OnEnemyDestroyed);
Reset (0x6c66f8):      base.Reset(); UnregisterHandler(ENEMY_DESTROYED, ...);   // ParentEnemy is not cleared here, but it is overwritten on reuse
UpdatePosition (0x6c63ec):
    if (ParentEnemy != null) MoveTowardKite(); else MoveTowardPlayer();
MoveTowardKite (0x6c647c):
    localPosition = ParentEnemy.localPosition + DistanceToFollowParent * (-ParentEnemy.CurrentDirection);
    // rigid: placed exactly d px behind the head along the head's CURRENT motion direction (a straight tail, not a path trail)
MoveTowardPlayer (0x6c65b4): BasicEnemy.UpdatePosition();   // 0.3 world/s = 108 px/s straight to the centre
OnEnemyDestroyed(go) (0x6c65c4): if (ParentEnemy != null && ParentEnemy.gameObject == go) ParentEnemy = null;
```
Tail: HP 1, **10** points, prefab scale 0.5 (sprite 24 px, collider 16 px), death FX KiteChildDE.
**Rules:** killing a tail piece only removes it from the head's list. Killing the head releases the remaining tail pieces, which then fly straight at the player at 108 px/s
(faster than anything except a Diver). This is the meaning of "destroy the tail before the head". There is an achievement for destroying all tail pieces while the head is still undamaged.
The head and tail are independent targets. They are not life-tied, and both are hit if the ring touches both.
In the first frame the head's CurrentDirection is (0,0,0), so the tail sits on the head [code].

## 6. CarrierEnemy ("the longer Carriers live, the faster they deploy") + CarrierChildEnemy
CarrierEnemy : BasicOrbitEnemy — `CarrierChildPrefab 0xB8`, `SpawnRate 0xC0` (AnimationCurve), `_timeToNextSpawn 0xC8`. Its ctor sets SpeedPulledToCenter 25 and calls the BasicEnemy ctor directly.
```c
SetInitialPosition (0x4be8d4): BasicOrbitEnemy.SetInitialPosition(); SetupTimeToNextSpawn();
UpdatePosition (0x4be84c):     BasicOrbitEnemy.UpdatePosition();          // 25 deg/s, 8 px/s inward (prefab)
                               _timeToNextSpawn -= dtg;  TryShoot();
TryShoot (0x4be888):           if (_timeToNextSpawn > 0) return false;
                               Shoot(CarrierChildPrefab); SetupTimeToNextSpawn(); return true;
SetupTimeToNextSpawn (0x4be8f8):
    rate = SpawnRate.Evaluate(_timeAlive);                 // _timeAlive in scaled seconds (1.2)
    _timeToNextSpawn = (rate > 0) ? 1 / rate : 1.0;
```
SpawnRate curve (prefab): keys (−0.1299, 0.3721, slope 0.0274) and (14.914, 1.4985, slope 0.2577), clamped outside the keys. That is about 0.38 children/s at birth
(first child after ≈2.6 s), ≈1.0/s at ≈8 s, and 1.4985/s (every 0.667 s) from 15 s on. Note that `_timeAlive` is read when the *next* delay is scheduled.
Carrier: HP 1, **15** points, collider 48 px, death FX CarrierDE. It reaches the player only after ≈39 s (360→~46 px at 8 px/s) if it is not killed.

CarrierChildEnemy : BasicEnemy — `SetInitialPosition` is empty (0x4be7fc), so the position comes from Shoot (the carrier's position).
It homes straight in: 0.2 world/s = **72 px/s**, HP 1, **20** points, scale 0.35 (sprite 16.8 px, collider 11.2 px), death FX CarrierChildDE.
**When the Carrier dies, its children are unaffected** (no link is kept).

## 7. OrbiterEnemy + OrbiterChildEnemy (present in code and pool, NOT in the spawner list: unused in normal play [?])
OrbiterEnemy : BasicOrbitEnemy — `OrbiterChildPrefab 0xB8`, `EllipseSquishiness 0xC0` (ctor 80, prefab 170), `_startAngle 0xC4`, `_nextShootAngle 0xC8` (int),
`_a 0xCC`, `_b 0xD0`, `_ab 0xD4`.
```c
SetInitialPosition (0x641330):
    BasicOrbitEnemy.SetInitialPosition();
    _startAngle = _currentAngle;
    _a = _currentRadius;                                   // 360
    _b = _currentRadius - EllipseSquishiness;              // 190
    if (_b < 2*PlayerRadius*1.2) _b = 2*PlayerRadius*1.2;  // 52.8
    _ab = _a*_b;  _nextShootAngle = 180;
UpdateRadius (0x641180):   // replaces the spiral: polar ellipse, major axis along the spawn direction
    t = (_currentAngle - _startAngle) * Deg2Rad;
    _currentRadius = _ab / sqrt(_a*_a*sin(t)^2 + _b*_b*cos(t)^2);   // r = a at t=0,180; r = b at t=90,270
UpdateAngle (0x641254):
    BasicOrbitEnemy.UpdateAngle();                         // 25 deg/s
    d = fmod(|_currentAngle - _startAngle|, 360);
    if ((d >= 180 && _nextShootAngle == 180) || (d < 180 && _nextShootAngle == 0)) {
        Shoot(OrbiterChildPrefab);
        _nextShootAngle = (_nextShootAngle == 0) ? 180 : 0;
    }   // = one child each time it passes the far point opposite the spawn point, and one each time it returns to the spawn direction
```
Orbiter: HP 1, **0** points, collider 32 px, no death FX. It never approaches the player (the radius stays between 190 and 360).
OrbiterChildEnemy: SetInitialPosition is empty. It homes at 0.2 world/s = 72 px/s, HP 1, **1** point, scale 0.35, collider 11.2 px.

## 8. OrbiterAlly (yellow star: "Let Orbiters live. You'll be rewarded.")
OrbiterAlly : BasicOrbitEnemy — `MinDistance 0xB8` (ctor 75), `MaxDistance 0xBC` (ctor 130), `PointsPerTimeFrame 0xC0` (ctor 1), `PointsEverySeconds 0xC4` (ctor 1.0),
`PointsIncreasedEveryTimeFrame 0xC8` (ctor 2), `EndTutorialHintAfterTimeFrames 0xCC` (ctor 3), `_nextPointTime 0xD0`, `_currentPointsPerTimeFrame 0xD4`, `_numTimeFrames 0xD8`.
Prefab: Min/Max 110/160 px, PointsPerTimeFrame 2, PointsEverySeconds 2.0, PointsIncreased 2, MoveSpeed 25°/s, SpeedPulledToCenter **0**, PointValue **0**.
```c
Initialize (0x64101c):  base.Initialize(); RegisterHandler(RESET_ORBITER /*16*/, OnResetOrbiter);
Reset (0x640fdc):       base.Reset(); _currentPointsPerTimeFrame = PointsPerTimeFrame; _numTimeFrames = 0; UnregisterHandlers();
OnResetOrbiter (0x6410ac): same two assignments (sent by the tutorial when real play starts)
SetInitialPosition (0x640fa0):
    BasicOrbitEnemy.SetInitialPosition();                 // random angle and direction
    _currentRadius = Random.Range(MinDistance, MaxDistance);   // 110..160 px: it appears close to the player
    _nextPointTime = PointsEverySeconds;
UpdatePosition (0x640df8):
    BasicOrbitEnemy.UpdatePosition();                     // circle (pull 0), 25 deg/s
    _nextPointTime -= dtg;
    if (_nextPointTime <= 0) {
        _nextPointTime = PointsEverySeconds;              // 2 s (scaled)
        _currentPointsPerTimeFrame += PointsIncreasedEveryTimeFrame;   // increment BEFORE awarding
        GameState.AddPoints(_currentPointsPerTimeFrame);  // 4, 6, 8, 10, ... every 2 s
        ShowPointLabel(_currentPointsPerTimeFrame);       // "+N" popup at the star
        if (++_numTimeFrames >= EndTutorialHintAfterTimeFrames) TryFireTutorialEnemyHintKill();   // returns false, stub
        SocialManager.ReportProgress(18, cur/500*100);    // achievement: reach 500 per tick
    }
```
Being shot: HP 1. It dies normally with 0 points (×multiplier = 0), death FX OrbiterDE, and the growing income is lost. A kill still counts toward the multiplier of other
enemies killed by the same release (n includes it). It never reaches the player (radius constant). Spawner cap 2–3 alive (android_values.md §7).
The tick pays `PointsPerTimeFrame + k·PointsIncreased` for k = 1, 2, ..., so the k-th tick (after 2k scaled seconds) pays 2+2k: total after k ticks = k² + 3k.

## 9. PowerupTarget (green ball: "When Power-Ups hit you, you become stronger.")
PowerupTarget : BasicOrbitEnemy — `PowerupTypeSpawnRates 0xB8` (List<float>, prefab [0.4,0.3,0.3]), `MinDistance 0xC0` (ctor 300, prefab 440),
`MaxDistance 0xC4` (ctor 1000, prefab 1000), `CurrentPowerupType 0xC8`.
Prefab: MoveSpeed **0** (no rotation), SpeedPulledToCenter **65 px/s**, PointValue 10, HP 1, collider 23.25 px, FX PowerUpDE.
```c
Initialize (0x64604c):
    base.Initialize();                          // -> SetInitialPosition below
    rnd = Random.Range(0.0, 1.0); cum = 0;
    for (i = 0; i < rates.Count; i++) {
        cum += rates[i];
        if (rnd < cum) { CurrentPowerupType = i + 1; break; }     // 1 Shield 40 %, 2 AttackSpeed 30 %, 3 AttackWidth 30 %
        if (cum >= 1.0) Debug.LogWarning("Random didn't find a new powerup type: " + cum + " " + rnd);
    }   // nothing found: the type keeps its previous value (rnd == 1.0 edge case only)
SetInitialPosition (0x645ed0):              // no random direction call: _movingClockwise keeps its old value, irrelevant because MoveSpeed is 0
    SetPositionOnScreenEdge(); SetAngleFromPosition();
    _currentRadius = Clamp(Random.Range(MinDistance, MaxDistance), 0, DistanceToPlayer - 30);
    BasicOrbitEnemy.UpdatePosition();       // placed immediately (no jump)
    // With 440..1000 vs edge distance 360..734 px, the radius is almost always DistanceToPlayer-30:
    // it starts 30 px inside the edge point, on the ray toward the centre.
UpdatePosition: inherited spiral with MoveSpeed 0, i.e. straight inward at 65 px/s (scaled).
ShowPointLabel (0x645f9c): empty            // no "+10" popup ever
```
* **Shot by the attack:** dies (+10·mult points, FX, sound, no label). **No power-up.**
* **Touches the player:** `OnDamage` gives the power-up (EARNED_POWERUP message + `AddPowerup(type)`), then `Kill(null,false)`. That adds 10 points with no label or FX (1.4).
  The durations come from PlayerController.PowerupDurations [45,15,15] s. Shield = absorbs one hit, AttackSpeed = ring expands 2x, AttackWidth = ring line grows 2x
  (android_values.md §3).
* Spawner: max 1 alive, at least 10–12 s between power-ups.

---

## 10. Quick constant table (prefab values; speeds at GameTimeScale 1)

| Enemy | Movement | Speed | Inward | HP | Points | Collider px | Children / special |
|---|---|---|---|---|---|---|---|
| Fighter | straight homing | 36 px/s | – | 1 | 10 | 48 | – |
| Kamakazi (Diver) | straight homing, sprite rotated to the centre | 108 px/s | – | 1 | 15 | 32 | – |
| Stealth | straight homing | 36 px/s | – | 1 | 15 | 48 | hidden colour until ≤200 px, 0.5 s fade to white |
| Kite head | spiral (r0 360) | 25°/s, 40°/s after 1 hit | 25 px/s | 2 | 15 | 48 | 2 tail pieces at 50/100 px behind its motion direction |
| Kite tail | rigid follow; after the head dies straight homing | 108 px/s | – | 1 | 10 | 16 | – |
| Carrier | spiral (r0 360) | 25°/s | 8 px/s | 1 | 15 | 48 | child every 1/SpawnRate(t): ≈2.6 s → 0.67 s |
| Carrier child | straight homing from the carrier position | 72 px/s | – | 1 | 20 | 11.2 | – |
| Orbiter (unused) | ellipse a=360, b=190 | 25°/s | – | 1 | 0 | 32 | child at 0° and 180° from the spawn angle |
| Orbiter child | straight homing | 72 px/s | – | 1 | 1 | 11.2 | – |
| OrbiterAlly | circle r = 110..160 | 25°/s | 0 | 1 | 0 | 48 | +4, +6, +8 … every 2 s while alive |
| PowerupTarget | straight inward from 30 px inside the edge | 0°/s | 65 px/s | 1 | 10 | 23.25 | touching = power-up (40/30/30) |

Direction for all orbiters: 50/50 random. Angles in degrees, y up.

## 11. Open questions / uncertainties
1. `LifeSpan` per prefab is not listed in android_values.md. The code supports it (Kill without FX after LifeSpan scaled seconds); presumably −1 everywhere [?].
2. Whether the edge spawn point of orbit enemies is ever rendered before the first Update moves it onto the 360 px circle (Unity order: Initialize happens during the spawner's
   Update, so the enemy's own Update may first run in the *next* frame, which would show one frame at the edge) [?].
3. Kite MoveSpeed is not restored on Reset (see 5), which looks like an original bug [code]. Check against the original game if possible.
4. The exact physical hit test depends on Unity trigger overlap of rotated BoxColliders (enemy box vs. thin ring/side bars, 6 px thick). It is not reproduced here.
   For a port: hit if the distance to the ring line ≤ enemy half-size (+3 px). Player hit when the distance to the centre ≤ ~22 + half-size.
5. TweenColor duration is presumably in real (unscaled) time (NGUI default) [?].
6. OrbiterEnemy is unused in normal play (not in EnemySpawner.EnemyPrefabs, android_values.md §7) [?].
7. `GameState.AddPoints` (used by OrbiterAlly) was not disassembled in detail; it presumably adds to CurrentScore and fires SCORE_UPDATED [?].
