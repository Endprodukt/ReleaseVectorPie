/* main.c — Release for VectorPie.
 *
 * A port of Malban's Vectrex version of Release (public domain, ported with
 * the permission of Gimogames). One button: press to grow the shield,
 * release to destroy everything inside it. Enemies close in from all sides.
 *
 * The game logic runs at 50 Hz, one update per Vectrex frame, in the
 * original units (see release.h). Where the original has quirks they are
 * kept; docs/spec_core.md and docs/spec_objects.md say where each rule
 * comes from.
 *
 * Controls: P1 buttons 1-4 = shield (all the same, as on the Vectrex),
 * START 1 / button 1 = play, button 2 on the title = high scores,
 * the cabinet's exit (Esc) = back to the menu.
 */
#include <vectorpie/vp.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "release.h"
#include "service.h"
#include "vpbind.h"
#include "font_xspace.inc"

#define FPS                 50
#define MAX_OBJECTS         38      /* the original pool */
#define POOL                64      /* slots: removed ones are reused after the frame */
#define BASE_SCALE          5
#define SPAWN_MAX           200
#define SHIELD_MAX          208
#define SHIELD_VARIANCE     10
#define SHIELD_DELAY_TICKS  7
#define STARLET_SCORE_DELAY 75
#define BOMB_RELOAD_REDUCTION 25
#define EXPLOSION_MAX_INIT  40
#define SCORE_DISPLAY_TIME  50
#define STARFIELD_MAX       10
/* the demo starts after the title's three pages (TITLE_*_FRAMES) */
#define ENTRY_TIMEOUT       30.0f             /* initials: seconds, shown counting down */
#define DEMO_AFTER_FRAMES   (TITLE_LOGO_FRAMES + TITLE_TUTOR_FRAMES + TITLE_HELP_FRAMES + TITLE_SCORE_FRAMES)

enum { BONUS_NONE, BONUS_SHIELD, BONUS_EXPAND, BONUS_FASTER };

enum { T_X, T_HX, T_HUNTER, T_BOMBER, T_SHOT, T_DRAGON, T_CHILD, T_STARLET, T_BONUS,
       T_HITTABLE,                       /* types below this one the shield can hit */
       T_EXPLOSION = T_HITTABLE, T_POPUP, T_STARFIELD, T_TYPES };

enum { EX_IMPLODE, EX_DIAMONDS, EX_CROSSES, EX_OUTLINE, EX_SHOT, EX_STAR, EX_BRIGHT, EX_DOTS };

enum { ST_TITLE, ST_PLAY, ST_GAMEOVER, ST_ENTRY, ST_HISCORE, ST_MENU };

/* Colors per object type, from the Android original (its tutorial screen):
 * the Vectrex is monochrome. */
static const unsigned char type_rgb[T_TYPES][3] = {
    [T_X]        = {  70, 170, 255 },   /* blue "+" */
    [T_HX]       = {  70, 150, 230 },   /* the same, nearly invisible far out */
    [T_HUNTER]   = { 255,  35,  35 },   /* red triangle */
    [T_BOMBER]   = { 255, 140,   0 },   /* orange circle */
    [T_SHOT]     = { 255, 190,  60 },   /* what the bomber deploys */
    [T_DRAGON]   = {  40, 230,  60 },   /* green square with a tail */
    [T_CHILD]    = { 120, 255, 120 },
    [T_STARLET]  = { 240, 230,   0 },   /* yellow star */
    [T_BONUS]    = { 110, 255,  80 },   /* green ball */
    [T_EXPLOSION]= { 255, 255, 255 },
    [T_POPUP]    = { 255, 255, 255 },
    [T_STARFIELD]= { 200, 200, 220 },   /* (--classic; with effects: many colors) */
};

static const unsigned char score_rgb[3]  = { 210, 130, 255 };  /* the original's score */
static const unsigned char record_rgb[3] = { 255, 110, 110 };  /* and its best score */

typedef struct obj {
    int used, dead, ending;
    int type;
    int scale, angle;
    int tick, delta;
    int anim, anim_counter;
    int intensity;
    /* bomber */
    int shot_counter, shot_reset;
    /* dragon */
    int ang_counter, sc_counter, damaged, child[2];
    /* dragon child */
    int parent, ang_off, sc_off, wiggle, wdir, freed;
    /* starlet */
    int star_id, ang_base, score_counter, value;
    /* explosion */
    int victim, etype, escale, eint, shrinking;
    /* popup */
    int countdown, star_popup, mult;
    /* starfield: 4 stars, pos -1 = not born yet (scale is then the delay) */
    int sf_scale[4], sf_pos[4], tint;
} obj;

static struct {
    int state, state_frames;
    int demo;

    /* the pool, in list (spawn) order */
    obj pool[POOL];
    int order[POOL], n_order;
    int object_count;

    /* frame counter and random numbers (getButtonState) */
    unsigned recal;
    unsigned char rnd1, rnd2;
    unsigned char btn_hist;

    /* base and shield */
    int base_angle, starlet_angle, sided;
    int shield_start, shield_width, inner, osc_forth, osc_back, cfd, cbd;
    int shield_active, shield_visible, shield_delay;
    int minor_counter, width_counter;

    /* phases and spawning */
    int phase_count, start_phase;
    const phase_rec *cur;
    phase_rec P;                        /* the live values (bonuses change them) */
    int spawn_timer, spawn_reset, spawn_rest_counter, spawn_inc_delay;
    int min_spawn_reset, max_spawn_inc, spawn_count;
    int starlet_count, star_flags;
    int starfields, starfield_max, star_swirl;
    int expl_active, expl_max;

    /* bonus */
    int bonus_counter, bonus_active, bonus_destroyed, timer_active, icon_s;

    int score, dead_enemies, dying, mute_spawn;

    /* demo AI */
    int demo_wait, demo_held;

    /* game over / entry: three letters with one button and START */
    char entry_name[4];
    int entry_pos, entry_flash[3];      /* frames left of a taken letter's ring */
    int best;                           /* the table's best score */
    float entry_time, entry_repeat;
    int last_rank;

    /* the settings (the service menu's, kept in save/settings.ini) */
    service_settings set;
    int classic_arg;                  /* --classic: no effects, whatever the setting */

    /* drawing */
    float ax, ay;                       /* board units per raw unit, per axis */
    int vertical;

    /* effects (--classic turns them off) */
    int fx;
    int danger_scale;                   /* the nearest enemy's SCALE */
    int announce_frames;
    char announce[24];

    /* sound */
    vp_audio *audio;
    vp_voice music;
    int music_track, orig_spawn;
    vp_sound *s_spawn, *s_release, *s_explode, *s_star, *s_phase, *s_bonus,
             *s_dragonhit, *s_shot, *s_gameover, *s_shieldhit, *s_mystery;
} G;

static int cir_x[383], cir_y[383];      /* the circle table, radius 108 */

/* The Android original's gameplay (original.c, included further down):
 * played when the style is "original" (G.fx), this file's Vectrex logic
 * otherwise. */
static void orig_init_game(int held);
static void orig_step(vp_input *in, float dt);
static void orig_draw(vp_dvg *d);
static int  orig_demo_over(void);
static void orig_view(void);
static void orig_draw_title_player(vp_dvg *d, float cy_board, int frames);
static void orig_draw_icon(vp_dvg *d, int row, float bx, float by, int frames);
static void orig_death_start(void);             /* the death's firework, both styles */
static void orig_death_step(float dt);
static void orig_death_draw(vp_dvg *d);
static void orig_draw_tutorial(vp_dvg *d, int frames);

/* The Android original's shapes, made at start (release.h's format): a
 * square with a tail, a circle, a ball. --classic keeps the Vectrex ones. */
static short orig_square[4][24], orig_child[16], orig_circle[40], orig_ball[96];

static short *put_poly(short *p, int n, float r, float rot)
{
    *p++ = (short)(n + 1);
    for (int i = 0; i <= n; i++) {
        float a = rot + i * 6.2831853f / n;
        *p++ = (short)lrintf(sinf(a) * r);
        *p++ = (short)lrintf(cosf(a) * r);
    }
    return p;
}

static void make_shapes(void)
{
    static const int half[4] = { 74, 70, 66, 70 };      /* the dragon's pulse */
    for (int k = 0; k < 4; k++) {                       /* a square, its neon edge doubled */
        short *p = put_poly(orig_square[k], 4, half[k] * 1.4142f, 0.785398f);
        p = put_poly(p, 4, half[k] * 1.4142f * 0.8f, 0.785398f);
        *p = 0;
    }
    *put_poly(orig_child, 4, 22 * 1.4142f, 0.785398f) = 0;
    *put_poly(orig_circle, 16, 66, 0) = 0;
    short *p = put_poly(orig_ball, 16, 48, 0);          /* a filled ball: rings inside */
    p = put_poly(p, 12, 32, 0);
    p = put_poly(p, 8, 16, 0);
    *p = 0;
}

static const short *look_dragon(int anim) { return G.fx ? orig_square[anim & 3] : shape_dragon[anim & 3]; }
static const short *look_child(void)      { return G.fx ? orig_child : shape_child; }
static const short *look_bomber(int anim) { return G.fx ? orig_circle : shape_bomber[anim % 9]; }
static const short *look_bonus(int anim)  { return G.fx ? orig_ball : shape_bonus[anim % 17]; }

/* ======================================================================= */
/* small helpers                                                            */

/* The attract pages (title, instructions, high scores), the demo and the
 * service menu: silent when the operator turns attract sound off. */
static int in_attract(void)
{
    return G.state == ST_TITLE || G.state == ST_MENU ||
           (G.state == ST_HISCORE && G.last_rank < 0) ||
           ((G.state == ST_PLAY || G.state == ST_GAMEOVER) && G.demo);
}

static void play(vp_sound *s, float vol, float pitch)
{
    if (!G.set.attract_sound && in_attract()) return;
    if (G.audio && s) vp_sound_play(G.audio, s, vol * G.set.effects_volume / 100.0f, pitch, 0);
}

static int ci(int angle)                /* circle index from a doubled-degree angle */
{
    int k = angle >> 1;
    if (k < 0) k = 0;
    if (k > 382) k %= 360;
    return k;
}

static void add_score(int n)
{
    G.score += n;
    if (G.score > 999999) G.score = 999999;
}

/* The random generators of getButtonState: one of the two per frame. */
static void rng_step(void)
{
    unsigned lo = G.recal & 0xFF, hi = (G.recal >> 8) & 0xFF;
    unsigned a = (lo + hi) & 0xFF;
    unsigned t = a + G.btn_hist;
    unsigned c = (t >> 8) & 1;
    a = t & 0xFF;
    if (lo & 1) { a ^= G.rnd1; G.rnd1 = (unsigned char)((a << 1) | c); }
    else        { a ^= G.rnd2; G.rnd2 = (unsigned char)((a << 1) | c); }
}

static void rng_mix(void)               /* spawnStarfield's extra mixing */
{
    unsigned char a = (unsigned char)((G.rnd1 << 1) | (G.rnd1 >> 7));
    a ^= G.rnd2;
    a = (unsigned char)(a + G.rnd2);
    a ^= (unsigned char)(G.recal & 0xFF);
    G.rnd2 = a;
}

/* ======================================================================= */
/* effects: particles, shock rings, announcements. Purely visual, with
 * their own random numbers, so the game plays exactly as without them.     */

#define MAX_PARTICLES 192
#define MAX_RINGS     4

typedef struct { float x, y, vx, vy; int life, max_life; unsigned char rgb[3]; } particle;
typedef struct { float r; int life; } shock_ring;

static particle parts[MAX_PARTICLES];
static shock_ring rings[MAX_RINGS];
static unsigned fx_seed = 12345;

/* the bonus types' colors: none, shield, expand, faster */
static const unsigned char bonus_rgb[4][3] = {
    { 90, 255, 255 }, { 255, 200, 40 }, { 90, 255, 120 }, { 255, 110, 40 }
};

static float fx_rand(void)
{
    fx_seed = fx_seed * 1664525u + 1013904223u;
    return (float)(fx_seed >> 8) / 16777216.0f;
}

/* `count` sparks flying out from an object's position (raw speed per frame). */
static void fx_burst(int angle, int scale, const unsigned char *rgb, int count, float speed)
{
    if (!G.fx) return;
    int k = ci(angle);
    float x = (float)cir_x[k] * scale, y = (float)cir_y[k] * scale;
    for (int i = 0, n = 0; i < MAX_PARTICLES && n < count; i++) {
        particle *p = &parts[i];
        if (p->life > 0) continue;
        float a = fx_rand() * 6.2831853f, v = speed * (0.35f + 0.65f * fx_rand());
        p->x = x;
        p->y = y;
        p->vx = cosf(a) * v;
        p->vy = sinf(a) * v;
        p->max_life = p->life = 18 + (int)(fx_rand() * 22);
        memcpy(p->rgb, rgb, 3);
        n++;
    }
}

/* A ring that runs outward from the released shield. */
static void fx_shock(int radius)
{
    if (!G.fx) return;
    for (int i = 0; i < MAX_RINGS; i++) {
        if (rings[i].life > 0) continue;
        rings[i].r = (float)radius;
        rings[i].life = 14;
        return;
    }
}

