/* original.c — the gameplay of the Android original (Gimogames' Release),
 * as read from its code and data: docs/android_player.md,
 * docs/android_enemies.md, docs/android_values.md give every rule and
 * number used here, with the addresses they come from.
 *
 * Included by main.c (it uses its helpers: G, play(), the text and colour
 * functions); used when the style is "original" (the effects setting),
 * main.c's own logic (the Vectrex version) otherwise.
 *
 * Units as in the original: px with the player at (0, 0), y up, the screen
 * 720 px tall (360 each way), seconds. GTS (GameTimeScale) starts at 1 and
 * grows 0.0075 a second; everything that moves is scaled by it, the wave
 * table and the spawn interval run on real time.
 */

#define O_MAX        160
#define O_PARTICLES  256
#define O_POPUPS     32
#define O_FX         48             /* explosions: rings of dots, spheres of dots, flashes */
#define O_SHARDS     400            /* a killed model's edges, flying apart */

enum { O_FIGHTER, O_DIVER, O_KITE, O_CARRIER, O_ALLY, O_STEALTH, O_POWERUP, O_SPAWN_TYPES,
       O_KITE_TAIL = O_SPAWN_TYPES, O_CARRIER_CHILD, O_TYPES };

enum { P_NONE, P_SHIELD, P_SPEED, P_WIDTH };

/* per type: speed (homing px/s or orbit deg/s), pull to the centre (px/s),
 * hit points, points, collider half size (px) — the prefabs' values */
static const struct { float speed, pull; int hp, points; float half; } o_kind[O_TYPES] = {
    [O_FIGHTER]       = {  36,  0, 1, 10, 24.0f },
    [O_DIVER]         = { 108,  0, 1, 15, 16.0f },
    [O_KITE]          = {  25, 25, 2, 15, 24.0f },
    [O_CARRIER]       = {  25,  8, 1, 15, 24.0f },
    [O_ALLY]          = {  25,  0, 1,  0, 24.0f },
    [O_STEALTH]       = {  36,  0, 1, 15, 24.0f },
    [O_POWERUP]       = {   0, 65, 1, 10, 11.6f },
    [O_KITE_TAIL]     = { 108,  0, 1, 10,  8.0f },
    [O_CARRIER_CHILD] = {  72,  0, 1, 20,  5.6f },
};

/* EnemySpawner.EnemiesSpawnRates: from AtGameTime (real s) on, the weights of
 * Fighter, Diver, Kite, Carrier, Ally, Stealth, Powerup; the spawn interval;
 * live caps per type (-1 none); the least time between power-ups (scaled s) */
static const struct { float at, rate[7], min_t, max_t; int cap[7]; float powerup_gap; } o_waves[5] = {
    {  0, { 0.60f, 0.10f, 0,     0,     0,     0,     0.30f }, 1.0f, 2.0f, { -1, -1, -1, -1, -1, -1, 1 }, 10 },
    {  8, { 0.60f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.15f }, 1.0f, 1.9f, { -1, -1, -1, -1,  2, -1, 1 }, 11 },
    { 15, { 0.60f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.15f }, 0.9f, 1.5f, { -1, -1, -1, -1,  2, -1, 1 }, 10 },
    { 25, { 0.40f, 0.10f, 0.10f, 0.10f, 0.10f, 0.10f, 0.10f }, 0.7f, 1.1f, { -1, -1, -1, -1,  2, -1, 1 }, 12 },
    { 40, { 0.30f, 0.15f, 0.10f, 0.10f, 0.15f, 0.10f, 0.10f }, 0.3f, 0.8f, { -1, -1, -1, -1,  3, -1, 1 }, 10 },
};

#define O_PLAYER_R       22.0f      /* the player polygon's apothem */
#define O_ACCENT_R       26.0f
#define O_RING_R         120.0f     /* the ring's apothem at power 1 */
#define O_RING_ACCENT    4.0f
#define O_RING_GROW      1.5f       /* power a second */
#define O_RING_WIDEN     12.5f      /* line width a second */
#define O_ROTATION       5.0f       /* the player, degrees a second */
#define O_SPEED_GROWTH   0.0075f
#define O_UNSTEALTH_R    200.0f
#define O_DEMO_SECONDS   60.0f

typedef struct {
    int used, type, hp;
    float x, y;
    float speed, pull, r, ang;      /* orbit: radius px, angle degrees */
    int ccw;
    float t_alive;
    float dir_x, dir_y;             /* kite: its motion's direction */
    int parent, child[2];           /* kite and its tail */
    float follow;                   /* tail: distance behind the head */
    float next_shot;                /* carrier */
    float next_pay; int pay;        /* ally */
    int ptype;                      /* powerup */
    int unstealthed; float fade;    /* stealth: 0 hidden .. 1 seen */
    float rot;                      /* diver: its sprite's angle */
    int damaged, has_hit;
    float spin0;                    /* its 3D model's turn at the start */
} oenemy;

/* a particle: a streak flying out in 3D (z toward the viewer) */
typedef struct { float x, y, z, vx, vy, vz, life, max; unsigned char rgb[3]; } opart;
/* the death's blasts after the first: at `at` s, (x, y), a kind (0 a blast, 1 a wave) */
typedef struct { float at, x, y; int kind, done; unsigned char rgb[3]; } oblast;
#define O_BLASTS 12
/* an explosion as in Tempest 2000: rings of dots (draw_prex) or a sphere of
 * dots (drawsphere), growing to `size` px over `max` s */
enum { FX_RINGS, FX_SPHERE, FX_FLASH, FX_SHOCK, FX_WAVE };
typedef struct { float x, y, t, max, size, spin; int kind, sides; unsigned char rgb[3]; } ofx;
/* a shard: one edge of a killed model, tumbling away (centre, half its length) */
typedef struct { float x, y, vx, vy, hx, hy, spin, life, max; unsigned char rgb[3]; } oshard;
typedef struct { float x, y, t; int value; } opopup;

static struct {
    oenemy e[O_MAX];
    opart part[O_PARTICLES];
    opopup pop[O_POPUPS];
    ofx fx[O_FX];
    oshard shard[O_SHARDS];
    oblast blast[O_BLASTS];
    float death_t;                  /* since the death, s */
    float shake, shake_x, shake_y;  /* the screen shaking, px */
    int shatter;                    /* o_model makes shards instead of drawing */
    float flash;                    /* the player's white flash after a kill, 0..1 */
    unsigned fx_seed;               /* the effects' own random numbers: they do not change the game's */
    float hw, hh;                   /* half the screen, px */
    float kx, ky;                   /* board units a px */
    float gts, game_time;
    int sides;                      /* 5..8: kept between games, as in the original */
    float rot, accent_h;
    int attacking, attack_sides;
    float power, width;
    int powerup;
    float powerup_t;
    float next_spawn, since[O_SPAWN_TYPES];
    float label_t;
    const char *label;
    int prev_held, spawned_sound, death_sound;
    unsigned seed;
    float demo_wait, demo_target;
    float demo_careless;                            /* from then on the demo player starts no rings */
} O;

static void orig_view(void);

static float o_rand(void)
{
    O.seed = O.seed * 1664525u + 1013904223u;
    return (float)(O.seed >> 8) / 16777216.0f;
}

static float o_fx_rand(void)
{
    O.fx_seed = O.fx_seed * 1664525u + 1013904223u;
    return (float)(O.fx_seed >> 8) / 16777216.0f;
}

static float o_range(float a, float b) { return a + (b - a) * o_rand(); }

/* The point's distance to the centre measured as a polygon (n sides, the
 * first side's middle at `rot` degrees): the largest projection on a side
 * normal. On the polygon of apothem R it is R. */
static float o_poly_norm(float x, float y, int n, float rot)
{
    float best = -1e9f;
    for (int i = 0; i < n; i++) {
        float a = (rot + i * 360.0f / n) * 0.017453293f;
        float p = x * cosf(a) + y * sinf(a);
        if (p > best) best = p;
    }
    return best;
}

/* ---- spawning ----------------------------------------------------------- */

static void o_edge(float *x, float *y)
{
    switch ((int)(o_rand() * 4)) {
    case 0:  *x = -O.hw; *y = o_range(-O.hh, O.hh); break;
    case 1:  *x =  O.hw; *y = o_range(-O.hh, O.hh); break;
    case 2:  *x = o_range(-O.hw, O.hw); *y =  O.hh; break;
    default: *x = o_range(-O.hw, O.hw); *y = -O.hh; break;
    }
}

static oenemy *o_new(int type)
{
    for (int i = 0; i < O_MAX; i++) {
        if (O.e[i].used) continue;
        oenemy *e = &O.e[i];
        memset(e, 0, sizeof *e);
        e->used = 1;
        e->type = type;
        e->hp = o_kind[type].hp;
        e->speed = o_kind[type].speed;
        e->pull = o_kind[type].pull;
        e->parent = e->child[0] = e->child[1] = -1;
        e->spin0 = o_fx_rand() * 6.2831853f;
        if (!O.spawned_sound) {               /* one spawn sound a frame */
            play(G.s_spawn, 0.45f, 1.0f);
            O.spawned_sound = 1;
        }
        return e;
    }
    return NULL;
}

static void o_orbit_place(oenemy *e)
{
    float rad = e->ang * 0.017453293f;
    e->x = e->r * cosf(rad);
    e->y = e->r * sinf(rad);
}

/* BasicOrbitEnemy.SetInitialPosition: the angle of a random edge point, on
 * the circle of half the screen's smaller side, a random direction */
static void o_orbit_start(oenemy *e)
{
    float x, y;
    o_edge(&x, &y);
    e->ang = atan2f(y, x) * 57.29578f;
    e->r = O.hw < O.hh ? O.hw : O.hh;
    e->ccw = o_rand() < 0.5f;
    o_orbit_place(e);
}

static void o_spawn(int type)
{
    oenemy *e = o_new(type);
    if (!e) return;
    switch (type) {
    case O_KITE:
        o_orbit_start(e);
        for (int k = 0; k < 2; k++) {               /* DistanceForFollowersToFollow [50, 100] */
            oenemy *t = o_new(O_KITE_TAIL);
            if (!t) break;
            t->parent = (int)(e - O.e);
            t->follow = k ? 100.0f : 50.0f;
            t->x = e->x;
            t->y = e->y;
            e->child[k] = (int)(t - O.e);
        }
        break;
    case O_CARRIER:
        o_orbit_start(e);
        e->next_shot = 0;                           /* set below */
        break;
    case O_ALLY:
        o_orbit_start(e);
        e->r = o_range(110, 160);                   /* it jumps close to the player */
        e->next_pay = 2.0f;
        e->pay = 2;
        o_orbit_place(e);
        break;
    case O_POWERUP: {
        float x, y, d, r;
        o_edge(&x, &y);
        e->ang = atan2f(y, x) * 57.29578f;
        d = sqrtf(x * x + y * y);
        r = o_range(440, 1000);
        e->r = r > d - 30 ? d - 30 : r;
        o_orbit_place(e);
        float p = o_rand();                         /* Shield 40 %, speed 30 %, width 30 % */
        e->ptype = p < 0.4f ? P_SHIELD : p < 0.7f ? P_SPEED : P_WIDTH;
        break;
    }
    case O_DIVER:
        o_edge(&e->x, &e->y);
        e->rot = atan2f(-e->y, -e->x);              /* its point looks at the player */
        break;
    default:                                        /* Fighter, Stealth */
        o_edge(&e->x, &e->y);
        break;
    }
    if (type == O_CARRIER) {
        float t = e->t_alive < -0.13f ? -0.13f : e->t_alive > 14.914f ? 14.914f : e->t_alive;
        float rate = 0.3721f + (1.4985f - 0.3721f) * (t + 0.1299f) / (14.914f + 0.1299f);
        e->next_shot = 1.0f / rate;
    }
}

static int o_wave(void)
{
    int w = 0;
    for (int i = 4; i >= 0; i--) if (O.game_time >= o_waves[i].at) { w = i; break; }
    return w;
}

static int o_live(int type)
{
    int n = 0;
    for (int i = 0; i < O_MAX; i++) if (O.e[i].used && O.e[i].type == type) n++;
    return n;
}

static int o_blocked(int w, int i)
{
    if (i == O_POWERUP && O.since[i] < o_waves[w].powerup_gap) return 1;
    if (o_waves[w].cap[i] >= 0 && o_live(i) >= o_waves[w].cap[i]) return 1;
    return o_waves[w].rate[i] <= 0;
}

/* EnemySpawner.SpawnGuyRepeat: one enemy, by weight among the types not
 * blocked; the next try after Random(MinSpawnTime, MaxSpawnTime) */
static void o_spawn_tick(void)
{
    int w = o_wave();
    float total = 0;
    for (int i = 0; i < O_SPAWN_TYPES; i++) if (!o_blocked(w, i)) total += o_waves[w].rate[i];
    if (total > 0) {
        float x = o_rand() * total, acc = 0;
        for (int i = 0; i < O_SPAWN_TYPES; i++) {
            if (o_blocked(w, i)) continue;
            acc += o_waves[w].rate[i];
            if (x < acc) { o_spawn(i); O.since[i] = 0; break; }
        }
    }
    O.next_spawn = o_range(o_waves[w].min_t, o_waves[w].max_t);
}

/* ---- effects ------------------------------------------------------------ */

/* the original's death particles, flying out in all directions of space */
static void o_burst(float x, float y, const unsigned char *rgb, int count, float speed)
{
    for (int i = 0, n = 0; i < O_PARTICLES && n < count; i++) {
        opart *p = &O.part[i];
        if (p->life > 0) continue;
        float a = o_fx_rand() * 6.2831853f, cz = 2 * o_fx_rand() - 1, sz = sqrtf(1 - cz * cz);
        float v = speed * (0.35f + 0.65f * o_fx_rand());
        p->x = x; p->y = y; p->z = 0;
        p->vx = cosf(a) * sz * v; p->vy = sinf(a) * sz * v; p->vz = cz * v;
        p->max = p->life = 0.35f + 0.45f * o_fx_rand();
        memcpy(p->rgb, rgb, 3);
        n++;
    }
}

static ofx *o_explode(float x, float y, const unsigned char *rgb, int kind, float size, float seconds)
{
    for (int i = 0; i < O_FX; i++) {
        ofx *f = &O.fx[i];
        if (f->t < f->max) continue;
        f->x = x; f->y = y; f->t = 0; f->max = seconds; f->size = size; f->kind = kind;
        f->spin = o_fx_rand() * 6.2831853f;
        f->sides = 6;
        memcpy(f->rgb, rgb, 3);
        return f;
    }
    return NULL;
}

/* white sparks: fast, short, many */
static void o_sparks(float x, float y, int count, float speed)
{
    static const unsigned char white[3] = { 255, 255, 255 };
    for (int i = 0, n = 0; i < O_PARTICLES && n < count; i++) {
        opart *p = &O.part[i];
        if (p->life > 0) continue;
        float a = o_fx_rand() * 6.2831853f, cz = 2 * o_fx_rand() - 1, sz = sqrtf(1 - cz * cz);
        float v = speed * (0.6f + 0.4f * o_fx_rand());
        p->x = x; p->y = y; p->z = 0;
        p->vx = cosf(a) * sz * v; p->vy = sinf(a) * sz * v; p->vz = cz * v;
        p->max = p->life = 0.15f + 0.2f * o_fx_rand();
        memcpy(p->rgb, white, 3);
        n++;
    }
}

static void o_draw_enemy(vp_dvg *d, const oenemy *e);

/* the killed one's model breaks into its edges: each flies off from the
 * centre, tumbling, and fades */
static void o_shatter(const oenemy *e)
{
    O.shatter = 1;
    o_draw_enemy(NULL, e);
    O.shatter = 0;
}

static void o_popup(float x, float y, int value)
{
    for (int i = 0; i < O_POPUPS; i++) {
        if (O.pop[i].t > 0) continue;
        O.pop[i].x = x; O.pop[i].y = y; O.pop[i].value = value; O.pop[i].t = 1.5f;
        return;
    }
}

static const unsigned char *o_rgb(const oenemy *e);

/* BasicEnemy.Kill: points (PointValue x multiplier, truncated), the death
 * effect and the "+N" popup when shot. */