static void fx_update(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle *p = &parts[i];
        if (p->life <= 0) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->vx *= 0.93f;
        p->vy *= 0.93f;
        p->life--;
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        if (rings[i].life <= 0) continue;
        rings[i].r += 7;
        rings[i].life--;
    }
    if (G.announce_frames > 0) G.announce_frames--;
}

static void fx_clear(void)
{
    memset(parts, 0, sizeof parts);
    memset(rings, 0, sizeof rings);
    G.announce_frames = 0;
}

/* ======================================================================= */
/* objects                                                                  */

static obj *obj_new(int type)
{
    if (G.object_count >= MAX_OBJECTS) return NULL;
    for (int i = 0; i < POOL; i++) {
        if (!G.pool[i].used) {
            obj *o = &G.pool[i];
            memset(o, 0, sizeof *o);
            o->used = 1;
            o->type = type;
            o->parent = o->child[0] = o->child[1] = -1;
            G.order[G.n_order++] = i;
            G.object_count++;
            return o;
        }
    }
    return NULL;
}

static void obj_remove(obj *o)
{
    if (o->dead) return;
    if (o->type == T_STARFIELD) G.starfields--;
    o->dead = 1;
    G.object_count--;
}

static int obj_index(const obj *o) { return (int)(o - G.pool); }

static void compact(void)
{
    int n = 0;
    for (int i = 0; i < G.n_order; i++) {
        obj *o = &G.pool[G.order[i]];
        if (o->dead) o->used = 0;
        else G.order[n++] = G.order[i];
    }
    G.n_order = n;
}

static void popup(int angle, int scale, int value, int star, int mult)
{
    obj *o = obj_new(T_POPUP);
    if (!o) return;                     /* the points still count */
    o->angle = angle;
    o->scale = scale;
    o->value = value;
    o->star_popup = star;
    o->mult = mult;
}

static void free_children(obj *dragon)
{
    for (int k = 0; k < 2; k++) {
        int c = dragon->child[k];
        if (c >= 0 && G.pool[c].used && !G.pool[c].dead && G.pool[c].type == T_CHILD)
            G.pool[c].freed = 1;
        dragon->child[k] = -1;
    }
}

/* ======================================================================= */
/* phases                                                                   */

static void init_phase(const phase_rec *rec)
{
    G.cur = rec;
    G.minor_counter = rec->minor_reset;
    if (rec->sp_reset   != -1) G.spawn_reset = G.spawn_timer = rec->sp_reset;
    if (rec->sp_inc     != -1) G.spawn_inc_delay = rec->sp_inc;
    if (rec->sp_min     != -1) G.min_spawn_reset = rec->sp_min;
    if (rec->sp_max_inc != -1) G.max_spawn_inc = rec->sp_max_inc;
    G.P = *rec;
    if (G.bonus_active == BONUS_FASTER) {
        G.P.speed <<= 1;
        G.P.w_adder <<= 1;
    } else if (G.bonus_active == BONUS_EXPAND) {
        G.P.w_growth = 1;
        G.P.w_adder = G.P.speed;
    }
}

static void check_stage(void)
{
    if (G.P.next_at == 0) return;
    if (G.spawn_count <= G.P.next_at) return;
    G.spawn_count = 0;
    if (G.phase_count + 1 >= PHASE_COUNT) return;
    G.phase_count++;
    play(G.s_phase, 0.6f, 1.0f);
    init_phase(phase_list[G.phase_count]);

    int al = G.P.allow;
    const char *name = al == ALLOW_HUNTER ? "Hunter Raid" :
                       al == (ALLOW_BOMBER | ALLOW_HIDDEN_X) ? "Bomber Raid" :
                       al == ALLOW_DRAGON ? "Dragon Raid" :
                       al == ALLOW_SQUAD ? "Squad Raid" : NULL;
    if (name) snprintf(G.announce, sizeof G.announce, "%s", name);
    else snprintf(G.announce, sizeof G.announce, "Phase %d", G.phase_count + 1);
    G.announce_frames = 100;
}

/* ======================================================================= */
/* bonus                                                                    */

static void init_bonus(void)
{
    G.bonus_destroyed = 0;
    G.bonus_counter = FPS * G.P.bonus_time;
    G.timer_active = 1;
    int t = (G.rnd2 & 3) + 1;
    if (t == 4) t = BONUS_SHIELD;
    if (t == BONUS_FASTER) {
        G.P.speed <<= 1;
        G.P.w_adder <<= 1;
    } else if (t == BONUS_EXPAND) {
        G.P.w_growth = 1;
        G.P.w_adder = G.P.speed;
    }
    G.bonus_active = t;
    G.icon_s = 1;
    play(G.s_bonus, 0.7f, 1.0f);
    fx_burst(0, 0, bonus_rgb[t], 24, 200);
}

static void end_bonus(void)
{
    if (G.bonus_active == BONUS_FASTER) {
        G.P.speed = G.cur->speed;
        G.P.w_adder = G.cur->w_adder;
    } else if (G.bonus_active == BONUS_EXPAND) {
        G.P.w_growth = G.cur->w_growth;
        G.P.w_adder = G.cur->w_adder;
    }
    G.bonus_active = BONUS_NONE;
    G.timer_active = 0;
}

/* An enemy reached the base (gameOver, VRelease.asm:2095). */
static void hit_base(obj *o)
{
    if (G.bonus_active == BONUS_SHIELD) {
        G.bonus_active = BONUS_NONE;
        G.bonus_counter = 1;            /* the timer ends the bonus next frame */
        fx_burst(0, 0, bonus_rgb[BONUS_SHIELD], 24, 260);
        obj_remove(o);
        play(G.s_shieldhit, 0.8f, 1.0f);
        return;
    }
    G.dying = 1;
}

/* SCALE -= delta; reaching the base (<= BASE_SCALE + 3) ends it. */
static int move_in(obj *o, int delta)
{
    int ns = o->scale - delta;
    if (ns <= BASE_SCALE + 3) { hit_base(o); return 1; }
    o->scale = ns;
    return 0;
}

/* ======================================================================= */
/* spawning                                                                 */

static void spawn_sound(float pitch)
{
    if (G.mute_spawn) return;
    if (G.orig_spawn) play(G.s_spawn, 0.5f, 1.0f);   /* the original has one sound for all */
    else play(G.s_spawn, 0.35f, pitch);
}

static int angle_0_762(void) { return (G.rnd2 & 0x7F) * 6; }

static obj *spawn_x(void)
{
    obj *o = obj_new(T_X);
    if (!o) return NULL;
    o->scale = SPAWN_MAX;
    o->angle = angle_0_762();
    o->tick = G.P.x_delay;
    o->delta = G.P.x_addi;
    o->anim_counter = 2;
    spawn_sound(1.0f);
    return o;
}

static obj *spawn_hx(void)
{
    obj *o = obj_new(T_HX);
    if (!o) return NULL;
    o->scale = SPAWN_MAX;
    o->angle = angle_0_762();
    o->tick = G.P.hx_delay;
    o->delta = 1;                       /* HX_addi, forced by initPhase */
    o->anim_counter = 2;
    spawn_sound(0.9f);
    return o;
}

static obj *spawn_hunter(void)
{
    obj *o = obj_new(T_HUNTER);
    if (!o) return NULL;
    int r = G.rnd2 & 0x7F;
    if (r >= 120) r -= 120;
    o->scale = SPAWN_MAX;
    o->angle = ((r * 6) >> 5) * 32;
    o->tick = 0;                        /* Hunter_add_delay, forced by initPhase */
    o->delta = G.P.hunter_addi;
    spawn_sound(1.4f);
    return o;
}

static obj *spawn_bomber(void)
{
    obj *o = obj_new(T_BOMBER);
    if (!o) return NULL;
    unsigned char b = (unsigned char)((G.rnd1 << 4) | (G.rnd1 >> 4));
    b ^= G.rnd2;
    b = (unsigned char)(b + G.rnd2);
    b ^= (unsigned char)(G.recal & 0xFF);
    o->scale = 100 + (G.rnd2 & 31);
    o->angle = (b & 0x7F) * 6;
    o->tick = G.P.bomber_delay;
    o->anim_counter = 2;
    o->shot_counter = o->shot_reset = G.P.bomber_start;
    spawn_sound(0.7f);
    return o;
}

static void spawn_child(obj *dragon, int k, int ang_off, int sc_off, int wiggle, int wdir)
{
    obj *c = obj_new(T_CHILD);
    if (!c) return;
    c->parent = obj_index(dragon);
    c->ang_off = ang_off;
    c->sc_off = sc_off;
    c->wiggle = wiggle;
    c->wdir = wdir;
    c->scale = dragon->scale + sc_off;
    c->angle = dragon->angle + ang_off;
    if (c->angle < 0) c->angle += 720;
    dragon->child[k] = obj_index(c);
}

static obj *spawn_dragon(void)
{
    obj *o = obj_new(T_DRAGON);
    if (!o) return NULL;
    o->scale = 100 + (G.rnd2 & 31);
    o->angle = angle_0_762();
    o->ang_counter = G.P.dr_angle_delay;
    o->sc_counter = G.P.dr_scale_delay;
    spawn_child(o, 0, -20, 10, 4, 0);
    spawn_child(o, 1, -40, 20, -4, 1);
    spawn_sound(0.5f);
    return o;
}

static void spawn_starlet(void)
{
    int id = 0;
    while (id < 3 && (G.star_flags & (1 << id))) id++;
    if (id >= 3) return;
    obj *o = obj_new(T_STARLET);
    if (!o) return;
    static const int radius[3] = { 40, 25, 50 };
    static const int offset[3] = { 0, 150, 90 };
    G.star_flags |= 1 << id;
    G.starlet_count++;
    o->star_id = id;
    o->scale = radius[id];
    o->ang_base = (2 * ((G.rnd2 >> 3) + offset[id])) % 720;
    o->angle = o->ang_base;
    o->score_counter = STARLET_SCORE_DELAY;
    o->value = 2;
    o->anim_counter = 1;
    spawn_sound(2.0f);
}

static void spawn_bonus(void)
{
    obj *o = obj_new(T_BONUS);
    if (!o) return;
    o->scale = SPAWN_MAX;
    o->angle = angle_0_762();
    o->tick = G.P.bonus_delay;
    o->delta = G.P.bonus_addi;
    o->anim_counter = 1;
    G.bonus_counter = 1;
    spawn_sound(1.7f);
}

static void spawn_starfield(void)
{
    obj *o = obj_new(T_STARFIELD);
    if (!o) return;
    G.starfields++;
    o->tint = G.fx ? (int)(fx_rand() * 6) : 0;
    for (int k = 0; k < 4; k++) {
        rng_mix();
        int delay = G.rnd2 & 127;
        o->sf_scale[k] = delay ? delay : 256;
        o->sf_pos[k] = -1;
    }
}

/* Not allowed by the phase: try again next frame. */
static void spawn_retry(void) { G.spawn_timer = 0; G.spawn_count--; }
static void spawn_skip(void)  { G.spawn_count--; }

static void spawn_squad(void)
{
    if (!G.demo && G.object_count > 25) { spawn_skip(); return; }
    if (!(G.P.allow & ALLOW_SQUAD)) { spawn_retry(); return; }
    obj *(*fn)(void);
    int r = G.rnd2;
    if (r <= 50) fn = spawn_hunter;
    else if (r <= 100) fn = spawn_hx;
    else if (r <= 150) fn = spawn_x;
    else if (r <= 200) fn = spawn_bomber;
    else { spawn_skip(); return; }
    G.spawn_count += 5;
    obj *prev = fn();
    if (!prev) return;
    G.mute_spawn = 1;
    for (int i = 1; i < 6; i++) {
        obj *o = fn();
        if (!o) break;
        o->angle = prev->angle + 120;
        if (o->angle >= 720) o->angle -= 720;
        prev = o;
    }
    G.mute_spawn = 0;
}

/* no_spawn_now: the starfield budget (the CPU-load branches of the original
 * are always "plenty of time" here). */
static void no_spawn_now(void)
{
    G.expl_max = EXPLOSION_MAX_INIT;
    G.starfield_max = STARFIELD_MAX;
    int free_slots = MAX_OBJECTS - G.object_count;
    if (free_slots <= 10) G.starfield_max = free_slots < 1 ? 1 : free_slots;
    if (G.starfields < G.starfield_max) {
        spawn_starfield();
    } else if (G.starfields > G.starfield_max) {
        obj *victim = NULL;
        for (int i = 0; i < G.n_order; i++) {
            obj *o = &G.pool[G.order[i]];
            if (o->dead || o->type != T_STARFIELD) continue;
            if (!victim) victim = o;
            if (o->sf_pos[0] < 0 && o->sf_pos[1] < 0 && o->sf_pos[2] < 0 && o->sf_pos[3] < 0) {
                victim = o;
                break;
            }
        }
        if (victim) obj_remove(victim);
    }
}