static void o_kill(oenemy *e, float mult, int shot)
{
    int pts = (int)(o_kind[e->type].points * mult);
    add_score(pts);
    if (shot) {
        int small = e->type >= O_KITE_TAIL, big = e->type == O_CARRIER || e->type == O_KITE;
        o_shatter(e);
        o_burst(e->x, e->y, o_rgb(e), small ? 14 : big ? 40 : 28, 320);
        o_sparks(e->x, e->y, small ? 6 : 14, 520);
        o_explode(e->x, e->y, o_rgb(e), FX_FLASH, small ? 40 : big ? 110 : 75, 0.18f);
        o_explode(e->x, e->y, o_rgb(e), FX_RINGS, small ? 22 : 40, 0.8f);
        if (big)                                    /* the big ones: a sphere too */
            o_explode(e->x, e->y, o_rgb(e), FX_SPHERE, 80, 1.0f);
        if (O.flash < 0.6f) O.flash = 0.6f;
        if (e->type != O_POWERUP) o_popup(e->x, e->y, pts);
        O.death_sound = 1;
    }
    if (e->type == O_KITE) {                        /* the tail is let go: it rushes in */
        for (int k = 0; k < 2; k++)
            if (e->child[k] >= 0 && O.e[e->child[k]].used) O.e[e->child[k]].parent = -1;
    }
    if (e->type == O_KITE_TAIL && e->parent >= 0) {
        oenemy *h = &O.e[e->parent];
        for (int k = 0; k < 2; k++) if (h->child[k] == (int)(e - O.e)) h->child[k] = -1;
    }
    e->used = 0;
}

/* ---- the player, the ring ----------------------------------------------- */

/* The original turns the player 5, 6, 7, 8, 5 ... sides, one more after
 * every ring (PlayerController.IterationType Count); here it stays a
 * hexagon, as the owner wants it. */
static void o_next_sides(void)
{
    O.sides = 6;
}

static void o_fire(void)
{
    if (!O.attacking) return;
    O.attacking = 0;
    float outer = (O_RING_R + O_RING_ACCENT) * O.power;
    float inner = (O_RING_R - (O.width + 6)) * O.power;
    if (inner < 0) inner = 0;
    int dead[O_MAX], k = 0;
    for (int i = 0; i < O_MAX; i++) {
        oenemy *e = &O.e[i];
        if (!e->used) continue;
        float n = o_poly_norm(e->x, e->y, O.attack_sides, O.rot);
        float h = o_kind[e->type].half;
        if (n + h < inner || n - h > outer) continue;      /* not touching the ring */
        e->hp--;
        e->damaged = 1;
        if (e->type == O_KITE && e->hp > 0) e->speed = 40;  /* DamagedMoveSpeed */
        if (e->hp <= 0) dead[k++] = i;
    }
    float mult = 1.0f + 0.1f * (k - 1);
    for (int i = 0; i < k; i++) if (O.e[dead[i]].used) o_kill(&O.e[dead[i]], mult, 1);
    /* the ring let go: it flies on outward as a white shock wave, bigger and
     * brighter the more it took; three or more: the screen flashes */
    static const unsigned char white[3] = { 255, 255, 255 };
    ofx *f = o_explode(0, 0, white, FX_SHOCK, outer, k ? 0.45f : 0.3f);
    if (f) { f->sides = O.attack_sides; f->spin = O.rot; f->rgb[0] = (unsigned char)(k > 3 ? 3 : k); }
    if (k >= 3) {
        o_explode(0, 0, white, FX_FLASH, 520, 0.3f);
        o_sparks(0, 0, 30, 700);
        O.flash = 1;
    }
    o_next_sides();
}

static void o_add_powerup(int type)
{
    static const char *const names[4] = { "", "Shield", "Attack Speed", "Attack Width" };
    static const float seconds[4] = { 0, 45, 15, 15 };
    O.powerup = type;
    O.powerup_t = seconds[type];
    O.label = names[type];
    O.label_t = 2.0f;
    play(G.s_bonus, 0.6f, 1.0f);
}

/* PlayerController.OnDamage */
static void o_touch(oenemy *e)
{
    e->has_hit = 1;
    if (e->type == O_POWERUP) {
        o_add_powerup(e->ptype);
        o_explode(0, 0, o_rgb(e), FX_RINGS, 40, 0.7f);   /* taken in */
        o_explode(0, 0, o_rgb(e), FX_FLASH, 90, 0.2f);
        O.flash = 1;
        o_kill(e, 1.0f, 0);                         /* scores 10, no popup */
    } else if (O.powerup == P_SHIELD) {
        O.powerup = P_NONE;
        O.powerup_t = 0;
        play(G.s_shieldhit, 0.7f, 1.0f);
        o_kill(e, 1.0f, 1);
    } else {
        static const unsigned char white[3] = { 255, 255, 255 };
        o_burst(0, 0, white, 60, 380);
        o_sparks(0, 0, 30, 600);
        G.dying = 1;
    }
}

/* ---- the step ----------------------------------------------------------- */

static void orig_init_game(int held)
{
    static int once;
    memset(O.e, 0, sizeof O.e);
    memset(O.part, 0, sizeof O.part);
    memset(O.pop, 0, sizeof O.pop);
    memset(O.fx, 0, sizeof O.fx);
    memset(O.shard, 0, sizeof O.shard);
    O.shake = O.shake_x = O.shake_y = 0;
    O.flash = 0;
    orig_view();
    O.gts = 1;
    O.game_time = 0;
    O.attacking = 0;
    O.powerup = P_NONE;
    O.powerup_t = 0;
    O.label_t = 0;
    O.next_spawn = 0.1f;                            /* the first try 0.1 s after the start */
    O.prev_held = held;                             /* the press that started the game is not a ring */
    O.demo_wait = 2.0f;
    if (!once) {                                    /* the original keeps these between games */
        once = 1;
        O.sides = 4;
        O.seed = 0x5EED1234u;
        for (int i = 0; i < O_SPAWN_TYPES; i++) O.since[i] = 99999;
        O.accent_h = 0.83f;
    }
    O.demo_careless = 20 + 15 * o_rand();
    o_next_sides();
}

/* The demo's player: hold when something comes near, let go when the ring
 * has passed it. */
static int o_demo_button(float dt)
{
    float nearest = 1e9f;
    for (int i = 0; i < O_MAX; i++) {
        oenemy *e = &O.e[i];
        if (!e->used || e->type == O_ALLY || e->type == O_POWERUP) continue;
        float n = o_poly_norm(e->x, e->y, O.sides, O.rot);
        if (n < nearest) nearest = n;
    }
    if (!O.attacking) {
        if (O.game_time > O.demo_careless) return 0;   /* so that the demo ends as games do */
        if (O.demo_wait > 0) { O.demo_wait -= dt; return 0; }
        if (nearest < 180 + 120 * o_rand()) { O.demo_target = 4 + 14 * o_rand(); return 1; }
        return 0;
    }
    /* let go once the ring's outer edge is just past the nearest one */
    if (O_RING_R * O.power >= nearest + O.demo_target) {
        O.demo_wait = 0.1f + 0.4f * o_rand();
        return 0;
    }
    return 1;
}