static void check_spawn(void)
{
    if (--G.spawn_timer >= 0) { no_spawn_now(); return; }
    int a = G.spawn_reset;
    if (--G.spawn_rest_counter < 0) {
        int b = G.spawn_inc_delay + 1;
        if (b >= G.max_spawn_inc) b = G.max_spawn_inc;
        G.spawn_inc_delay = G.spawn_rest_counter = b;
        a = a - 1;
        if (a <= G.min_spawn_reset) a = G.min_spawn_reset;
    }
    G.spawn_reset = G.spawn_timer = a;
    G.spawn_count++;

    int r = G.rnd1, allow = G.P.allow;
    if (allow == ALLOW_SQUAD)  { spawn_squad(); return; }
    if (r <= 15) {
        if (G.starlet_count >= 3) spawn_skip();
        else if (!(allow & ALLOW_STAR)) spawn_retry();
        else spawn_starlet();
    } else if (r <= 80)  { if (allow & ALLOW_X) spawn_x(); else spawn_retry(); }
    else if (r <= 130)   { if (allow & ALLOW_HIDDEN_X) spawn_hx(); else spawn_retry(); }
    else if (r <= 170)   { if (allow & ALLOW_HUNTER) spawn_hunter(); else spawn_retry(); }
    else if (r <= 200)   { if (allow & ALLOW_BOMBER) spawn_bomber(); else spawn_retry(); }
    else if (r <= 230)   { if (allow & ALLOW_DRAGON) spawn_dragon(); else spawn_retry(); }
    else if (r <= 240)   spawn_squad();
    else if (r <= 252) {
        if (G.bonus_counter != 0) spawn_skip();
        else if (!(allow & ALLOW_BONUS)) spawn_retry();
        else spawn_bonus();
    } else spawn_skip();
}

/* ======================================================================= */
/* the shield                                                               */

static int shrink(int v, int code)
{
    int t;
    switch (code) {
    case 1:  return v >> 5;
    case 2:  return v >> 4;
    case 3:  t = v >> 4; return t + (t >> 2);
    case 4:  t = v >> 3; return t - (t >> 2);
    case 5:  t = v >> 3; return t - (t >> 3);
    case 6:  t = v >> 3; return t - (t >> 4);
    case 7:  return v >> 3;
    case 8:  t = v >> 3; return t + (t >> 3);
    case 9:  t = v >> 3; return t + (t >> 2);
    case 10: t = v >> 2; return t - (t >> 2) - (t >> 4);
    case 11: t = v >> 2; return t - (t >> 2);
    case 12: t = v >> 2; return t - (t >> 3) - (t >> 4);
    case 13: t = v >> 2; return t - (t >> 3);
    default: return 0;
    }
}

/* onShield: is the object's centre inside the polygon ring? */
static int on_shield(const obj *o)
{
    static const int per_side[4] = { 72, 60, 51, 45 };
    if (o->type >= T_HITTABLE) return 0;
    int r = o->scale;
    int outer = G.shield_start + SHIELD_VARIANCE;
    if (outer <= r) return 0;
    int d = o->angle - G.base_angle;
    if (d < 0) d += 720;
    int code = poly_correction[G.sided - 5][(d >> 1) % per_side[G.sided - 5]];
    if (outer - shrink(outer, code) <= r) return 0;
    int inner = G.shield_start - G.shield_width - SHIELD_VARIANCE;
    if (inner < 0) inner = G.shield_start - G.shield_width;
    if (inner - shrink(inner, code) >= r) return 0;
    return 1;
}

static void exchange_to_explosion(obj *o)
{
    int mult = G.dead_enemies == 0 ? 1 : G.dead_enemies == 1 ? 2 : 3;
    int base = 0, inc = 0, ex = EX_DOTS;
    int victim = o->type;

    switch (o->type) {
    case T_DRAGON:
        if (!o->damaged) {              /* the first hit only wounds it */
            static const unsigned char red[3] = { 255, 60, 60 };
            o->damaged = 1;
            play(G.s_dragonhit, 0.7f, 1.0f);
            fx_burst(o->angle, o->scale, red, 6, 160);
            return;
        }
        free_children(o);
        base = 18; inc = 1; ex = EX_CROSSES;
        break;
    case T_BONUS: {
        G.bonus_counter = 0;
        G.bonus_destroyed++;
        add_score(505 * G.bonus_destroyed);
        int g = G.bonus_destroyed;
        while (g >= 20) g -= 15;
        if (g == 5 || g == 10 || g == 15) { inc = 2; ex = EX_DOTS; }  /* gimmick: not drawn yet */
        else { inc = 1; ex = EX_BRIGHT; }
        play(G.s_mystery, 0.7f, 1.0f);
        break;
    }
    case T_STARLET:
        G.starlet_count--;
        G.star_flags &= ~(1 << o->star_id);
        ex = EX_STAR;
        break;
    case T_X:      base = 10; inc = 1; ex = EX_IMPLODE; break;
    case T_HX:     base = 15; inc = 2; ex = EX_DOTS; break;
    case T_HUNTER: base = 15; inc = 1; ex = EX_OUTLINE; break;
    case T_BOMBER: base = 18; inc = 1; ex = EX_DIAMONDS; break;
    case T_SHOT:   base = 10; inc = 1; ex = EX_SHOT; break;
    case T_CHILD:
        if (o->parent >= 0 && !o->freed) {
            obj *p = &G.pool[o->parent];
            for (int k = 0; k < 2; k++) if (p->child[k] == obj_index(o)) p->child[k] = -1;
        }
        base = 10; inc = 2; ex = EX_DOTS;
        break;
    default:
        return;
    }
    if (base) {
        int pts = base * mult;
        add_score(pts);
        popup(o->angle, o->scale, pts, 0, mult);
    }
    G.dead_enemies += inc;
    {
        static const int sparks[T_HITTABLE] = {
            [T_X] = 8, [T_HX] = 8, [T_HUNTER] = 10, [T_BOMBER] = 14, [T_SHOT] = 5,
            [T_DRAGON] = 18, [T_CHILD] = 6, [T_STARLET] = 12, [T_BONUS] = 20
        };
        float speed = victim == T_SHOT ? 150.0f : victim == T_BOMBER || victim == T_DRAGON ? 280.0f : 220.0f;
        fx_burst(o->angle, o->scale, type_rgb[victim], sparks[victim], speed);
    }
    if (victim != T_BONUS) play(G.s_explode, 0.55f, victim == T_STARLET ? 1.8f : 0.9f + 0.1f * mult);

    if (G.object_count >= MAX_OBJECTS - 3 && G.expl_active >= 4) { obj_remove(o); return; }
    o->type = T_EXPLOSION;
    o->victim = victim;
    o->etype = ex;
    o->escale = 0;
    o->eint = 0x7F;
    o->shrinking = 0;
    o->ending = 0;
    G.expl_active++;
}

static void shield_animate(void)
{
    G.inner = G.shield_start - G.shield_width;
    if (G.shield_start >= SHIELD_MAX && G.inner <= 9) {
        G.shield_width = G.shield_start - 10;
        G.inner = 10;
    }
    G.osc_forth += G.cfd;
    if (G.osc_forth >= G.shield_start || G.osc_forth > 255) {
        int v = G.shield_start - G.shield_width;
        G.osc_forth = v >= 0xF0 ? 0 : v;
    }
    G.osc_back -= G.cbd;
    if (G.osc_back < 0) G.osc_back = 0;
    if (G.osc_back < G.inner) G.osc_back = G.shield_start;
}

static void new_shield(void)
{
    G.shield_start = 1;
    G.osc_forth = G.osc_back = 1;
    G.cfd = G.P.speed * 2;
    G.cbd = G.P.speed / 2;
    G.shield_width = 3;
    G.width_counter = G.P.w_growth;
    G.sided = 6;                        /* the Vectrex turns 5..8 sides a press; a hexagon here */
}

static void continue_shield(void)
{
    if (G.shield_delay >= 0) { G.shield_visible = 0; return; }   /* cooldown */
    G.shield_active = 1;
    if (--G.width_counter < 0) {
        G.width_counter = G.P.w_growth;
        G.shield_width += G.P.w_adder;
    }
    int a = G.shield_start + G.P.speed;
    if (--G.minor_counter < 0) {
        G.minor_counter = G.P.minor_reset;
        a += G.P.minor_inc;
    }
    if (a > SHIELD_MAX) {
        G.cfd = G.cbd = G.P.speed;
        if (G.shield_width != 0x80) G.cfd = G.cbd = G.shield_width >> 3;
        a = SHIELD_MAX;
    }
    G.shield_start = a;
    if (G.shield_start <= G.shield_width) G.shield_width = G.shield_start - 1;
    shield_animate();
    G.shield_visible = 1;
}

static void finish_shield(void)
{
    G.shield_delay = SHIELD_DELAY_TICKS;
    G.shield_active = 0;
    G.shield_visible = 0;
    G.dead_enemies = 0;
    if (G.shield_start <= 9) return;
    play(G.s_release, 0.5f, 1.0f);
    for (int i = 0; i < G.n_order; i++) {
        obj *o = &G.pool[G.order[i]];
        if (!o->dead && on_shield(o)) exchange_to_explosion(o);
    }
    fx_shock(G.shield_start);
    G.shield_start = 1;
}

/* ======================================================================= */
/* behaviours: one call per object per frame                               */

static void end_explosion(obj *o) { o->ending = 1; G.expl_active--; }

static void behave(obj *o)
{
    if (o->ending) { obj_remove(o); return; }

    switch (o->type) {
    case T_X:
    case T_HX:
        if (--o->tick < 0) {
            o->tick = o->type == T_X ? G.P.x_delay : G.P.hx_delay;
            if (move_in(o, o->delta)) return;
        }
        if (--o->anim_counter < 0) { o->anim_counter = 2; o->anim = (o->anim + 1) & 3; }
        if (o->type == T_X) {
            o->intensity = 0x5F;
        } else if (o->scale >= 128) {
            o->intensity = 0;           /* cloaked */
        } else {
            o->intensity = 143 - o->scale;
            if (o->intensity > 127) o->intensity -= 16;
        }
        break;

    case T_HUNTER:
        if (--o->tick < 0) {
            o->tick = 0;
            if (move_in(o, o->delta)) return;
        }
        o->intensity = 0x5F;
        break;

    case T_BOMBER:
        if (--o->tick < 0) {
            o->tick = G.P.bomber_delay;
            o->angle -= 2;
            if (o->angle < 0) o->angle += 720;
        }
        if (--o->anim_counter < 0) { o->anim_counter = 2; o->anim = (o->anim + 1) % 9; }
        o->shot_counter = (o->shot_counter - 1) & 0xFF;
        if (o->shot_counter == 0) {
            int next = (o->shot_reset - BOMB_RELOAD_REDUCTION) & 0xFF;
            if (next <= G.P.min_bomb) next = G.P.min_bomb;
            o->shot_counter = o->shot_reset = next;
            obj *s = obj_new(T_SHOT);
            if (s) {
                s->angle = o->angle;
                s->scale = o->scale;
                s->tick = G.P.shot_delay;
                s->delta = G.P.shot_addi;
                play(G.s_shot, 0.35f, 1.0f);
            }
        }
        o->intensity = 0x5F;
        break;

    case T_SHOT:
        if (--o->tick < 0) {
            o->tick = G.P.shot_delay;
            if (move_in(o, o->delta)) return;
        }
        o->intensity = 0x7F;
        break;

    case T_DRAGON:
        if (--o->ang_counter < 0) {
            o->ang_counter = G.P.dr_angle_delay;
            o->angle += G.P.dr_angle_addi;
            if (o->angle >= 720) o->angle -= 720;
        }
        if (--o->sc_counter < 0) {
            o->sc_counter = G.P.dr_scale_delay;
            if (o->scale - 1 <= BASE_SCALE + 3) {
                free_children(o);
                hit_base(o);
                return;
            }
            o->scale--;
        }
        if (G.recal & 1) o->anim = (o->anim + 1) & 3;
        o->intensity = o->damaged ? 0x4F : 0x7F;
        break;

    case T_CHILD:
        if (!o->freed) {
            obj *p = o->parent >= 0 ? &G.pool[o->parent] : NULL;
            if (!p || !p->used || p->dead || p->type != T_DRAGON) { o->freed = 1; }
            else {
                o->scale = p->scale + o->sc_off;
                o->angle = p->angle + o->ang_off;
                if (o->angle < 0) o->angle += 720;
                if (o->wdir) { if (++o->wiggle == 4) o->wdir = 0; }
                else         { if (--o->wiggle == -4) o->wdir = 1; }
                o->scale += o->wiggle;
                o->intensity = 0x5F;
                break;
            }
        }
        if (move_in(o, G.P.child_addi)) return;
        o->intensity = 0x7F;
        break;

    case T_STARLET:
        o->angle = (o->ang_base + G.starlet_angle) % 720;
        if (--o->anim_counter < 0) { o->anim_counter = 1; o->anim = (o->anim + 1) % 11; }
        if (--o->score_counter < 0) {
            o->score_counter = STARLET_SCORE_DELAY;
            o->value += 2;
            if (o->value > 999) o->value = 999;
            add_score(o->value);
            popup(o->angle, o->scale, o->value, 1, 1);
            play(G.s_star, 0.4f, 1.0f);
            fx_burst(o->angle, o->scale, type_rgb[T_STARLET], 5, 90);
        }
        o->intensity = 0x5F;
        break;

    case T_BONUS:
        if (--o->tick < 0) {
            o->tick = G.P.x_delay;      /* sic: the original reloads from X_add_delay */
            int ns = o->scale - o->delta;
            if (ns <= BASE_SCALE + 3) { init_bonus(); obj_remove(o); return; }
            o->scale = ns;
        }
        if (--o->anim_counter < 0) { o->anim_counter = 2; o->anim = (o->anim + 1) % 17; }
        o->intensity = 0x5F;
        break;

    case T_EXPLOSION:
        switch (o->etype) {
        case EX_IMPLODE:
            if (!o->shrinking) {
                o->escale += 2;
                if (o->escale >= 30) o->shrinking = 1;
            } else if (o->escale - 3 < 0) end_explosion(o);
            else o->escale -= 3;
            break;
        case EX_STAR:
            o->escale += 4;
            if (o->escale > 80) end_explosion(o);
            break;
        default:
            o->escale += 2;
            if (o->etype == EX_SHOT) o->eint -= 4;
            if (o->escale + G.expl_active > G.expl_max) {
                G.expl_max += 5;
                end_explosion(o);
            }
            break;
        }
        break;

    case T_POPUP:
        if (!o->star_popup && o->scale >= SHIELD_MAX) { obj_remove(o); return; }
        if (++o->countdown >= SCORE_DISPLAY_TIME) o->ending = 1;
        if (G.recal & 1) o->scale++;
        break;

    case T_STARFIELD:
        for (int k = 0; k < 4; k++) {
            if (o->sf_pos[k] < 0) {
                if (--o->sf_scale[k] > 0) continue;
                o->sf_scale[k] = 8;
                o->sf_pos[k] = (G.rnd2 & 127) * 2;
                continue;
            }
            int p = o->sf_pos[k] + (G.fx ? 0 : G.star_swirl);   /* no swirl: it makes people dizzy */
            if (p > 240) p -= 240;
            if (p < 0) p += 240;
            o->sf_pos[k] = p;
            int s = o->sf_scale[k];
            s += s > 176 ? 5 : s > 128 ? 4 : s > 64 ? 3 : s > 32 ? 2 : 1;
            if (s > SPAWN_MAX) {
                s = 8;
                o->sf_pos[k] = (G.rnd2 & 127) * 2;
            }
            o->sf_scale[k] = s;
        }
        break;
    }
}