static void orig_step(vp_input *in, float dt)
{
    float dtg = dt * O.gts;
    O.spawned_sound = 0;
    O.death_sound = 0;
    O.gts += dt * O_SPEED_GROWTH;
    O.game_time += dt;
    O.rot += O_ROTATION * dtg;
    if (O.rot >= 360) O.rot -= 360;
    O.accent_h += 0.1f * dtg;
    if (O.accent_h >= 1) O.accent_h -= 1;

    /* the button: press starts a ring (firing one still held), release fires */
    int held = G.demo ? o_demo_button(dt) : buttons_held(in);
    if (held && !O.prev_held) {
        o_fire();
        O.attacking = 1;
        O.attack_sides = O.sides;
        O.power = 0;
        O.width = 0;
    } else if (!held && O.prev_held) {
        o_fire();
    }
    O.prev_held = held;
    if (O.attacking) {
        O.power += O_RING_GROW * (O.powerup == P_SPEED ? 2 : 1) * dtg;
        O.width += O_RING_WIDEN * (O.powerup == P_WIDTH ? 2 : 1) * dtg;
    }

    if (O.powerup != P_NONE && (O.powerup_t -= dtg) <= 0) O.powerup = P_NONE;
    if (O.label_t > 0) O.label_t -= dt;

    /* spawning */
    for (int i = 0; i < O_SPAWN_TYPES; i++) O.since[i] += dtg;
    if ((O.next_spawn -= dt) <= 0) o_spawn_tick();

    /* the enemies */
    for (int i = 0; i < O_MAX && !G.dying; i++) {
        oenemy *e = &O.e[i];
        if (!e->used) continue;
        e->t_alive += dtg;
        switch (e->type) {
        case O_KITE: {
            float ox = e->x, oy = e->y;
            e->r -= e->pull * dtg;
            e->ang += (e->ccw ? 1 : -1) * e->speed * dtg;
            o_orbit_place(e);
            float dx = e->x - ox, dy = e->y - oy, d = sqrtf(dx * dx + dy * dy);
            if (d > 0) { e->dir_x = dx / d; e->dir_y = dy / d; }
            break;
        }
        case O_CARRIER:
        case O_ALLY:
        case O_POWERUP:
            e->r -= e->pull * dtg;
            e->ang += (e->ccw ? 1 : -1) * e->speed * dtg;
            o_orbit_place(e);
            if (e->type == O_CARRIER && (e->next_shot -= dtg) <= 0) {
                oenemy *c = o_new(O_CARRIER_CHILD);
                if (c) { c->x = e->x; c->y = e->y; }
                float t = e->t_alive > 14.914f ? 14.914f : e->t_alive;
                e->next_shot = 1.0f / (0.3721f + (1.4985f - 0.3721f) * (t + 0.1299f) / (14.914f + 0.1299f));
            }
            if (e->type == O_ALLY && (e->next_pay -= dtg) <= 0) {   /* +4, +6, +8 ... every 2 s */
                e->next_pay = 2.0f;
                e->pay += 2;
                add_score(e->pay);
                o_popup(e->x, e->y, e->pay);
            }
            break;
        case O_KITE_TAIL:
            if (e->parent >= 0 && O.e[e->parent].used) {
                oenemy *h = &O.e[e->parent];
                e->x = h->x - e->follow * h->dir_x;
                e->y = h->y - e->follow * h->dir_y;
                break;
            }
            e->parent = -1;                         /* let go: it homes, as below */
            /* fall through */
        default: {                                  /* homing: straight to the centre */
            float d = sqrtf(e->x * e->x + e->y * e->y), step = e->speed * dtg;
            if (d <= step) { e->x = e->y = 0; }
            else { e->x -= e->x / d * step; e->y -= e->y / d * step; }
            if (e->type == O_STEALTH) {
                if (!e->unstealthed && d <= O_UNSTEALTH_R) e->unstealthed = 1;
                if (e->unstealthed && e->fade < 1) e->fade += dt / 0.5f;
            }
            break;
        }
        }
        /* touching the player's sides */
        if (!e->has_hit && e->type != O_ALLY &&
            o_poly_norm(e->x, e->y, O.sides, O.rot) - o_kind[e->type].half <= O_PLAYER_R)
            o_touch(e);
    }
    if (G.dying) O.attacking = 0;                   /* the held ring is dropped */

    for (int i = 0; i < O_PARTICLES; i++) {
        opart *p = &O.part[i];
        if (p->life <= 0) continue;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        p->vx *= 0.92f; p->vy *= 0.92f; p->vz *= 0.92f;
        p->life -= dt;
    }
    for (int i = 0; i < O_FX; i++) if (O.fx[i].t < O.fx[i].max) O.fx[i].t += dt;
    for (int i = 0; i < O_SHARDS; i++) {
        oshard *s = &O.shard[i];
        if (s->life <= 0) continue;
        s->x += s->vx * dt; s->y += s->vy * dt;
        s->vx *= 0.96f; s->vy *= 0.96f;
        float c = cosf(s->spin * dt), n = sinf(s->spin * dt), hx = s->hx;
        s->hx = hx * c - s->hy * n; s->hy = hx * n + s->hy * c;
        s->life -= dt;
    }
    if (O.flash > 0) O.flash -= dt * 4;
    for (int i = 0; i < O_POPUPS; i++) if (O.pop[i].t > 0) O.pop[i].t -= dt;
    if (O.death_sound) play(G.s_gameover, 0.5f, 1.0f);
}

static int orig_demo_over(void) { return O.game_time > O_DEMO_SECONDS; }

/* ---- drawing ------------------------------------------------------------ */

static int o_bx(float x) { return (int)lrintf(2048 + (x + O.shake_x) * O.kx); }
static int o_by(float y) { return (int)lrintf(2048 + (y + O.shake_y) * O.ky); }

static const unsigned char *o_rgb(const oenemy *e)
{
    static const unsigned char rgb[O_TYPES][3] = {
        [O_FIGHTER] = { 70, 170, 255 }, [O_DIVER] = { 255, 35, 35 }, [O_KITE] = { 40, 230, 60 },
        [O_CARRIER] = { 255, 140, 0 }, [O_ALLY] = { 240, 230, 0 }, [O_STEALTH] = { 70, 170, 255 },
        [O_POWERUP] = { 110, 255, 80 }, [O_KITE_TAIL] = { 120, 255, 120 }, [O_CARRIER_CHILD] = { 255, 40, 40 },
    };
    return rgb[e->type];
}

/* a polygon of apothem `a` px, n sides, the first side's middle at rot degrees */
static void o_poly(vp_dvg *d, float cx, float cy, float a, int n, float rot)
{
    float R = a / cosf(3.14159265f / n);
    for (int i = 0; i <= n; i++) {
        float ang = (rot + (i + 0.5f) * 360.0f / n) * 0.017453293f;
        int x = o_bx(cx + R * cosf(ang)), y = o_by(cy + R * sinf(ang));
        if (i == 0) vp_moveto(d, x, y);
        else vp_lineto(d, x, y);
    }
}

static void o_line(vp_dvg *d, float x0, float y0, float x1, float y1)
{
    vp_moveto(d, o_bx(x0), o_by(y0));
    vp_lineto(d, o_bx(x1), o_by(y1));
}

/* HSV hue h with the accent's own saturation and value (the original's
 * accent colour cycles its hue) */
static void o_accent(vp_dvg *d, float bright)
{
    float h = O.accent_h * 6, s = 0.485f, f = h - floorf(h);
    float p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f), r, g, b;
    switch ((int)h % 6) {
    case 0: r = 1; g = t; b = p; break;
    case 1: r = q; g = 1; b = p; break;
    case 2: r = p; g = 1; b = t; break;
    case 3: r = p; g = q; b = 1; break;
    case 4: r = t; g = p; b = 1; break;
    default: r = 1; g = p; b = q; break;
    }
    vp_color(d, (uint8_t)(255 * r * bright), (uint8_t)(255 * g * bright), (uint8_t)(255 * b * bright));
}

/* ---- 3D models ------------------------------------------------------------
 * The enemies and objects as wire models turning in space, in the
 * original's colours and sizes (a model's size is the old shape's): drawn
 * with a little perspective, their far edges dimmer. */

enum { M_JACK, M_DART, M_CUBE, M_OCTA, M_TETRA, M_GLOBE, M_BALL, M_STAR, M_COUNT };
#define OM_V 40
#define OM_E 40
#define O_PERSP 260.0f                  /* the eye's distance, px: the models' perspective */
typedef struct { int nv, ne; float v[OM_V][3]; unsigned char e[OM_E][2]; } omodel;
static omodel o_models[M_COUNT];

static int om_v(omodel *m, float x, float y, float z)
{
    m->v[m->nv][0] = x; m->v[m->nv][1] = y; m->v[m->nv][2] = z;
    return m->nv++;
}

static void om_e(omodel *m, int a, int b)
{
    m->e[m->ne][0] = (unsigned char)a; m->e[m->ne][1] = (unsigned char)b;
    m->ne++;
}

/* three great circles of n points (x-y, x-z, y-z) */
static void om_globe(omodel *m, int n)
{
    for (int c = 0; c < 3; c++) {
        int first = m->nv;
        for (int i = 0; i < n; i++) {
            float a = i * 6.2831853f / n, u = cosf(a), w = sinf(a);
            om_v(m, c == 2 ? 0 : u, c == 0 ? w : c == 2 ? u : 0, c == 0 ? 0 : w);
            om_e(m, first + i, first + (i + 1) % n);
        }
    }
}

static void o_models_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    omodel *m = &o_models[M_OCTA];                  /* the octahedron: points on the three axes */
    for (int i = 0; i < 6; i++) {
        float s = i & 1 ? -1.0f : 1.0f;
        om_v(m, i / 2 == 0 ? s : 0, i / 2 == 1 ? s : 0, i / 2 == 2 ? s : 0);
    }
    for (int i = 0; i < 6; i++) for (int j = i + 1; j < 6; j++) if (i / 2 != j / 2) om_e(m, i, j);
    o_models[M_JACK] = *m;                          /* the fighter's "+": the octahedron and its axes */
    m = &o_models[M_JACK];
    om_e(m, 0, 1); om_e(m, 2, 3); om_e(m, 4, 5);

    m = &o_models[M_DART];                          /* the diver: a pyramid, its point forward (+x) */
    om_v(m, 1, 0, 0);
    for (int i = 0; i < 4; i++) {
        float a = i * 1.5707963f;
        om_v(m, -1, 0.46f * cosf(a), 0.46f * sinf(a));
    }
    for (int i = 0; i < 4; i++) { om_e(m, 0, 1 + i); om_e(m, 1 + i, 1 + (i + 1) % 4); }

    m = &o_models[M_CUBE];
    for (int i = 0; i < 8; i++) om_v(m, i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
    for (int i = 0; i < 8; i++) for (int b = 1; b < 8; b <<= 1) if (!(i & b)) om_e(m, i, i | b);

    m = &o_models[M_TETRA];
    om_v(m, 1, 1, 1); om_v(m, 1, -1, -1); om_v(m, -1, 1, -1); om_v(m, -1, -1, 1);
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) om_e(m, i, j);

    om_globe(&o_models[M_GLOBE], 12);
    om_globe(&o_models[M_BALL], 8);

    m = &o_models[M_STAR];                          /* the ally: a five-pointed star with depth */
    for (int side = 0; side < 2; side++)
        for (int i = 0; i < 10; i++) {
            float a = 1.5707963f + i * 0.6283185f, r = i & 1 ? 0.42f : 1.0f;
            om_v(m, r * cosf(a), r * sinf(a), side ? -0.22f : 0.22f);
            om_e(m, side * 10 + i, side * 10 + (i + 1) % 10);
        }
    for (int i = 0; i < 10; i += 2) om_e(m, i, 10 + i);   /* the points joined front to back */
}

/* R = Rz(az) Ry(ay) Rx(ax) */
static void o_rot(float R[3][3], float ax, float ay, float az)
{
    float ca = cosf(ax), sa = sinf(ax), cb = cosf(ay), sb = sinf(ay), cc = cosf(az), sc = sinf(az);
    R[0][0] = cc * cb; R[0][1] = cc * sb * sa - sc * ca; R[0][2] = cc * sb * ca + sc * sa;
    R[1][0] = sc * cb; R[1][1] = sc * sb * sa + cc * ca; R[1][2] = sc * sb * ca - cc * sa;
    R[2][0] = -sb;     R[2][1] = cb * sa;                R[2][2] = cb * ca;
}

static void o_model(vp_dvg *d, int model, float x, float y, float s, float R[3][3],
                    const unsigned char *rgb, float bright)
{
    const omodel *m = &o_models[model];
    float p[OM_V][3];
    for (int i = 0; i < m->nv; i++) {
        const float *v = m->v[i];
        float X = (R[0][0] * v[0] + R[0][1] * v[1] + R[0][2] * v[2]) * s;
        float Y = (R[1][0] * v[0] + R[1][1] * v[1] + R[1][2] * v[2]) * s;
        float Z = R[2][0] * v[0] + R[2][1] * v[1] + R[2][2] * v[2];
        float k = O_PERSP / (O_PERSP - Z * s);
        p[i][0] = x + X * k; p[i][1] = y + Y * k; p[i][2] = Z;
    }
    if (O.shatter) {                                /* o_shatter: the edges become shards */
        for (int i = 0, j = 0; i < m->ne && j < O_SHARDS; j++) {
            oshard *s = &O.shard[j];
            if (s->life > 0) continue;
            const float *a = p[m->e[i][0]], *b = p[m->e[i][1]];
            float cx = (a[0] + b[0]) / 2, cy = (a[1] + b[1]) / 2, dx = cx - x, dy = cy - y;
            float l = sqrtf(dx * dx + dy * dy) + 0.01f, v = 140 + 220 * o_fx_rand();
            s->x = cx; s->y = cy; s->hx = (b[0] - a[0]) / 2; s->hy = (b[1] - a[1]) / 2;
            s->vx = dx / l * v + (o_fx_rand() - 0.5f) * 80; s->vy = dy / l * v + (o_fx_rand() - 0.5f) * 80;
            s->spin = (o_fx_rand() - 0.5f) * 24;
            s->max = s->life = 0.5f + 0.4f * o_fx_rand();
            memcpy(s->rgb, rgb, 3);
            i++;
        }
        return;
    }
    int level = -1;
    for (int i = 0; i < m->ne; i++) {
        const float *a = p[m->e[i][0]], *b = p[m->e[i][1]];
        float z = (a[2] + b[2]) * 0.5f;             /* -1 far .. 1 near: four brightnesses */
        int l = z < -0.5f ? 0 : z < 0 ? 1 : z < 0.5f ? 2 : 3;
        if (l != level) { color_scaled(d, rgb, bright * (0.4f + 0.2f * l)); level = l; }
        o_line(d, a[0], a[1], b[0], b[1]);
    }
}

static void o_draw_enemy(vp_dvg *d, const oenemy *e)
{
    const unsigned char *rgb = o_rgb(e);
    float x = e->x, y = e->y, t = e->t_alive, sp = e->spin0, R[3][3];
    o_models_init();
    switch (e->type) {
    case O_FIGHTER:
    case O_STEALTH: {
        /* the stealth fighter is the same in dark grey until it is near */
        float f = e->type == O_STEALTH ? 0.22f + 0.78f * (e->fade > 1 ? 1 : e->fade) : 1.0f;
        o_rot(R, sp + t * 1.3f, sp * 0.7f + t * 0.9f, t * 0.4f);
        o_model(d, M_JACK, x, y, 24, R, rgb, f);
        break;
    }
    case O_DIVER:                                   /* the dart points at the player, turning round its axis */
        o_rot(R, sp + t * 6.0f, 0, e->rot);
        o_model(d, M_DART, x, y, 24, R, rgb, 1);
        break;
    case O_KITE: {                                  /* a cube along its motion, a cube within until hit */
        static const unsigned char hurt[3] = { 255, 80, 80 };
        int flicker = e->damaged && (G.state_frames / 3) % 2;
        o_rot(R, sp + t * 2.5f, 0.6f, atan2f(e->dir_y, e->dir_x));
        o_model(d, M_CUBE, x, y, 16, R, flicker ? hurt : rgb, 1);
        if (!e->damaged) o_model(d, M_CUBE, x, y, 10, R, rgb, 0.8f);
        break;
    }
    case O_KITE_TAIL:
        o_rot(R, sp + t * 3.0f, t * 2.2f, sp);
        o_model(d, M_OCTA, x, y, 9, R, rgb, 1);
        break;
    case O_CARRIER:
        o_rot(R, sp + t * 0.8f, t * 0.5f, 0.4f);
        o_model(d, M_GLOBE, x, y, 21, R, rgb, 1);
        break;
    case O_CARRIER_CHILD:
        o_rot(R, sp + t * 5.0f, t * 4.0f, 0);
        o_model(d, M_TETRA, x, y, 6, R, rgb, 1);
        break;
    case O_ALLY:                                    /* the star turns like a coin */
        o_rot(R, 0.35f, sp + t * 2.0f, 0);
        o_model(d, M_STAR, x, y, 22, R, rgb, 1);
        break;
    case O_POWERUP:
        o_rot(R, sp + t * 1.6f, t * 1.1f, 0);
        o_model(d, M_BALL, x, y, 11, R, rgb, 1);
        o_rot(R, -t * 3, t * 2, 0);
        o_model(d, M_OCTA, x, y, 4, R, rgb, 1);
        break;
    }
}

/* An explosion (Tempest 2000's draw_prex and drawsphere): rings of 17, 15
 * and 13 dots, each twice as wide as the last, flashing between the
 * enemy's colour and white; or a sphere of 8 circles of 16 dots, turning.
 * Both grow to `size` and fade. */
static void o_dot(vp_dvg *d, float x, float y)
{
    int bx = o_bx(x), by = o_by(y);
    vp_moveto(d, bx, by);
    vp_lineto(d, bx, by);
}