/* ======================================================================= */
/* the game                                                                 */

static void init_game(int demo, int held)
{
    memset(G.pool, 0, sizeof G.pool);
    G.n_order = 0;
    G.object_count = 0;
    G.demo = demo;
    G.sided = 6;
    G.base_angle = 0;
    G.shield_start = 1;
    G.shield_width = 3;
    G.shield_delay = -1;
    G.shield_active = G.shield_visible = 0;
    G.spawn_reset = G.spawn_timer = 50;
    G.spawn_rest_counter = G.spawn_inc_delay = 1;
    G.min_spawn_reset = 15;
    G.max_spawn_inc = 10;
    G.spawn_count = 0;
    G.starlet_count = G.star_flags = 0;
    G.starfields = 0;
    G.star_swirl = 0;
    G.expl_active = 0;
    G.expl_max = EXPLOSION_MAX_INIT;
    G.bonus_counter = G.bonus_active = G.bonus_destroyed = G.timer_active = 0;
    G.icon_s = 1;
    G.score = 0;
    G.dead_enemies = 0;
    G.dying = 0;
    G.btn_hist = held ? 1 : 0;
    G.demo_wait = 250;
    G.demo_held = 0;
    G.phase_count = demo ? 0 : G.start_phase;
    init_phase(phase_list[G.phase_count]);
    fx_clear();
    G.danger_scale = 255;
    if (G.fx) orig_init_game(held);
}

/* The demo's player: wait, then press and release at random. */
static int demo_button(void)
{
    if (G.demo_wait > 0) { G.demo_wait--; return G.demo_held; }
    G.demo_held = !G.demo_held;
    G.demo_wait = (G.rnd1 & 31) + 1;
    return G.demo_held;
}

/* The one button: SHIELD BUTTON, laid out in the service menu (vpbind.h's
 * controls; several may share it). */
static int buttons_held(vp_input *in)
{
    (void)in;
    return vpb_held(G.set.button[SB_SHIELD]);
}

static int shield_pressed(void) { return vpb_pressed(G.set.button[SB_SHIELD]); }

static int any_pressed(vp_input *in)
{
    return shield_pressed() || vp_input_pressed(in, "start1");
}

/* A game starts with START or any button: the laid out one, or any of the
 * panel's (the advmame names). */
static int start_pressed(vp_input *in)
{
    static const char *const names[] = {
        "start1", "p1_button1", "p1_button2", "p1_button3", "p1_button4", "p1_button5", "p1_button6",
    };
    if (shield_pressed()) return 1;
    for (int i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (vp_input_pressed(in, names[i])) return 1;
    return 0;
}

static void set_state(int s)
{
    G.state = s;
    G.state_frames = 0;
}

/* One beat for everything that blinks with the START lamp ("Press start",
 * "Demo", ...). */
static int blink_on(void) { return (G.state_frames / 20) % 2 == 0; }

static void game_over(void)
{
    play(G.s_gameover, 0.8f, 1.0f);
    orig_death_start();
    set_state(ST_GAMEOVER);
}

static void game_step(vp_input *in)
{
    if (G.shield_delay >= 0) G.shield_delay--;
    check_stage();
    G.recal = (G.recal + 1) & 0xFFFF;
    G.base_angle += 2;
    if (G.base_angle >= 720) G.base_angle -= 720;
    G.starlet_angle = 720 - G.base_angle;

    /* the starfield swirl changes now and then */
    if ((G.recal & 0xFF) == 0 && ((G.recal >> 8) & 2) == 0) {
        G.star_swirl += (G.rnd2 & 0x80) ? -2 : 2;
        if (G.star_swirl > 2) G.star_swirl = 2;
        if (G.star_swirl < -2) G.star_swirl = -2;
    }

    int held = G.demo ? demo_button() : buttons_held(in);
    G.btn_hist = (unsigned char)((G.btn_hist << 1) | (held ? 1 : 0));
    rng_step();
    switch (G.btn_hist & 3) {
    case 1: new_shield(); continue_shield(); break;
    case 3: continue_shield(); break;
    case 2: finish_shield(); break;
    default: G.shield_visible = 0; break;
    }

    if (G.bonus_counter > 1 && G.icon_s < 127) G.icon_s += 2;
    if (G.timer_active && --G.bonus_counter <= 0) {
        G.bonus_counter = 0;
        end_bonus();
    }

    check_spawn();

    for (int i = 0; i < G.n_order; i++) {
        obj *o = &G.pool[G.order[i]];
        if (o->dead) continue;
        behave(o);
        if (G.dying) break;
    }
    compact();

    fx_update();
    G.danger_scale = 255;
    for (int i = 0; i < G.n_order; i++) {
        const obj *o = &G.pool[G.order[i]];
        if (!o->dead && o->type < T_STARLET && o->scale < G.danger_scale) G.danger_scale = o->scale;
    }
}

/* ======================================================================= */
/* drawing                                                                  */

/* raw Vectrex units (centre 0, y up) -> board */
static int bx(float rx) { return (int)lrintf(2048.0f + rx * G.ax); }
static int by(float ry) { return (int)lrintf(2048.0f + ry * G.ay); }

static int set_color(vp_dvg *d, const unsigned char *rgb, int intensity)
{
    if (intensity <= 0) return 0;
    float f = intensity / 112.0f;
    if (f > 1.0f) f = 1.0f;
    vp_color(d, (uint8_t)(rgb[0] * f), (uint8_t)(rgb[1] * f), (uint8_t)(rgb[2] * f));
    return 1;
}

static void obj_pos(int angle, int scale, float *rx, float *ry)
{
    int k = ci(angle);
    *rx = (float)cir_x[k] * scale;
    *ry = (float)cir_y[k] * scale;
}

/* The sprites' scale: the Vectrex draws them at 6. The original on a phone
 * shows them much bigger; so do the effects (--classic keeps 6). */
#define SPRITE      (G.fx ? 6.0f * 1.7f : 6.0f)
/* The view: 1 = the Vectrex's screen. A little closer, so the bombers'
 * orbit (SCALE up to 131) still fits the 4:3 tube's height. */
#define VIEW_ZOOM   1.12f

/* A shape (release.h format) at raw (rx, ry), sprite scale s. */
static void draw_shape(vp_dvg *d, float rx, float ry, const short *sh, float s)
{
    while (*sh) {
        int n = *sh++;
        vp_moveto(d, bx(rx + sh[0] * s), by(ry + sh[1] * s));
        for (int i = 1; i < n; i++) vp_lineto(d, bx(rx + sh[2 * i] * s), by(ry + sh[2 * i + 1] * s));
        sh += 2 * n;
    }
}

/* Corner k of the base polygon (radius 108) turned by `rot` (doubled degrees). */
static void corner_at(int rot, int k, float *x, float *y)
{
    static const int add[4] = { 144, 120, 102, 90 };
    int a = ((rot + k * add[G.sided - 5]) % 720 + 720) % 720;
    *x = (float)cir_x[a >> 1];
    *y = (float)cir_y[a >> 1];
}

static void corner(int k, float *x, float *y) { corner_at(G.base_angle, k, x, y); }

static void draw_ring_at(vp_dvg *d, float cx, float cy, float r, int rot)
{
    float x, y;
    corner_at(rot, 0, &x, &y);
    vp_moveto(d, bx(cx + x * r), by(cy + y * r));
    for (int k = 1; k < G.sided; k++) {
        corner_at(rot, k, &x, &y);
        vp_lineto(d, bx(cx + x * r), by(cy + y * r));
    }
    corner_at(rot, 0, &x, &y);
    vp_lineto(d, bx(cx + x * r), by(cy + y * r));
}

static void draw_ring(vp_dvg *d, float cx, float cy, float r) { draw_ring_at(d, cx, cy, r, G.base_angle); }

static void color_scaled(vp_dvg *d, const unsigned char *rgb, float f)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    vp_color(d, (uint8_t)(rgb[0] * f), (uint8_t)(rgb[1] * f), (uint8_t)(rgb[2] * f));
}

/* ---- text: the original game's font (font_xspace.inc), or with --classic
 * the SDK's Asteroids letters. `h` is a capital's height in board units. */

static short xs_left[XS_LAST - XS_FIRST + 1], xs_right[XS_LAST - XS_FIRST + 1];

static int xs_index(char ch)
{
    int c = (unsigned char)ch;
    return (c < XS_FIRST || c > XS_LAST ? '?' : c) - XS_FIRST;
}

static void xs_init(void)                /* each glyph's ink, left and right */
{
    for (int g = 0; g <= XS_LAST - XS_FIRST; g++) {
        const short *p = xs_data + xs_glyph[g].offset;
        int lo = 32767, hi = -32767;
        while (*p) {
            int n = *p++;
            for (int i = 0; i < n; i++) {
                if (p[2 * i] < lo) lo = p[2 * i];
                if (p[2 * i] > hi) hi = p[2 * i];
            }
            p += 2 * n;
        }
        xs_left[g] = (short)(lo > hi ? 0 : lo);
        xs_right[g] = (short)(lo > hi ? xs_glyph[g].advance : hi);
    }
}

/* From the pen's start to the last letter's ink, and where the ink begins:
 * so centered text is centered as drawn, whatever the side bearings. */
static void xs_ink(const char *s, float kx, float *from, float *to)
{
    float pen = 0;
    *from = *to = 0;
    for (const char *p = s; *p; p++) {
        int g = xs_index(*p);
        if (p == s) *from = xs_left[g] * kx;
        if (!p[1]) *to = pen + xs_right[g] * kx;
        pen += xs_glyph[g].advance * kx;
    }
}

static void xs_text(vp_dvg *d, const char *s, float x, float y, float h, int align, float squeeze)
{
    float ky = h / XS_CAP, kx = ky * (G.ax / G.ay) * squeeze;   /* round on the tube */
    float from, to;
    xs_ink(s, kx, &from, &to);
    if (align == VP_TEXT_CENTER) x -= (from + to) / 2;
    else if (align == VP_TEXT_RIGHT) x -= to;
    else x -= from;
    for (const char *c = s; *c; c++) {
        int g = xs_index(*c);
        const short *p = xs_data + xs_glyph[g].offset;
        while (*p) {
            int n = *p++;
            vp_moveto(d, (int)(x + p[0] * kx), (int)(y + p[1] * ky));
            for (int i = 1; i < n; i++) vp_lineto(d, (int)(x + p[2 * i] * kx), (int)(y + p[2 * i + 1] * ky));
            p += 2 * n;
        }
        x += xs_glyph[g].advance * kx;
    }
}

/* The screens' text is always the original's font, whichever gameplay is
 * set (the service menu has the cabinet's own look, service.c). */
static float ui_width(const char *s, float h)
{
    float from, to;
    xs_ink(s, h / XS_CAP * (G.ax / G.ay), &from, &to);
    return to - from;
}

static void ui_text(vp_dvg *d, const char *s, float x, float y, float h, int align)
{
    xs_text(d, s, x, y, h, align, 1.0f);
}

/* Squeezed to `width` if it is wider. */
static void ui_fit(vp_dvg *d, const char *s, float x, float y, float h, float width, int align)
{
    float w = ui_width(s, h), squeeze = w > width ? width / w : 1.0f;
    xs_text(d, s, x, y, h, align, squeeze);
}

static void text_c(vp_dvg *d, const char *s, float x, float y, float h) { ui_text(d, s, x, y, h, VP_TEXT_CENTER); }

/* The original's panels: a thin white frame. */
static void panel(vp_dvg *d, int x0, int y0, int x1, int y1)
{
    vp_color(d, 210, 210, 210);
    vp_moveto(d, x0, y0);
    vp_lineto(d, x1, y0);
    vp_lineto(d, x1, y1);
    vp_lineto(d, x0, y1);
    vp_lineto(d, x0, y0);
}

/* ---- the screens' look: the game's starfield and its shield ------------ */

#define BG_STARS 48
static struct { float angle, r, speed; int tint; float phase; } bg[BG_STARS];

static void bg_star_new(int i, int anywhere)
{
    bg[i].angle = fx_rand() * 6.2831853f;
    bg[i].r = anywhere ? fx_rand() * 2600.0f : 20.0f + fx_rand() * 80.0f;
    bg[i].speed = 0.008f + fx_rand() * 0.02f;
    bg[i].tint = (int)(fx_rand() * 6);
    bg[i].phase = fx_rand() * 6.2831853f;
}

/* Once a frame on the screens around the game: the dots fly outwards,
 * faster as they come closer, as the game's own starfield does. */
static void bg_update(void)
{
    static int ready;
    for (int i = 0; i < BG_STARS; i++) {
        if (!ready) bg_star_new(i, 1);
        bg[i].r += 4.0f + bg[i].r * bg[i].speed;
        if (bg[i].r > 2900.0f) bg_star_new(i, 0);
    }
    ready = 1;
}

static void bg_draw(vp_dvg *d)
{
    static const unsigned char tints[6][3] = {
        { 255, 255, 255 }, { 255, 140, 200 }, { 255, 230, 90 },
        { 255, 150,  80 }, { 150, 200, 255 }, { 200, 150, 255 }
    };
    /* in a short path, each dot the nearest one left from the last: in their
     * own order the beam crossed the screen between them, more than half of
     * a frame's travel and different every frame, and the board, drawing
     * faster when a frame is long, made the picture twitch */
    int xs[BG_STARS], ys[BG_STARS], left = 0, bx = 2048, by = 2048;
    unsigned char todo[BG_STARS];
    for (int i = 0; i < BG_STARS; i++) {
        xs[i] = (int)(2048 + sinf(bg[i].angle) * bg[i].r * (G.ax / G.ay));
        ys[i] = (int)(2048 + cosf(bg[i].angle) * bg[i].r);
        todo[i] = xs[i] >= 0 && xs[i] <= VP_DVG_COORD_MAX && ys[i] >= 0 && ys[i] <= VP_DVG_COORD_MAX;
        left += todo[i];
    }
    for (; left > 0; left--) {
        int i = -1;
        long best = 0;
        for (int j = 0; j < BG_STARS; j++) {
            if (!todo[j]) continue;
            long dx = xs[j] - bx, dy = ys[j] - by, d2 = dx * dx + dy * dy;
            if (i < 0 || d2 < best) { i = j; best = d2; }
        }
        todo[i] = 0;
        float twinkle = 0.45f + 0.55f * sinf(G.state_frames * 0.3f + bg[i].phase);
        color_scaled(d, tints[bg[i].tint], (0.2f + bg[i].r / 2400.0f) * twinkle);
        vp_moveto(d, xs[i], ys[i]);
        vp_lineto(d, xs[i], ys[i]);
        bx = xs[i]; by = ys[i];
    }
}

/* A polygon on the screen (board units, round on the tube), `rot` in radians. */
static void screen_poly(vp_dvg *d, float cx, float cy, float r, int sides, float rot)
{
    float k = G.ax / G.ay;
    for (int i = 0; i <= sides; i++) {
        float a = rot + i * 6.2831853f / sides;
        int x = (int)(cx + sinf(a) * r * k), y = (int)(cy + cosf(a) * r);
        if (i == 0) vp_moveto(d, x, y);
        else vp_lineto(d, x, y);
    }
}

/* A shield as a badge: green edge, white inner wall; `live` adds the ring
 * running between them and a pulse, `dim` is a slot not reached yet. */
static void badge(vp_dvg *d, float cx, float cy, float r, int sides, float rot, int live, int dim)
{
    static const unsigned char green[3] = { 130, 255, 160 }, white[3] = { 255, 255, 255 };
    float bright = dim ? 0.3f : live ? 0.75f + 0.25f * sinf(G.state_frames * 0.25f) : 1.0f;
    color_scaled(d, green, bright);
    screen_poly(d, cx, cy, r, sides, rot);
    color_scaled(d, white, bright * 0.8f);
    screen_poly(d, cx, cy, r * 0.84f, sides, rot);
    if (live) {
        float run = (G.state_frames % 30) / 30.0f;
        color_scaled(d, white, 0.5f * (1 - run));
        screen_poly(d, cx, cy, r * (0.84f + 0.16f * run), sides, rot);
    }
}

/* A clockwise arc around the base from the top, `frac` of a full circle. */
static void draw_arc(vp_dvg *d, float r, float frac)
{
    int n = (int)(32 * frac + 0.999f);
    if (n < 1) return;
    vp_moveto(d, bx(0), by(r));
    for (int i = 1; i <= n; i++) {
        float a = i * 6.2831853f / 32;
        vp_lineto(d, bx(sinf(a) * r), by(cosf(a) * r));
    }
}

static void dot(vp_dvg *d, float rx, float ry)
{
    int x = bx(rx), y = by(ry);
    vp_moveto(d, x, y);
    vp_lineto(d, x, y);
}

static void draw_explosion(vp_dvg *d, const obj *o, float rx, float ry)
{
    const unsigned char *rgb = type_rgb[o->victim];
    float e = (float)o->escale, x, y;
    switch (o->etype) {
    case EX_OUTLINE:
        if (set_color(d, rgb, 0x7F - 3 * o->escale)) draw_ring(d, rx, ry, e);
        break;
    case EX_DIAMONDS:
        set_color(d, rgb, 0x7F);
        for (int k = 0; k < G.sided; k++) {
            corner(k, &x, &y);
            draw_shape(d, rx + x * e, ry + y * e, shape_shot, SPRITE);
        }
        break;
    case EX_CROSSES: {
        static const short cross[] = { 2, 0,0, 36,36,  2, 36,0, 0,36, 0 };
        set_color(d, rgb, 0x7F);
        for (int k = 0; k < G.sided; k++) {
            corner(k, &x, &y);
            draw_shape(d, rx + x * e, ry + y * e, cross, SPRITE);
        }
        break;
    }
    case EX_SHOT:
        if (set_color(d, rgb, o->eint)) draw_shape(d, rx, ry, shape_shot, SPRITE);
        break;
    default:                            /* dots at the corners */
        set_color(d, rgb, 0x7F);
        for (int k = 0; k < G.sided; k++) {
            corner(k, &x, &y);
            dot(d, rx + x * e, ry + y * e);
            if (o->etype == EX_STAR || o->etype == EX_BRIGHT) dot(d, rx + x * e, ry + y * e);
        }
        break;
    }
}

static void draw_object(vp_dvg *d, const obj *o)
{
    float rx, ry;
    obj_pos(o->angle, o->scale, &rx, &ry);
    const unsigned char *rgb = type_rgb[o->type];

    switch (o->type) {
    case T_X:
    case T_HX:
        if (set_color(d, rgb, o->intensity)) draw_shape(d, rx, ry, shape_x[o->anim], SPRITE);
        break;
    case T_HUNTER: {
        int i = o->angle >> 5;
        if (i > 23) i = 23;
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, shape_hunter[i], SPRITE);
        break;
    }
    case T_BOMBER:
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, look_bomber(o->anim), SPRITE);
        break;
    case T_SHOT:
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, shape_shot, SPRITE);
        break;
    case T_DRAGON: {
        static const unsigned char hurt[3] = { 255, 60, 60 };
        int flicker = G.fx && o->damaged && (G.state_frames / 3) % 2;
        set_color(d, flicker ? hurt : rgb, o->intensity);
        draw_shape(d, rx, ry, look_dragon(o->anim), SPRITE);
        break;
    }
    case T_CHILD:
        if (G.fx && !o->freed && o->parent >= 0 && !G.pool[o->parent].dead) {
            float px, py;                /* a faint tether to the mother ship */
            obj_pos(G.pool[o->parent].angle, G.pool[o->parent].scale, &px, &py);
            color_scaled(d, rgb, 0.3f);
            vp_moveto(d, bx(px), by(py));
            vp_lineto(d, bx(rx), by(ry));
        }
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, look_child(), SPRITE);
        break;
    case T_STARLET:
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, shape_starlet[o->anim], SPRITE);
        break;
    case T_BONUS:
        set_color(d, rgb, o->intensity);
        draw_shape(d, rx, ry, look_bonus(o->anim), SPRITE);
        break;
    case T_EXPLOSION:
        draw_explosion(d, o, rx, ry);
        break;
    case T_POPUP: {
        char s[8];
        static const unsigned char by_mult[4][3] = {
            { 255, 255, 255 }, { 230, 230, 230 }, { 255, 240, 60 }, { 255, 140, 40 }
        };
        snprintf(s, sizeof s, "%d", o->value);
        set_color(d, o->star_popup ? type_rgb[T_STARLET] : G.fx ? by_mult[o->mult & 3] : rgb, 0x5F);
        text_c(d, s, (float)bx(rx), (float)by(ry), G.fx ? 80 : 48);
        break;
    }
    case T_STARFIELD: {
        /* the original's background: dots in many colors */
        static const unsigned char tints[6][3] = {
            { 255, 255, 255 }, { 255, 140, 200 }, { 255, 230, 90 },
            { 255, 150,  80 }, { 150, 200, 255 }, { 200, 150, 255 }
        };
        for (int k = 0; k < 4; k++) {
            if (o->sf_pos[k] < 0) continue;
            int s = o->sf_scale[k];
            int in = s + 10 > 0x2F ? 0x2F : s + 10;
            float x, y;
            obj_pos(o->sf_pos[k] * 3, s, &x, &y);
            if (!G.fx) {
                set_color(d, rgb, in);
            } else {
                /* each star its own color, twinkling at its own pace and
                 * brighter as it comes closer */
                float twinkle = 0.45f + 0.55f * sinf(G.state_frames * (0.25f + 0.05f * k) +
                                                     (float)(o - G.pool) * 1.7f + k * 2.3f);
                color_scaled(d, tints[(o->tint + k) % 6], (0.3f + s / 150.0f) * twinkle);
            }
            dot(d, x, y);
        }
        break;
    }
    }
}

static void draw_hud(vp_dvg *d)
{
    char s[16];
    vp_text_font(VP_FONT_ASTEROIDS);
    if (G.fx) {
        /* as in the original: the score top left, the best top right */
        snprintf(s, sizeof s, "%d", G.score);
        vp_color(d, score_rgb[0], score_rgb[1], score_rgb[2]);
        ui_text(d, s, 150, 4095 - 280, 190, VP_TEXT_LEFT);
        snprintf(s, sizeof s, "%d", G.score > G.best ? G.score : G.best);
        vp_color(d, record_rgb[0], record_rgb[1], record_rgb[2]);
        ui_text(d, s, 4095 - 150, 4095 - 280, 190, VP_TEXT_RIGHT);
        if (G.demo && blink_on()) {
            vp_color(d, 255, 255, 255);
            text_c(d, "Demo", 2048, 4095 - 300, 230);
        }
    } else if (G.demo) {
        if (blink_on()) {
            vp_color(d, 255, 255, 255);
            text_c(d, "DEMO", 2048, 4095 - 170, 110);
        }
    } else {
        snprintf(s, sizeof s, "%06d", G.score);
        vp_color(d, 220, 220, 220);
        text_c(d, s, 2048, 4095 - 170, 110);
    }

    /* the bonus icon flies to the top, the seconds count down in the base */
    if (G.bonus_counter > 1 && G.bonus_active) {
        const short *icon = G.bonus_active == BONUS_SHIELD ? shape_icon_shield :
                            G.bonus_active == BONUS_FASTER ? shape_icon_faster : shape_icon_expand;
        float s2 = (float)G.icon_s;
        vp_color(d, 90, 255, 255);
        draw_shape(d, -30.0f * s2 - 900.0f, s2 * s2 * 0.95f, icon, 4);
    }
    if (G.timer_active && G.bonus_counter > 1) {
        snprintf(s, sizeof s, "%d", (G.bonus_counter + FPS - 1) / FPS);
        vp_color(d, 90, 255, 255);
        text_c(d, s, 2048, G.fx ? 2048 - 35 : 2048 - 22, G.fx ? 70 : 44);
    }
}