static void o_draw_fx(vp_dvg *d, const ofx *f)
{
    if (f->t >= f->max) return;                     /* also the unused ones (max 0) */
    float a = f->t / f->max;
    float fade = 1 - a, grow = 1 - (1 - a) * (1 - a);
    unsigned char c[3];
    if (f->kind == FX_FLASH) {                      /* white rays, a star of light shrinking away */
        static const unsigned char white[3] = { 255, 255, 255 };
        int n = f->size > 200 ? 24 : 12;
        float r0 = f->size * 0.15f * a, r1 = f->size * (0.4f + 0.6f * a) * fade + r0;
        color_scaled(d, white, 1);
        for (int j = 0; j < n; j++) {
            float ang = f->spin + j * 6.2831853f / n + (j & 1) * 0.1f, len = j & 1 ? 0.6f : 1.0f;
            o_line(d, f->x + cosf(ang) * r0, f->y + sinf(ang) * r0,
                   f->x + cosf(ang) * (r0 + (r1 - r0) * len), f->y + sinf(ang) * (r0 + (r1 - r0) * len));
        }
        color_scaled(d, f->rgb, fade);
        o_poly(d, f->x, f->y, 6 + f->size * 0.12f * fade, 8, f->spin * 57.3f);
        return;
    }
    if (f->kind == FX_WAVE) {                       /* the death's wave: a hexagon to the screen's edge */
        static const unsigned char white[3] = { 255, 255, 255 };
        float r = f->size * grow;
        color_scaled(d, white, fade);
        o_poly(d, f->x, f->y, r, 6, f->spin);
        color_scaled(d, f->rgb, fade);
        o_poly(d, f->x, f->y, r * 0.93f, 6, f->spin);
        o_poly(d, f->x, f->y, r * 0.86f, 6, f->spin);
        return;
    }
    if (f->kind == FX_SHOCK) {                      /* the let-go ring flying on: white, thinning */
        static const unsigned char white[3] = { 255, 255, 255 };
        int kills = f->rgb[0];
        float r = f->size * (1 + (0.6f + 0.3f * kills) * grow);
        color_scaled(d, white, fade * (0.6f + 0.13f * kills));
        o_poly(d, f->x, f->y, r, f->sides, f->spin);
        if (fade > 0.4f) o_poly(d, f->x, f->y, r * 0.94f, f->sides, f->spin);
        if (kills) o_poly(d, f->x, f->y, r * 1.08f, f->sides, f->spin);
        return;
    }
    if (f->kind == FX_RINGS) {
        for (int i = 0; i < 3; i++) {
            float w = 0.5f + 0.5f * sinf(f->t * 30 + i * 2.1f), r = f->size * grow * (1 << i) / 4;
            int n = 17 - 2 * i;
            for (int k = 0; k < 3; k++) c[k] = (unsigned char)(f->rgb[k] + (255 - f->rgb[k]) * w);
            color_scaled(d, c, fade);
            for (int j = 0; j < n; j++) {
                float ang = f->spin + f->t * 1.5f + j * 6.2831853f / n;
                o_dot(d, f->x + cosf(ang) * r, f->y + sinf(ang) * r);
            }
        }
        return;
    }
    float r = f->size * grow;
    int level = -1;
    for (int ring = 0; ring < 8; ring++) {
        float th = f->spin + f->t * 2.0f + ring * 0.3926991f, ct = cosf(th), st = sinf(th);
        for (int k = 0; k < 16; k++) {
            float ang = k * 0.3926991f, sa = sinf(ang), ca = cosf(ang);
            float z = -sa * st;                     /* -1 far .. 1 near */
            int l = z < -0.33f ? 0 : z < 0.33f ? 1 : 2;
            if (l != level) { color_scaled(d, f->rgb, fade * (0.45f + 0.275f * l)); level = l; }
            float kp = O_PERSP / (O_PERSP - z * r * 0.5f);
            o_dot(d, f->x + sa * ct * r * kp, f->y + ca * r * kp);
        }
    }
}

/* The instructions page's pictures, at (bx, by) on the board: row 0..6 is
 * fighter, diver, stealth, kite with its tail, carrier, ally, power-up. */
static void orig_draw_icon(vp_dvg *d, int row, float bx, float by, int frames)
{
    static const int types[7] = { O_FIGHTER, O_DIVER, O_STEALTH, O_KITE, O_CARRIER, O_ALLY, O_POWERUP };
    oenemy e;
    memset(&e, 0, sizeof e);
    e.type = types[row];
    e.x = (bx - 2048) / O.kx; e.y = (by - 2048) / O.ky;
    e.t_alive = frames / (float)FPS;
    e.spin0 = row * 1.3f;
    e.dir_x = 1;
    e.fade = 0.5f + 0.5f * sinf(frames * 0.08f);   /* the stealth fades in and out */
    o_draw_enemy(d, &e);
    if (e.type == O_KITE) {                         /* its tail, trailing */
        float hx = e.x, hy = e.y;
        e.type = O_KITE_TAIL;
        for (int k = 0; k < 2; k++) {
            e.x = hx - 32 - 20 * k; e.y = hy - 5 - 4 * k;
            e.spin0 += 2;
            o_draw_enemy(d, &e);
        }
    }
}

/* The view: px to board units (the screen 720 px tall). Set at the start
 * and for every game. */
static void orig_view(void)
{
    float aspect = G.vertical ? 0.75f : 4.0f / 3.0f;
    O.hh = 360;
    O.hw = 360 * aspect;
    O.kx = 4096.0f / (2 * O.hw);
    O.ky = 4096.0f / (2 * O.hh);
}

/* The player at (cx, cy) px: its sides (6 px bars, green with the shield)
 * and the accent outline in its cycling colour; with `power` > 0 its ring:
 * a white band (120 - (width + 6) .. 120) x power, the accent outline 4 px
 * outside. The game and the title draw it the same. */
static void o_draw_player(vp_dvg *d, float cx, float cy, int sides, float rot, int shield, float power, float width)
{
    static const unsigned char white[3] = { 255, 255, 255 }, green[3] = { 120, 255, 140 };
    color_scaled(d, shield ? green : white, 1);
    o_poly(d, cx, cy, O_PLAYER_R, sides, rot);
    o_poly(d, cx, cy, O_PLAYER_R - 6, sides, rot);
    o_accent(d, 1);
    o_poly(d, cx, cy, O_ACCENT_R, sides, rot);
    if (power <= 0.02f) return;

    float outer = O_RING_R * power, inner = (O_RING_R - (width + 6)) * power;
    if (inner < 0) inner = 0;
    color_scaled(d, white, 1);
    o_poly(d, cx, cy, outer, sides, rot);
    o_poly(d, cx, cy, inner, sides, rot);
    float band = outer - inner;
    int fills = (int)(band / 9);                     /* fill the band: lines ~9 px apart */
    if (fills > 6) fills = 6;
    color_scaled(d, white, 0.6f);
    for (int k = 1; k < fills; k++) o_poly(d, cx, cy, inner + band * k / fills, sides, rot);
    for (int i = 0; i < sides; i++) {                /* and its corners */
        float a = (rot + (i + 0.5f) * 360.0f / sides) * 0.017453293f, c = 1 / cosf(3.14159265f / sides);
        o_line(d, cx + inner * c * cosf(a), cy + inner * c * sinf(a), cx + outer * c * cosf(a), cy + outer * c * sinf(a));
    }
    o_accent(d, 1);
    o_poly(d, cx, cy, (O_RING_R + O_RING_ACCENT) * power, sides, rot);
}

/* The title's player: the game's, its ring opening again and again. */
static void orig_draw_title_player(vp_dvg *d, float cy_board, int frames)
{
    float t = (frames % 60) / 50.0f;                 /* a ring opened for 0.8 s, let go, again */
    if (t > 0.8f) t = 0;                             /* (wider, it would cover the lettering) */
    O.accent_h = fmodf(0.83f + frames * 0.1f / FPS, 1.0f);
    o_draw_player(d, 0, (cy_board - 2048) / O.ky, 6, frames * O_ROTATION / FPS, 0,
                  t * O_RING_GROW, t * O_RING_WIDEN);
}

/* The attract's tutorial, a scripted scene of TUTOR_SECONDS: holding the
 * button opens the ring, letting go with a fighter in it destroys it; then
 * what helps: a star paying points, a green ball giving a shield. Drawn with
 * the game's own shapes; the captions are main.c's tutor_caption. */
#define TUTOR_SECONDS 16.0f
#define TUTOR_PY      (-40.0f)                      /* the player, px below the centre */

static void tutor_caption(vp_dvg *d, const char *one, const char *two, float alpha);

static void o_tutor_burst(vp_dvg *d, float x, float y, const unsigned char *rgb, float age)
{
    if (age < 0 || age > 0.8f) return;
    ofx f = { x, y, age, 0.8f, 40, 0, FX_RINGS, 6, { rgb[0], rgb[1], rgb[2] } };
    o_draw_fx(d, &f);
    ofx flash = { x, y, age, 0.18f, 75, 0.3f, FX_FLASH, 6, { rgb[0], rgb[1], rgb[2] } };
    o_draw_fx(d, &flash);
    color_scaled(d, rgb, 1 - age / 0.8f);
    for (int i = 0; i < 14; i++) {
        float a = i * 0.4488f + 0.2f, r = 260 * age * (1.2f - age), c = cosf(a), s = sinf(a);
        o_line(d, x + c * r, y + s * r, x + c * (r + 14), y + s * (r + 14));
    }
}