static void draw_base(vp_dvg *d)
{
    static const unsigned char white[3] = { 255, 255, 255 };
    if (!G.fx) {
        vp_color(d, 255, 255, 255);
        draw_ring(d, 0, 0, BASE_SCALE);
        return;
    }
    /* white, pulsing red while something is about to reach it */
    unsigned char c[3] = { 255, 255, 255 };
    if (G.danger_scale < 40) {
        float p = 0.5f + 0.5f * sinf(G.state_frames * 0.6f);
        float near = (40 - G.danger_scale) / 32.0f;
        if (near > 1) near = 1;
        c[1] = c[2] = (unsigned char)(255 * (1 - near * p));
    }
    /* drawn as big as the reach of a hit (BASE_SCALE + 3), as the
     * original's base is drawn bigger than the Vectrex's */
    const float r = BASE_SCALE + 3;
    vp_color(d, c[0], c[1], c[2]);
    draw_ring(d, 0, 0, r);
    vp_color(d, 130, 255, 160);         /* the original's green edge */
    draw_ring(d, 0, 0, r + 1.0f);
    color_scaled(d, white, 0.45f);      /* a small core turning the other way */
    draw_ring_at(d, 0, 0, r * 0.5f, -G.base_angle * 2);

    if (G.bonus_active == BONUS_SHIELD) {
        color_scaled(d, bonus_rgb[BONUS_SHIELD], 0.6f + 0.4f * sinf(G.state_frames * 0.3f));
        draw_ring(d, 0, 0, r + 3.0f);
    }
    if (G.timer_active && G.bonus_counter > 1) {    /* the bonus time left */
        float frac = G.bonus_counter / (float)(FPS * G.P.bonus_time);
        color_scaled(d, bonus_rgb[G.bonus_active], 0.8f);
        draw_arc(d, (r + 5.0f) * 108, frac > 1 ? 1 : frac);
    }
}

/* Outer and inner wall, two rings running between them. With the effects
 * it has the original's look: green edges, the band between them white
 * (spokes and the running rings), pulsing when full. */
static void draw_shield(vp_dvg *d)
{
    if (!G.shield_visible) return;
    static const unsigned char plain[3] = { 40, 190, 255 }, osc_plain[3] = { 20, 90, 200 };
    static const unsigned char green[3] = { 130, 255, 160 }, white[3] = { 255, 255, 255 };
    const unsigned char *c = plain, *osc = osc_plain;
    float bright = 1.0f;
    if (G.fx) {
        c = green;
        osc = G.bonus_active == BONUS_FASTER || G.bonus_active == BONUS_EXPAND ? bonus_rgb[G.bonus_active] : white;
        if (G.shield_start >= SHIELD_MAX) bright = 0.7f + 0.3f * sinf(G.state_frames * 0.8f);
    }
    color_scaled(d, c, bright);
    draw_ring(d, 0, 0, (float)G.shield_start);
    draw_ring(d, 0, 0, (float)G.inner);
    if (G.fx) {
        float x, y;
        color_scaled(d, white, bright * 0.5f);
        for (int k = 0; k < G.sided; k++) {
            corner(k, &x, &y);
            vp_moveto(d, bx(x * G.inner), by(y * G.inner));
            vp_lineto(d, bx(x * G.shield_start), by(y * G.shield_start));
        }
    }
    color_scaled(d, osc, G.fx ? 0.8f : 1.0f);
    draw_ring(d, 0, 0, (float)G.osc_forth);
    draw_ring(d, 0, 0, (float)G.osc_back);
}

static void draw_fx(vp_dvg *d)
{
    if (!G.fx) return;
    for (int i = 0; i < MAX_RINGS; i++) {
        if (rings[i].life <= 0) continue;
        float f = rings[i].life / 14.0f;
        vp_color(d, (uint8_t)(180 * f), (uint8_t)(230 * f), (uint8_t)(255 * f));
        draw_ring(d, 0, 0, rings[i].r);
    }
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const particle *p = &parts[i];
        if (p->life <= 0) continue;
        color_scaled(d, p->rgb, (float)p->life / p->max_life);
        vp_moveto(d, bx(p->x), by(p->y));
        vp_lineto(d, bx(p->x - p->vx * 2), by(p->y - p->vy * 2));
    }
    if (G.announce_frames > 0) {
        static const unsigned char gold[3] = { 255, 220, 80 };
        float f = G.announce_frames / 25.0f;
        vp_text_font(VP_FONT_ASTEROIDS);
        color_scaled(d, gold, f > 1 ? 1 : f);
        text_c(d, G.announce, 2048, 2048 + 620, 150);
    }
}

static void draw_game(vp_dvg *d)
{
    /* background first: the starfields */
    for (int i = 0; i < G.n_order; i++) {
        const obj *o = &G.pool[G.order[i]];
        if (!o->dead && o->type == T_STARFIELD) draw_object(d, o);
    }

    draw_base(d);
    draw_shield(d);

    for (int i = 0; i < G.n_order; i++) {
        const obj *o = &G.pool[G.order[i]];
        if (!o->dead && o->type != T_STARFIELD) draw_object(d, o);
    }
    draw_fx(d);
    draw_hud(d);
}

/* ======================================================================= */
/* the screens around the game                                              */

/* The attract pages: the title, the original's instructions, the high
 * scores; then the demo. */
#define TITLE_LOGO_FRAMES   (6 * FPS)
#define TITLE_TUTOR_FRAMES  (16 * FPS)    /* original.c's TUTOR_SECONDS */
#define TITLE_HELP_FRAMES   (10 * FPS)
#define TITLE_SCORE_FRAMES  (5 * FPS)

/* The screens look like the original's: its starfield behind, thin white
 * panels, its font, the last score top left (lilac) and the best top
 * right (salmon). */
static void draw_corners(vp_dvg *d)
{
    char s[16];
    snprintf(s, sizeof s, "%d", G.score);
    vp_color(d, score_rgb[0], score_rgb[1], score_rgb[2]);
    ui_text(d, s, 150, 4095 - 280, 190, VP_TEXT_LEFT);
    snprintf(s, sizeof s, "%d", G.score > G.best ? G.score : G.best);
    vp_color(d, record_rgb[0], record_rgb[1], record_rgb[2]);
    ui_text(d, s, 4095 - 150, 4095 - 280, 190, VP_TEXT_RIGHT);
}

/* The instructions, as in the original: who is who, row by row. */
static void draw_instructions(vp_dvg *d, int frames)
{
    static const struct { int type; const char *text; } rows[7] = {
        { T_X,       "Basic, slow moving enemy." },
        { T_HUNTER,  "They move quickly. Watch out!" },
        { T_HX,      "Nearly invisible until it's right next to you!" },
        { T_DRAGON,  "Try to destroy the tail before the head." },
        { T_BOMBER,  "The longer they live the faster they deploy." },
        { T_STARLET, "Let them live. You'll be rewarded handsomely." },
        { T_BONUS,   "If it hits you you'll become stronger than ever." },
    };
    const int top = 3540, bottom = 720, left = 120, right = 3976, step = 400;
    panel(d, left, top, right, bottom);
    for (int i = 0; i < 7; i++) {
        if (frames < 10 + i * 25) break;            /* the rows appear one by one */
        float y = (float)(top - step / 2 - i * step);
        orig_draw_icon(d, i, 400, y, frames);       /* the game's 3D models */
        vp_color(d, 235, 235, 235);
        ui_fit(d, rows[i].text, 720, y - 62, 128, (float)(right - 720 - 70), VP_TEXT_LEFT);
    }
}

/* The original's logo: RELEASE, widely spaced, its A a red triangle (the
 * Hunter) with a glow. Bold: every stroke drawn several times, a little
 * apart, which is how a vector monitor draws a thick line. */
static void draw_logo_word(vp_dvg *d, float cx, float y, float h)
{
    static const char word[] = "RELEASE";
    static const float bold[9][2] = {
        { 0, 0 }, { 0.5f, 0 }, { 1, 0 }, { 0, 0.5f }, { 0.5f, 0.5f }, { 1, 0.5f }, { 0, 1 }, { 0.5f, 1 }, { 1, 1 }
    };
    float kx = h / XS_CAP * (G.ax / G.ay), track = h * 0.32f, t = h * 0.03f;
    float tri_w = h * 0.62f * (G.ax / G.ay);
    float widths[7], total = 0;

    for (int i = 0; i < 7; i++) {
        int g = xs_index(word[i]);
        widths[i] = i == 4 ? tri_w : (xs_right[g] - xs_left[g]) * kx;
        total += widths[i] + (i < 6 ? track : 0);
    }
    for (int pass = 0; pass < 9; pass++) {
        float ox = bold[pass][0] * t - t / 2, oy = bold[pass][1] * t - t / 2;
        float x = cx - total / 2;
        for (int i = 0; i < 7; i++) {
            if (i == 4) {                   /* the A: the red triangle */
                vp_color(d, 255, 40, 40);
                vp_moveto(d, (int)(x + ox), (int)(y + oy));
                vp_lineto(d, (int)(x + tri_w / 2 + ox), (int)(y + h * 1.08f + oy));
                vp_lineto(d, (int)(x + tri_w + ox), (int)(y + oy));
                vp_lineto(d, (int)(x + ox), (int)(y + oy));
            } else {
                char c[2] = { word[i], 0 };
                vp_color(d, 255, 255, 255);
                xs_text(d, c, x + ox, y + oy, h, VP_TEXT_LEFT, 1.0f);
            }
            x += widths[i] + track;
        }
    }
    /* the triangle's glow: a dim red halo round it */
    float x4 = cx - total / 2;
    for (int i = 0; i < 4; i++) x4 += widths[i] + track;
    float gx = tri_w * 0.14f, gy = h * 0.09f;
    vp_color(d, 110, 10, 10);
    vp_moveto(d, (int)(x4 - gx), (int)(y - gy * 0.6f));
    vp_lineto(d, (int)(x4 + tri_w / 2), (int)(y + h * 1.08f + gy * 1.4f));
    vp_lineto(d, (int)(x4 + tri_w + gx), (int)(y - gy * 0.6f));
    vp_lineto(d, (int)(x4 - gx), (int)(y - gy * 0.6f));
}

/* The tutorial's two lines at the top (original.c's orig_draw_tutorial). */
static void tutor_caption(vp_dvg *d, const char *one, const char *two, float alpha)
{
    int v = (int)(255 * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha));
    vp_color(d, v, v, v);
    ui_fit(d, one, 2048, 3480, 170, 3700, VP_TEXT_CENTER);
    ui_fit(d, two, 2048, 3230, 170, 3700, VP_TEXT_CENTER);
}

/* The first page: the logo over the base, its shield growing as in the
 * game (green edge, white wall) and fading. */
static void draw_logo(vp_dvg *d, double t)
{
    (void)t;
    draw_logo_word(d, 2048, 2600, 380);
    orig_draw_title_player(d, 1550, G.state_frames);   /* drawn as in the game */
}

static void draw_hiscores(vp_dvg *d, double t, int highlight);

static void draw_title(vp_dvg *d, double t)
{
    const int tutor = TITLE_LOGO_FRAMES, help = tutor + TITLE_TUTOR_FRAMES, scores = help + TITLE_HELP_FRAMES;
    if (G.state_frames >= scores)
        draw_hiscores(d, t, -1);
    else if (G.state_frames >= help)
        draw_instructions(d, G.state_frames - help);
    else if (G.state_frames >= tutor)
        orig_draw_tutorial(d, G.state_frames - tutor);
    else
        draw_logo(d, t);

    draw_corners(d);
    if (blink_on()) {
        vp_color(d, 255, 255, 255);
        text_c(d, "Press start to play", 2048, 440, 165);
    }
    vp_color(d, 130, 130, 130);
    ui_fit(d, "Original game by Gimogames - VectorPie version by Endprodukt", 2048, 140, 100, 3900, VP_TEXT_CENTER);
}

/* The player bursts into a small firework; then the original's panel: Game Over,
 * the score, and Play Again (START) or Title (the button). */
static void draw_gameover(vp_dvg *d, double t)
{
    float f = G.state_frames / (float)FPS;
    (void)t;
    orig_death_draw(d);                         /* the player bursts, a firework follows */
    draw_corners(d);
    if (f < 2.0f) return;                       /* the firework first */

    char s[32];
    panel(d, 960, 3280, 3136, 1100);
    vp_color(d, 255, 255, 255);
    text_c(d, "Game Over", 2048, 2820, 270);
    snprintf(s, sizeof s, "Score: %d", G.score);
    text_c(d, s, 2048, 2330, 220);
    if (!G.demo && vp_hiscore_rank(G.score) >= 0) {
        if (blink_on()) {
            vp_color(d, 130, 255, 160);
            text_c(d, "New high score!", 2048, 1500, 200);
        }
    } else if (f > 2.5f) {
        vp_color(d, 255, 255, 255);
        text_c(d, G.demo ? "Demo" : "Play Again", 2048, 1520, 200);
        if (blink_on()) {
            vp_color(d, 130, 130, 130);
            text_c(d, G.demo ? "Press start to play" : "Press start or a button", 2048, 1260, 85);
        }
    }
}

static void draw_hiscores(vp_dvg *d, double t, int highlight)
{
    static const unsigned char white[3] = { 255, 255, 255 }, green[3] = { 130, 255, 160 };
    vp_score table[VP_HISCORE_MAX];
    int n = vp_hiscore_load(table);
    char s[16];
    (void)t;

    panel(d, 720, 3560, 3376, 760);
    vp_color(d, 255, 255, 255);
    text_c(d, "High Scores", 2048, 3170, 230);
    for (int i = 0; i < n; i++) {
        float y = 2830.0f - i * 210.0f;
        float bright = 1.0f;
        if (i == highlight) {                       /* the new one: a live shield beside it */
            bright = 0.7f + 0.3f * sinf(G.state_frames * 0.3f);
            badge(d, 1130, y + 65, 75, 6, G.state_frames * 0.04f, 1, 0);
        }
        snprintf(s, sizeof s, "%d.", i + 1);
        color_scaled(d, score_rgb, bright);
        ui_text(d, s, 1400, y, 130, VP_TEXT_RIGHT);
        color_scaled(d, i == highlight ? green : white, bright);
        ui_text(d, table[i].name, 1560, y, 130, VP_TEXT_LEFT);
        snprintf(s, sizeof s, "%d", table[i].score);
        color_scaled(d, record_rgb, bright);
        ui_text(d, s, 3080, y, 130, VP_TEXT_RIGHT);
    }
}