static void orig_draw_tutorial(vp_dvg *d, int frames)
{
    static const unsigned char white[3] = { 255, 255, 255 }, green[3] = { 120, 255, 140 };
    float t = frames / (float)FPS, rot = frames * O_ROTATION / FPS;
    oenemy e;
    O.accent_h = fmodf(0.83f + t * 0.1f, 1.0f);

    if (t < 7.5f) {
        /* a fighter flies in; the ring opens at 1 s (slower than in the
         * game, to be watched) and is let go at 4.8 s with the fighter in
         * its band */
        const float start = 1.0f, release = 4.8f, rate = 0.35f, widen = 5.0f;
        float held = (t < release ? t : release) - start;
        float power = t >= start && t < release ? held * rate : 0;
        o_draw_player(d, 0, TUTOR_PY, 6, rot, 0, power, held * widen);
        float ex = 330 - 40 * (t < release ? t : release);
        if (t < release) {
            memset(&e, 0, sizeof e);
            e.type = O_FIGHTER; e.x = ex; e.y = TUTOR_PY; e.t_alive = t;
            o_draw_enemy(d, &e);
        } else {
            o_tutor_burst(d, ex, TUTOR_PY, o_rgb(&(oenemy){ .type = O_FIGHTER }), t - release);
            ofx shock = { 0, TUTOR_PY, t - release, 0.45f, (O_RING_R + O_RING_ACCENT) * held * rate,
                          rot, FX_SHOCK, 6, { 1, 0, 0 } };
            o_draw_fx(d, &shock);
        }
        float fade = t > 6.9f ? (7.5f - t) / 0.6f : 1;
        if (t < 3.4f)
            tutor_caption(d, "Hold the button", "to open your ring", fade);
        else if (t < release + 0.2f)
            tutor_caption(d, "Let go when an enemy", "is inside the ring", fade);
        else
            tutor_caption(d, "Every enemy in the ring", "is destroyed", fade);
        return;
    }

    /* what helps: the star circles and pays, the green ball is let in */
    float s = t - 7.5f, hit = 6.5f;
    int shield = s >= hit;
    o_draw_player(d, 0, TUTOR_PY, 6, rot, shield, 0, 0);
    memset(&e, 0, sizeof e);
    e.type = O_ALLY;
    e.t_alive = s;
    e.x = 175 * cosf(2.6f + s * 0.55f); e.y = TUTOR_PY + 175 * sinf(2.6f + s * 0.55f);
    o_draw_enemy(d, &e);
    for (int k = 0; k < 2; k++) {                   /* "+4", "+6", fading as in the game */
        float age = s - (1.2f + 2.0f * k);
        if (age < 0 || age > 1.5f) continue;
        float px = 175 * cosf(2.6f + (s - age) * 0.55f), py = TUTOR_PY + 175 * sinf(2.6f + (s - age) * 0.55f);
        char txt[8];
        snprintf(txt, sizeof txt, "+%d", 4 + 2 * k);
        color_scaled(d, white, 1 - age / 1.5f);
        ui_text(d, txt, (float)o_bx(px), (float)o_by(py + 12 + age * 20), 24 * O.ky, VP_TEXT_CENTER);
    }
    if (s >= 3.5f && s < hit) {                     /* the green ball, straight in from the left */
        memset(&e, 0, sizeof e);
        e.type = O_POWERUP;
        e.t_alive = s;
        e.x = -520 + (520 - O_PLAYER_R) * (s - 3.5f) / (hit - 3.5f); e.y = TUTOR_PY;
        o_draw_enemy(d, &e);
    }
    if (shield && s < hit + 2.0f) {
        color_scaled(d, green, s < hit + 1.0f ? 1 : hit + 2.0f - s);
        ui_text(d, "Shield", 2048, (float)o_by(TUTOR_PY - 110), 40 * O.ky, VP_TEXT_CENTER);
    }
    float fade = s < 0.4f ? s / 0.4f : t > TUTOR_SECONDS - 0.6f ? (TUTOR_SECONDS - t) / 0.6f : 1;
    if (s < 3.5f)
        tutor_caption(d, "Stars are friends: let them", "live and they pay you points", fade);
    else
        tutor_caption(d, "Green balls: let them hit you", "for a shield, speed or a wider ring", fade);
}

/* A short path through n points (px): from the centre, each the nearest one
 * left. The beam's invisible moves cost the board as much as lines, and in
 * the arrays' own order it crossed the screen between scattered pieces;
 * a long frame is drawn faster, which made the picture twitch (main.c's
 * bg_draw does the same for the stars). */
static void o_path(int n, const float *x, const float *y, int *order)
{
    static unsigned char left[O_SHARDS > O_PARTICLES ? O_SHARDS : O_PARTICLES];
    float bx = 0, by = 0;
    for (int i = 0; i < n; i++) left[i] = 1;
    for (int k = 0; k < n; k++) {
        int best = -1;
        float bd = 0;
        for (int j = 0; j < n; j++) {
            if (!left[j]) continue;
            float dx = x[j] - bx, dy = y[j] - by, d2 = dx * dx + dy * dy;
            if (best < 0 || d2 < bd) { best = j; bd = d2; }
        }
        left[best] = 0;
        order[k] = best;
        bx = x[best]; by = y[best];
    }
}

static void o_draw_enemies(vp_dvg *d)
{
    float x[O_MAX], y[O_MAX];
    int idx[O_MAX], order[O_MAX], n = 0;
    for (int i = 0; i < O_MAX; i++)
        if (O.e[i].used) { x[n] = O.e[i].x; y[n] = O.e[i].y; idx[n++] = i; }
    o_path(n, x, y, order);
    for (int k = 0; k < n; k++) o_draw_enemy(d, &O.e[idx[order[k]]]);
}

/* the shards and the particles, each in a short path */
static void o_draw_debris(vp_dvg *d)
{
    static float x[O_SHARDS], y[O_SHARDS];
    static int idx[O_SHARDS], order[O_SHARDS];
    int n = 0;
    for (int i = 0; i < O_SHARDS; i++)
        if (O.shard[i].life > 0) { x[n] = O.shard[i].x; y[n] = O.shard[i].y; idx[n++] = i; }
    o_path(n, x, y, order);
    for (int k = 0; k < n; k++) {
        const oshard *s = &O.shard[idx[order[k]]];
        float f = s->life / s->max;
        color_scaled(d, s->rgb, f > 0.5f ? 1 : f * 2);
        o_line(d, s->x - s->hx, s->y - s->hy, s->x + s->hx, s->y + s->hy);
    }
    n = 0;
    for (int i = 0; i < O_PARTICLES; i++)
        if (O.part[i].life > 0) { x[n] = O.part[i].x; y[n] = O.part[i].y; idx[n++] = i; }
    o_path(n, x, y, order);
    for (int k = 0; k < n; k++) {                   /* nearer: longer and brighter */
        const opart *p = &O.part[idx[order[k]]];
        float kz = O_PERSP / (O_PERSP - (p->z > 200 ? 200 : p->z));
        color_scaled(d, p->rgb, p->life / p->max * (kz > 1.25f ? 1 : kz * 0.8f));
        o_line(d, p->x, p->y, p->x - p->vx * 0.03f * kz, p->y - p->vy * 0.03f * kz);
    }
}

static void orig_draw(vp_dvg *d)
{
    static const unsigned char white[3] = { 255, 255, 255 }, green[3] = { 120, 255, 140 };
    char s[16];

    bg_draw(d);
    o_draw_player(d, 0, 0, O.sides, O.rot, O.powerup == P_SHIELD,
                  O.attacking ? O.power : 0, O.width);
    if (O.flash > 0) {                              /* the player lights up after a kill */
        static const unsigned char white[3] = { 255, 255, 255 };
        color_scaled(d, white, O.flash > 1 ? 1 : O.flash);
        o_poly(d, 0, 0, O_PLAYER_R + 2, O.sides, O.rot);
        o_poly(d, 0, 0, O_ACCENT_R + 3 + 6 * (1 - O.flash), O.sides, O.rot);
    }

    o_draw_enemies(d);
    for (int i = 0; i < O_FX; i++) o_draw_fx(d, &O.fx[i]);
    o_draw_debris(d);
    for (int i = 0; i < O_POPUPS; i++) {             /* "+N", fading over 1.5 s */
        const opopup *p = &O.pop[i];
        if (p->t <= 0) continue;
        snprintf(s, sizeof s, "+%d", p->value);
        color_scaled(d, white, p->t / 1.5f);
        ui_text(d, s, (float)o_bx(p->x), (float)o_by(p->y + 12), 24 * O.ky, VP_TEXT_CENTER);
    }

    /* the power-up: its seconds on the player, its name when earned */
    if (O.powerup != P_NONE) {
        snprintf(s, sizeof s, "%d", (int)ceilf(O.powerup_t));
        color_scaled(d, green, 1);
        ui_text(d, s, 2048, 2048 - 6 * O.ky, 12 * O.ky, VP_TEXT_CENTER);
    }
    if (O.label_t > 0) {
        color_scaled(d, green, O.label_t > 1 ? 1 : O.label_t);
        ui_text(d, O.label, 2048, 2048 + 230 * O.ky, 40 * O.ky, VP_TEXT_CENTER);
    }
}