/* Initials: three shields, the letters in them; the one being chosen is
 * live, a ring runs out of it when its letter is taken. */
static void draw_entry(vp_dvg *d)
{
    char s[32];
    draw_corners(d);
    panel(d, 780, 3500, 3316, 760);
    vp_color(d, 255, 255, 255);
    text_c(d, "New High Score", 2048, 3080, 230);
    snprintf(s, sizeof s, "Score: %d", G.score);
    vp_color(d, score_rgb[0], score_rgb[1], score_rgb[2]);
    text_c(d, s, 2048, 2700, 170);
    vp_color(d, 180, 180, 180);
    text_c(d, "Enter your initials", 2048, 2380, 110);

    for (int i = 0; i < 3; i++) {
        float cx = 2048.0f + (i - 1) * 700.0f, cy = 1720.0f, r = 330.0f;
        int current = i == G.entry_pos, done = i < G.entry_pos;
        badge(d, cx, cy, r, 6, current ? G.state_frames * 0.02f : 0.0f, current, !current && !done);
        if (G.entry_flash[i] > 0) {
            float k = 1.0f - G.entry_flash[i] / 15.0f;
            vp_color(d, (uint8_t)(255 * (1 - k)), (uint8_t)(255 * (1 - k)), (uint8_t)(255 * (1 - k)));
            screen_poly(d, cx, cy, r * (1.0f + 0.7f * k), 6, 0.0f);
        }
        if (i <= G.entry_pos) {
            char c[2] = { G.entry_name[i], 0 };
            if (current) vp_color(d, 130, 255, 160);
            else vp_color(d, 255, 255, 255);
            text_c(d, c, cx, cy - 130, 260);
        }
    }
    /* the time left: a bar shrinking to the middle and the seconds under it,
     * green, then yellow, red and blinking in the last five seconds */
    float left = ENTRY_TIMEOUT - G.entry_time, part = left / ENTRY_TIMEOUT;
    if (left < 0) left = 0, part = 0;
    int last = left <= 5.0f, show = !last || fmodf(left, 0.5f) > 0.2f;
    unsigned char rgb[3] = { 130, 255, 160 };
    if (left <= 10.0f) { rgb[0] = 255; rgb[1] = 230; rgb[2] = 90; }
    if (last) { rgb[0] = 255; rgb[1] = 70; rgb[2] = 60; }
    snprintf(s, sizeof s, "%d", (int)ceilf(left));
    vp_color(d, rgb[0], rgb[1], rgb[2]);
    if (show) text_c(d, s, 2048, 1060, 160);
    if (show) {
        float half = 1050.0f * part;
        for (int k = 0; k < 3; k++) {             /* three lines: a bar */
            vp_moveto(d, (int)(2048 - half), 1290 + 12 * k);
            vp_lineto(d, (int)(2048 + half), 1290 + 12 * k);
        }
    }
    vp_color(d, 150, 150, 150);
    text_c(d, "Button: next letter (hold to run)     Start: take it", 2048, 850, 85);
}

#include "original.c"

/* ======================================================================= */
/* sound: made in code, square waves and noise like the Vectrex's AY chip  */

#define RATE 22050

static vp_sound *tone(float dur, float f0, float f1, float noise, float decay, float vol)
{
    int n = (int)(dur * RATE);
    float *buf = malloc(sizeof(float) * (size_t)n);
    vp_sound *snd = NULL;
    if (!buf) return NULL;
    float ph = 0, nz = 0;
    unsigned x = 22222;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float f = f0 + (f1 - f0) * t / dur;
        ph += f / RATE;
        float sq = (ph - floorf(ph)) < 0.5f ? 1.0f : -1.0f;
        if (i % 3 == 0) { x = x * 1664525u + 1013904223u; nz = ((x >> 9) & 0xffff) / 32768.0f - 1.0f; }
        float env = expf(-decay * t) * fminf(1.0f, t * 500.0f) * (1.0f - t / dur);
        buf[i] = vol * env * ((1.0f - noise) * sq + noise * nz);
    }
    vp_sound_from_samples(G.audio, buf, n, 1, RATE, &snd);
    free(buf);
    return snd;
}

static vp_sound *arpeggio(const float *notes, int count, float note_dur, float vol)
{
    int per = (int)(note_dur * RATE), n = per * count;
    float *buf = malloc(sizeof(float) * (size_t)n);
    vp_sound *snd = NULL;
    if (!buf) return NULL;
    float ph = 0;
    for (int i = 0; i < n; i++) {
        int k = i / per;
        float t = (float)(i % per) / RATE;
        ph += notes[k] / RATE;
        float sq = (ph - floorf(ph)) < 0.5f ? 1.0f : -1.0f;
        buf[i] = vol * sq * expf(-6.0f * t) * fminf(1.0f, t * 500.0f);
    }
    vp_sound_from_samples(G.audio, buf, n, 1, RATE, &snd);
    free(buf);
    return snd;
}

static void make_sounds(void)
{
    static const float phase[]  = { 523, 659, 784, 1047 };
    static const float bonus[]  = { 784, 988, 1175, 1568, 1976 };
    static const float over[]   = { 523, 466, 392, 330, 262, 196 };
    static const float myst[]   = { 1568, 1175, 1568, 1976 };
    G.s_spawn     = tone(0.07f,  900, 1300, 0.0f, 25, 0.25f);
    G.s_release   = tone(0.30f,  420,   70, 0.55f, 7, 0.45f);
    G.s_explode   = tone(0.45f,  160,   60, 0.90f, 7, 0.55f);
    G.s_star      = tone(0.12f, 1760, 1760, 0.0f, 22, 0.25f);
    G.s_dragonhit = tone(0.22f,  260,  140, 0.25f, 9, 0.45f);
    G.s_shot      = tone(0.12f, 1500,  400, 0.1f, 15, 0.25f);
    G.s_shieldhit = tone(0.40f,  900,  200, 0.4f,  5, 0.5f);
    G.s_phase     = arpeggio(phase, 4, 0.09f, 0.3f);
    G.s_bonus     = arpeggio(bonus, 5, 0.06f, 0.3f);
    G.s_gameover  = arpeggio(over, 6, 0.18f, 0.35f);
    G.s_mystery   = arpeggio(myst, 4, 0.05f, 0.3f);

    /* the original's own sounds (from the APK), where it has them */
    char path[1024];
    vp_sound *s = NULL;
    if (vp_sound_load(G.audio, vp_asset_path("assets/sounds/Spawn.wav", path, sizeof path), &s) == VP_OK) {
        G.s_spawn = s;
        G.orig_spawn = 1;
    }
    if (vp_sound_load(G.audio, vp_asset_path("assets/sounds/Death.wav", path, sizeof path), &s) == VP_OK)
        G.s_gameover = s;
}

/* The music: parts of "Retro-Futurist Space" on the attract pages and for
 * the initials, "Starship 8-Bit" in the game, none while a game is over
 * (both the owner's choice). Changed with a short fade.
 * Each track plays from its start once, then loops from its loop point to
 * its end (the end is blended into the audio before the loop point, see
 * tools/loop_music.py --intro). vp_music_play loops only whole files, so the
 * game decodes the Oggs at start (the SDK's stb_vorbis) and streams them. */
enum { MUSIC_NONE, MUSIC_TITLE, MUSIC_GAME, MUSIC_ENTRY, MUSIC_TRACKS };

int stb_vorbis_decode_filename(const char *filename, int *channels, int *sample_rate, short **output);

static struct {
    const char *file;
    float loop_s;                       /* the loop point, seconds */
    short *pcm;                         /* stereo frames, or 0: played with vp_music_play */
    int frames, rate, loop;
} music[MUSIC_TRACKS] = {
    [MUSIC_TITLE] = { "assets/sounds/attract.ogg", 33.8832f },  /* the song from its start, bars 16..32 again */
    [MUSIC_GAME]  = { "assets/sounds/starship.ogg", 16.4980f },
    [MUSIC_ENTRY] = { "assets/sounds/entry.ogg", 0.0f },        /* bars 6..14, looped whole */
};
static int music_pos;                   /* the next frame to write */

static void music_load(void)
{
    char path[1024];
    for (int t = MUSIC_TITLE; t < MUSIC_TRACKS; t++) {
        int ch, rate;
        short *pcm;
        int n = stb_vorbis_decode_filename(vp_asset_path(music[t].file, path, sizeof path), &ch, &rate, &pcm);
        if (n <= 0) continue;
        if (ch != 2) { free(pcm); continue; }
        music[t].pcm = pcm;
        music[t].frames = n;
        music[t].rate = rate;
        music[t].loop = (int)(music[t].loop_s * rate + 0.5f);
        if (music[t].loop >= n) music[t].loop = 0;
    }
}

/* Keeps about a quarter second written ahead of the stream. */
static void music_feed(void)
{
    if (!G.music || !music[G.music_track].pcm) return;
    int t = G.music_track;
    float buf[2 * 1024];
    int want = music[t].rate / 4 - vp_stream_queued(G.audio, G.music);
    while (want > 0) {
        int n = music[t].frames - music_pos;
        if (n > want) n = want;
        if (n > 1024) n = 1024;
        const short *s = music[t].pcm + 2 * music_pos;
        for (int i = 0; i < 2 * n; i++) buf[i] = s[i] / 32768.0f;
        int put = vp_stream_write(G.audio, G.music, buf, n);
        music_pos += put;
        if (music_pos >= music[t].frames) music_pos = music[t].loop;
        if (put < n) break;             /* the stream is full */
        want -= n;
    }
}

static void music_want(int track)
{
    if (track == G.music_track) return;
    if (G.music) vp_voice_stop(G.audio, G.music, 0.6f);
    G.music = 0;
    G.music_track = track;
    if (!G.audio || track == MUSIC_NONE) return;
    float vol = 0.6f * G.set.music_volume / 100.0f;
    if (music[track].pcm) {
        G.music = vp_stream_play(G.audio, music[track].rate, vol);
        music_pos = 0;
        music_feed();
    } else {
        char path[1024];
        G.music = vp_music_play(G.audio, vp_asset_path(music[track].file, path, sizeof path), vol, 1, 0.5f);
    }
}

/* The START 1 lamp blinks whenever START does something: on the attract
 * pages, in the demo, for Title after a game, and entering initials. */
static void lamps_update(void)
{
    int waiting = G.state == ST_TITLE || G.state == ST_HISCORE || G.state == ST_ENTRY ||
                  (G.state == ST_PLAY && G.demo) ||
                  (G.state == ST_GAMEOVER && G.state_frames > 5 * FPS / 2);
    vp_lamps_set(waiting && blink_on() ? VP_LAMP_START1 : 0);
}

/* ======================================================================= */
/* settings, high scores, initials, the service menu                       */

static void settings_path(char *buf, size_t size)
{
    snprintf(buf, size, "%s/settings.ini", vp_save_dir());
}

static void apply_sound(void)
{
    if (!G.audio) return;
    vp_audio_set_master(G.audio, G.set.volume / 100.0f);
    if (G.music) vp_voice_set_volume(G.audio, G.music, 0.6f * G.set.music_volume / 100.0f, 0.1f);
}

static int clamp_percent(int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; }

/* save/settings.ini: one "key = value" a line. Older versions' keys
 * (effects = style, sound = 0 on / 1 half / 2 off) are read too. */
static void load_settings(void)
{
    char path[1024], line[256], text[VPB_LENGTH];
    int v;
    service_settings *s = &G.set;
    memset(s, 0, sizeof *s);
    s->original = 1;
    s->attract_sound = 1;
    s->volume = s->music_volume = s->effects_volume = 100;
    snprintf(s->button[SB_SHIELD], VPB_LENGTH, "%s", service_standard(SB_SHIELD));
    snprintf(s->menu_key, VPB_LENGTH, "key_F2");

    settings_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, " style = %d", &v) == 1 || sscanf(line, " effects = %d", &v) == 1) s->original = v != 0;
            if (sscanf(line, " sound = %d", &v) == 1) s->volume = v == 0 ? 100 : v == 1 ? 50 : 0;
            if (sscanf(line, " attract_sound = %d", &v) == 1) s->attract_sound = v != 0;
            if (sscanf(line, " volume = %d", &v) == 1) s->volume = clamp_percent(v, 100);
            if (sscanf(line, " music_volume = %d", &v) == 1) s->music_volume = clamp_percent(v, 150);
            if (sscanf(line, " effects_volume = %d", &v) == 1) s->effects_volume = clamp_percent(v, 150);
            if (sscanf(line, " shield = %127s", text) == 1) snprintf(s->button[SB_SHIELD], VPB_LENGTH, "%s", text);
            if (sscanf(line, " menu_key = %127s", text) == 1) snprintf(s->menu_key, VPB_LENGTH, "%s", text);
        }
        fclose(f);
    }
    vpb_normalize(s->button[SB_SHIELD], VPB_LENGTH);
    service_menu_key(s->menu_key);
    G.fx = s->original && !G.classic_arg;
}

static void save_settings(void)
{
    char path[1024];
    const service_settings *s = &G.set;
    settings_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "style = %d\nattract_sound = %d\nvolume = %d\nmusic_volume = %d\neffects_volume = %d\n"
               "shield = %s\nmenu_key = %s\n",
            s->original, s->attract_sound, s->volume, s->music_volume, s->effects_volume,
            s->button[SB_SHIELD][0] ? s->button[SB_SHIELD] : "none", s->menu_key[0] ? s->menu_key : "key_F2");
    fclose(f);
}

/* The table a new cabinet starts with: low enough to get onto, the top
 * ones a challenge (see STATUS.md for how they were chosen). */
static void seed_hiscores(void)
{
    /* on the original's scale (a simulated player scored 200..1400, the
     * original's own best scores were ~1500), the top a real challenge */
    static const struct { const char *name; int score; } table[VP_HISCORE_MAX] = {
        { "END", 4000 }, { "NMA", 3400 }, { "MIO", 2900 }, { "MAL", 2400 }, { "VTK", 2000 },
        { "GMO", 1600 }, { "TOM", 1300 }, { "ALX", 1000 }, { "VEC", 700 },  { "PIE", 400 },
    };
    for (int i = 0; i < VP_HISCORE_MAX; i++) vp_hiscore_insert(table[i].name, table[i].score);
}

/* Earlier versions seeded other tables: replace their entries, keep the
 * players' own. */
static void migrate_hiscores(void)
{
    static const struct { const char *name; int score; } old[] = {
        { "MAL", 2000 }, { "VTK", 1600 }, { "GMO", 1300 }, { "TOM", 1000 }, { "ALX", 800 },
        { "VEC", 600 },  { "PIE", 450 },  { "ARC", 300 },  { "REL", 200 },  { "ASE", 100 },
        { "MAL", 20000 }, { "VTK", 15000 }, { "GMO", 12000 }, { "TOM", 9000 }, { "ALX", 7000 },
        { "VEC", 5000 },  { "PIE", 4000 },  { "ARC", 3000 },  { "REL", 2000 }, { "ASE", 1000 },
        { "MAL", 100000 }, { "TOM", 50000 }, { "VTK", 30000 }, { "ALX", 20000 }, { "VEC", 10000 },
    };
    vp_score table[VP_HISCORE_MAX], keep[VP_HISCORE_MAX];
    int n = vp_hiscore_load(table), k = 0, found = 0;
    for (int i = 0; i < n; i++) {
        int seed = 0;
        for (size_t j = 0; j < sizeof old / sizeof old[0]; j++)
            if (table[i].score == old[j].score && strcmp(table[i].name, old[j].name) == 0) seed = 1;
        if (seed) found = 1;
        else keep[k++] = table[i];
    }
    if (!found) return;
    remove(vp_hiscore_path());
    seed_hiscores();
    for (int i = 0; i < k; i++) vp_hiscore_insert(keep[i].name, keep[i].score);
}

static void refresh_best(void)
{
    vp_score table[VP_HISCORE_MAX];
    G.best = vp_hiscore_load(table) > 0 ? table[0].score : 0;
}

static void reset_hiscores(void)
{
    remove(vp_hiscore_path());
    seed_hiscores();
    refresh_best();
}

/* Initials with one button: the button goes to the next letter (held, it
 * runs), START takes it. Up / down and left on a stick work too. */
static const char entry_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";

static void entry_begin(void)
{
    memset(G.entry_name, 0, sizeof G.entry_name);
    G.entry_name[0] = 'A';
    G.entry_pos = 0;
    memset(G.entry_flash, 0, sizeof G.entry_flash);
    G.entry_time = 0;
    G.entry_repeat = 0;
}

static int entry_update(vp_input *in, float dt)
{
    int n = (int)sizeof entry_letters - 1, step = 0;
    int was = (int)ceilf(ENTRY_TIMEOUT - G.entry_time);
    G.entry_time += dt;
    int now = (int)ceilf(ENTRY_TIMEOUT - G.entry_time);
    if (now != was && now <= 5 && now > 0) play(G.s_star, 0.4f, 0.6f);   /* the last seconds tick */
    for (int i = 0; i < 3; i++) if (G.entry_flash[i] > 0) G.entry_flash[i]--;
    if (shield_pressed() || vp_input_menu_up(in)) {
        step = 1;
        G.entry_repeat = 0.45f;          /* held: it runs after a moment */
    } else if (buttons_held(in)) {
        G.entry_repeat -= dt;
        if (G.entry_repeat <= 0) { step = 1; G.entry_repeat = 0.12f; }
    }
    if (vp_input_menu_down(in)) step = -1;
    if (step) {
        const char *at = strchr(entry_letters, G.entry_name[G.entry_pos]);
        int i = at ? (int)(at - entry_letters) : 0;
        G.entry_name[G.entry_pos] = entry_letters[(i + step + n) % n];
    }
    if (vp_input_menu_left_pressed(in) && G.entry_pos > 0) {
        G.entry_name[G.entry_pos] = 0;
        G.entry_pos--;
    }
    if (vp_input_menu_confirm(in) || vp_input_menu_right_pressed(in)) {
        G.entry_flash[G.entry_pos] = 15;
        play(G.s_star, 0.5f, 1.0f);
        if (++G.entry_pos >= 3) return 1;
        G.entry_name[G.entry_pos] = 'A';                                /* each letter starts at A */
    }
    if (G.entry_time > ENTRY_TIMEOUT) return 1;
    return 0;
}

/* The service menu (service.c) asks the game to let it be heard. */
void service_hear(int volume, int music_volume, int effects_volume)
{
    G.set.volume = volume;
    G.set.music_volume = music_volume;
    G.set.effects_volume = effects_volume;
    apply_sound();
}

void service_click(int ok)
{
    if (G.audio && G.s_star) vp_sound_play(G.audio, G.s_star, 0.35f * G.set.effects_volume / 100.0f, ok ? 1.0f : 0.7f, 0);
}

void service_preview(void)
{
    if (G.audio && G.s_spawn) vp_sound_play(G.audio, G.s_spawn, 0.5f * G.set.effects_volume / 100.0f, 1.0f, 0);
}

static service_settings menu_before;    /* to go back to on CANCEL */

static void menu_open(void)
{
    menu_before = G.set;
    service_open(&G.set);
    set_state(ST_MENU);
}

/* SAVE takes the menu's settings over and keeps them; CANCEL goes back to
 * what was. Either way, back to the title. */
static void menu_update(vp_input *in, int hotkey)
{
    int result = service_update(in, hotkey);
    if (result == SERVICE_OPEN) return;
    if (result == SERVICE_SAVED) {
        G.set = *service_get();
        if (G.set.reset_best) {
            reset_hiscores();
            G.set.reset_best = 0;
        }
        service_menu_key(G.set.menu_key);
        G.fx = G.set.original && !G.classic_arg;
        save_settings();
    } else {
        G.set = menu_before;
    }
    apply_sound();
    set_state(ST_TITLE);
}

/* The version in game.ini (next to the executable on the cabinet), for the
 * menu's corner. */
static void read_version(void)
{
    char path[1024], line[128], version[32];
    FILE *f = fopen(vp_asset_path("game.ini", path, sizeof path), "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, " version = %31s", version) == 1) service_version(version);
    fclose(f);
}

/* ======================================================================= */
/* vp_run                                                                   */

static int init(vp_game *g)
{
    for (int k = 0; k < 383; k++) {
        double a = (k % 360) * 3.14159265358979 / 180.0;
        cir_y[k] = (int)(108.0 * cos(a));          /* truncated, as in data.i */
        cir_x[k] = (int)(-108.0 * sin(a));
    }
    make_shapes();
    xs_init();
    orig_view();                        /* the title draws the original's player */
    G.sided = 6;                       /* the title's polygon, before the first game */

    /* The board square is stretched to the tube: compensate so circles are
     * round. Raw 16129 (the Vectrex screen's half height) = 2040 board. */
    const char *v = getenv("VP_VERTICAL");
    int vertical = G.vertical || (v && v[0] == '1');
    float k = 2040.0f / 16129.0f * VIEW_ZOOM;
    G.ax = vertical ? k : k * 0.75f;
    G.ay = vertical ? k * 0.75f : k;

    service_init();                     /* TAB kept for the menu */
    read_version();
    load_settings();

    G.audio = vp_game_audio(g);
    if (G.audio) { make_sounds(); music_load(); }
    apply_sound();

    vp_score table[VP_HISCORE_MAX];
    if (vp_hiscore_load(table) == 0) seed_hiscores();
    else migrate_hiscores();
    refresh_best();
    G.rnd1 = 0x5A;
    G.rnd2 = 0xA5;
    G.last_rank = -1;
    set_state(ST_TITLE);
    return VP_OK;
}

static void update(vp_game *g, float dt)
{
    vp_input *in = vp_game_input(g);
    vpb_update();                       /* the keys and buttons of this step */
    int hotkey = service_hotkey();
    G.state_frames++;
    /* the starfield flies on everywhere it is drawn: the screens and the
     * original's game (the Vectrex game has its own) */
    if (G.state != ST_MENU && !(G.state == ST_PLAY && !G.fx)) bg_update();
    music_want(G.state == ST_PLAY && !G.demo ? MUSIC_GAME :
               G.state == ST_ENTRY ? MUSIC_ENTRY :
               G.state == ST_GAMEOVER && !G.demo ? MUSIC_NONE :
               in_attract() && !G.set.attract_sound ? MUSIC_NONE : MUSIC_TITLE);
    music_feed();
    lamps_update();

    /* F2 or TAB: the service menu, as in the cabinet's other games (not
     * while initials are being entered) */
    if (hotkey && G.state != ST_MENU && G.state != ST_ENTRY) {
        menu_open();
        return;
    }

    switch (G.state) {
    case ST_TITLE:
        G.recal++;
        G.btn_hist = (unsigned char)((G.btn_hist << 1) | (buttons_held(in) ? 1 : 0));
        rng_step();
        if (start_pressed(in)) { init_game(0, buttons_held(in)); set_state(ST_PLAY); break; }
        if (G.state_frames > DEMO_AFTER_FRAMES) { init_game(1, 0); set_state(ST_PLAY); }
        break;

    case ST_PLAY:
        if (G.demo && start_pressed(in)) { init_game(0, buttons_held(in)); set_state(ST_PLAY); break; }
        if (G.fx) orig_step(in, dt);
        else game_step(in);
        if (G.dying) { game_over(); break; }
        if (G.demo && (G.fx ? orig_demo_over() : (G.spawn_count & 0xFF) >= 36)) {
            G.last_rank = -1;
            set_state(ST_HISCORE);
        }
        break;

    case ST_GAMEOVER:
        orig_death_step(dt);
        /* the demo's: on to the table; a game's: into the table first, else
         * Play Again (START or a button) or, after a while, the title */
        if (G.demo) {
            if (start_pressed(in)) { init_game(0, buttons_held(in)); set_state(ST_PLAY); }
            else if (G.state_frames > 6 * FPS) { G.last_rank = -1; set_state(ST_HISCORE); }
        } else if (vp_hiscore_rank(G.score) >= 0) {
            if ((G.state_frames > 5 * FPS / 2 && any_pressed(in)) || G.state_frames > 6 * FPS) {
                entry_begin();
                set_state(ST_ENTRY);
            }
        } else if (G.state_frames > 5 * FPS / 2) {
            if (start_pressed(in)) { init_game(0, buttons_held(in)); set_state(ST_PLAY); }
            else if (G.state_frames > 12 * FPS) set_state(ST_TITLE);
        }
        break;

    case ST_ENTRY:
        if (entry_update(in, dt)) {
            G.last_rank = vp_hiscore_insert(G.entry_name, G.score);
            refresh_best();
            set_state(ST_HISCORE);
        }
        break;

    case ST_MENU:
        menu_update(in, hotkey);
        break;

    case ST_HISCORE:
        if (G.last_rank < 0 && start_pressed(in)) { init_game(0, buttons_held(in)); set_state(ST_PLAY); break; }
        if ((G.state_frames > FPS / 2 && any_pressed(in)) || G.state_frames > 10 * FPS)
            set_state(ST_TITLE);
        break;
    }
}

static void draw(vp_game *g, vp_dvg *d)
{
    double t = vp_game_time(g);
    if (G.state != ST_PLAY && G.state != ST_MENU) bg_draw(d);
    switch (G.state) {
    case ST_TITLE:    draw_title(d, t); break;
    case ST_PLAY:
        if (G.fx) { orig_draw(d); draw_hud(d); }
        else draw_game(d);
        break;
    case ST_GAMEOVER: draw_gameover(d, t); break;
    case ST_ENTRY:    draw_entry(d); break;
    case ST_HISCORE:  draw_hiscores(d, t, G.last_rank); break;
    case ST_MENU:     service_draw(d); break;
    }
}

int main(int argc, char **argv)
{
    vp_game_desc desc = {0};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vertical") == 0) G.vertical = 1;
        if (strcmp(argv[i], "--classic") == 0) G.classic_arg = 1;   /* the Vectrex gameplay, whatever is set */
    }
    desc.title     = "Release";
    desc.init      = init;
    desc.update    = update;
    desc.draw      = draw;
    desc.update_hz = FPS;               /* one update = one Vectrex frame */
    desc.frame_hz  = FPS;
    return vp_run(&desc, argc, argv);
}