/* ---- the death -----------------------------------------------------------
 * A big explosion: the player bursts (its outline into shards, a white star
 * of light, a sphere and rings of dots, sparks), the screen shakes, waves of
 * white and colour roll out to the edges, and every enemy on the screen
 * explodes as the first wave reaches it. Blasts follow round the centre.
 * Runs on the game over page in both styles (main.c's game_over, update,
 * draw); the Vectrex style's enemies are not in O.e, so only the blasts. */

#define O_WAVE_SPEED 700.0f                         /* the first wave, px/s: what it reaches explodes */

static void o_shatter_poly(float a, int n, float rot, const unsigned char *rgb)
{
    float R = a / cosf(3.14159265f / n);
    for (int i = 0, j = 0; i < n && j < O_SHARDS; j++) {
        oshard *s = &O.shard[j];
        if (s->life > 0) continue;
        float a0 = (rot + (i + 0.5f) * 360.0f / n) * 0.017453293f, a1 = (rot + (i + 1.5f) * 360.0f / n) * 0.017453293f;
        float x0 = R * cosf(a0), y0 = R * sinf(a0), x1 = R * cosf(a1), y1 = R * sinf(a1);
        float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, l = sqrtf(cx * cx + cy * cy) + 0.01f, v = 200 + 220 * o_fx_rand();
        s->x = cx; s->y = cy; s->hx = (x1 - x0) / 2; s->hy = (y1 - y0) / 2;
        s->vx = cx / l * v; s->vy = cy / l * v;
        s->spin = (o_fx_rand() - 0.5f) * 16;
        s->max = s->life = 1.2f + 0.6f * o_fx_rand();
        memcpy(s->rgb, rgb, 3);
        i++;
    }
}

static void o_add_blast(float at, float x, float y, int kind, const unsigned char *rgb)
{
    for (int i = 0; i < O_BLASTS; i++) {
        oblast *b = &O.blast[i];
        if (!b->done) continue;
        b->at = at; b->x = x; b->y = y; b->kind = kind; b->done = 0;
        memcpy(b->rgb, rgb, 3);
        return;
    }
}

static void o_blast(const oblast *b)
{
    static const unsigned char white[3] = { 255, 255, 255 };
    if (b->kind == 1) {                             /* a wave to the edge */
        ofx *f = o_explode(b->x, b->y, b->rgb, FX_WAVE, O.hw * 1.35f, 0.9f);
        if (f) f->spin = O.rot;
        return;
    }
    o_explode(b->x, b->y, white, FX_FLASH, 180, 0.25f);
    o_explode(b->x, b->y, b->rgb, FX_SPHERE, 110, 1.0f);
    o_explode(b->x, b->y, b->rgb, FX_RINGS, 90, 0.8f);
    o_burst(b->x, b->y, b->rgb, 30, 420);
    o_sparks(b->x, b->y, 16, 650);
    if (O.shake < 7) O.shake = 7;
    play(G.s_explode, 0.45f, 0.7f + 0.3f * o_fx_rand());
}

static void orig_death_start(void)
{
    static const unsigned char white[3] = { 255, 255, 255 }, red[3] = { 255, 60, 40 },
                               orange[3] = { 255, 150, 30 };
    unsigned char accent[3];
    float h = O.accent_h * 6, f = h - floorf(h), q = 1 - 0.485f * f, tt = 1 - 0.485f * (1 - f);
    float rgb[6][3] = { { 1, tt, 0.515f }, { q, 1, 0.515f }, { 0.515f, 1, tt }, { 0.515f, q, 1 }, { tt, 0.515f, 1 }, { 1, 0.515f, q } };
    for (int k = 0; k < 3; k++) accent[k] = (unsigned char)(255 * rgb[(int)h % 6][k]);

    orig_view();
    O.death_t = 0;
    for (int i = 0; i < O_BLASTS; i++) O.blast[i].done = 1;
    O.attacking = 0;
    o_shatter_poly(O_PLAYER_R, 6, O.rot, white);
    o_shatter_poly(O_PLAYER_R - 6, 6, O.rot, white);
    o_shatter_poly(O_ACCENT_R, 6, O.rot, accent);
    o_explode(0, 0, white, FX_FLASH, 700, 0.45f);
    o_explode(0, 0, white, FX_SPHERE, 190, 1.5f);
    o_explode(0, 0, accent, FX_RINGS, 200, 1.2f);
    o_burst(0, 0, white, 70, 450);
    o_burst(0, 0, accent, 40, 300);
    o_sparks(0, 0, 50, 800);
    O.shake = 16;
    /* the waves, then the blasts round the centre */
    o_add_blast(0.00f, 0, 0, 1, white);
    o_add_blast(0.18f, 0, 0, 1, accent);
    o_add_blast(0.40f, 0, 0, 1, red);
    static const float when[5] = { 0.25f, 0.45f, 0.7f, 0.95f, 1.25f };
    const unsigned char *cols[5] = { orange, accent, red, white, orange };
    for (int i = 0; i < 5; i++) {
        float a = o_fx_rand() * 6.2831853f, r = 40 + 90 * o_fx_rand();
        o_add_blast(when[i], cosf(a) * r, sinf(a) * r, 0, cols[i]);
    }
}

static void orig_death_step(float dt)
{
    O.death_t += dt;
    for (int i = 0; i < O_BLASTS; i++) {
        oblast *b = &O.blast[i];
        if (!b->done && O.death_t >= b->at) { b->done = 1; o_blast(b); }
    }
    /* the first wave sweeps the screen clean */
    float reach = O.death_t * O_WAVE_SPEED;
    int sounds = 0;
    for (int i = 0; i < O_MAX; i++) {
        oenemy *e = &O.e[i];
        if (!e->used || sqrtf(e->x * e->x + e->y * e->y) > reach) continue;
        int big = e->type == O_CARRIER || e->type == O_KITE;
        o_shatter(e);
        o_burst(e->x, e->y, o_rgb(e), big ? 30 : 18, 340);
        o_sparks(e->x, e->y, 8, 520);
        o_explode(e->x, e->y, o_rgb(e), FX_RINGS, big ? 50 : 34, 0.8f);
        if (big) o_explode(e->x, e->y, o_rgb(e), FX_SPHERE, 80, 1.0f);
        if (sounds++ < 2) play(G.s_explode, 0.3f, 1.0f + 0.4f * o_fx_rand());
        e->used = 0;
    }
    for (int i = 0; i < O_PARTICLES; i++) {
        opart *p = &O.part[i];
        if (p->life <= 0) continue;
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        p->vx *= 0.93f; p->vy *= 0.93f; p->vz *= 0.93f;
        p->life -= dt;
    }
    for (int i = 0; i < O_FX; i++) if (O.fx[i].t < O.fx[i].max) O.fx[i].t += dt;
    for (int i = 0; i < O_SHARDS; i++) {
        oshard *s = &O.shard[i];
        if (s->life <= 0) continue;
        s->x += s->vx * dt; s->y += s->vy * dt;
        s->vx *= 0.97f; s->vy *= 0.97f;
        float c = cosf(s->spin * dt), n = sinf(s->spin * dt), hx = s->hx;
        s->hx = hx * c - s->hy * n; s->hy = hx * n + s->hy * c;
        s->life -= dt;
    }
    O.shake -= dt * 14;                              /* it dies down in about a second */
    if (O.shake < 0) O.shake = 0;
    O.shake_x = (o_fx_rand() - 0.5f) * 2 * O.shake;
    O.shake_y = (o_fx_rand() - 0.5f) * 2 * O.shake;
}

static void orig_death_draw(vp_dvg *d)
{
    o_draw_enemies(d);                              /* until the wave takes them */
    for (int i = 0; i < O_FX; i++) o_draw_fx(d, &O.fx[i]);
    o_draw_debris(d);
}
