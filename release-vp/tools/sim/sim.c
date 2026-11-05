/* sim.c — runs Release without the SDK: stubs for the vp_* calls the game
 * uses, a bot (or a fixed button pattern) as the player, statistics every
 * few seconds and SVG pictures of chosen frames. For checking the game
 * logic and the drawing on a machine with no window.
 *
 *   gcc -O2 -I<sdk>/include -Isrc tools/sim/sim.c src/data.c -lm -o sim
 *   ./sim [frames] [bot|pattern|demo] [svg-dir] [snapshot frames...]
 */
#define main release_main
#include "../../src/main.c"
#undef main

/* ---- stubs ------------------------------------------------------------- */

static int sim_held, sim_pressed_start;
static char sim_dvg;                    /* vp_text draws nothing for a NULL output */
#define SIM_DVG ((vp_dvg *)&sim_dvg)
static long sim_frame;

typedef struct { int x0, y0, x1, y1; unsigned char r, g, b; } seg;
static seg segs[200000];
static int nseg, cur_x, cur_y;
static unsigned char cr, cg, cb;

void vp_color(vp_dvg *d, uint8_t r, uint8_t g, uint8_t b) { (void)d; cr = r; cg = g; cb = b; }
void vp_get_color(const vp_dvg *d, uint8_t *r, uint8_t *g, uint8_t *b) { (void)d; *r = cr; *g = cg; *b = cb; }
void vp_moveto(vp_dvg *d, int x, int y) { (void)d; cur_x = x; cur_y = y; }
void vp_lineto(vp_dvg *d, int x, int y)
{
    (void)d;
    if (nseg < (int)(sizeof segs / sizeof segs[0]))
        segs[nseg++] = (seg){ cur_x, cur_y, x, y, cr, cg, cb };
    cur_x = x; cur_y = y;
}
/* what the frame costs the board: lit and invisible length, board units
 * (vp_dvg_stats' lit_length / blank_length; the USB-DVG draws about 3
 * million points a second) */
static void frame_cost(double *lit, double *blank)
{
    int bx = 2048, by = 2048;
    *lit = *blank = 0;
    for (int i = 0; i < nseg; i++) {
        const seg *s = &segs[i];
        double l = hypot(s->x1 - s->x0, s->y1 - s->y0);
        if (s->x0 != bx || s->y0 != by) *blank += hypot(s->x0 - bx, s->y0 - by);
        if (s->r | s->g | s->b) *lit += l; else *blank += l;
        bx = s->x1; by = s->y1;
    }
}
#ifndef SIM_REAL_TEXT   /* else the SDK's own vp_text.cpp draws the letters */
void vp_text_font(vp_font f) { (void)f; }
void vp_text_border(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; }
void vp_text_wobble(float a, double t) { (void)a; (void)t; }
void vp_text(vp_dvg *d, const char *s, float x, float y, float h, int align)
{
    float w = strlen(s) * h * 0.8f;
    float x0 = align == VP_TEXT_CENTER ? x - w / 2 : align == VP_TEXT_RIGHT ? x - w : x;
    vp_moveto(d, (int)x0, (int)y);
    vp_lineto(d, (int)(x0 + w), (int)y);
    vp_lineto(d, (int)(x0 + w), (int)(y + h));
    vp_lineto(d, (int)x0, (int)(y + h));
    vp_lineto(d, (int)x0, (int)y);
}

float vp_text_width(const char *s, float h) { return strlen(s) * h * 0.8f; }
void vp_text_fit(vp_dvg *d, const char *s, float x, float y, float h, float w, int align)
{
    float need = strlen(s) * h * 0.8f;
    vp_text(d, s, x, y, need > w ? h * w / need : h, align);
}
#endif

static vp_score table_mem[VP_HISCORE_MAX];
static int table_n;
int vp_hiscore_load(vp_score out[VP_HISCORE_MAX]) { memcpy(out, table_mem, sizeof table_mem); return table_n; }int vp_hiscore_rank(int score)
{
    if (score <= 0) return -1;
    for (int i = 0; i < table_n; i++) if (score > table_mem[i].score) return i;
    return table_n < VP_HISCORE_MAX ? table_n : -1;
}
int vp_hiscore_insert(const char *name, int score)
{
    int r = vp_hiscore_rank(score);
    if (r < 0) return -1;
    if (table_n < VP_HISCORE_MAX) table_n++;
    memmove(&table_mem[r + 1], &table_mem[r], sizeof(vp_score) * (size_t)(table_n - 1 - r));
    snprintf(table_mem[r].name, sizeof table_mem[r].name, "%s", name);
    table_mem[r].score = score;
    return r;
}
void vp_hiscore_draw(vp_dvg *d, const vp_score *t, int n, float x, float y, float h, int hl, double tm)
{
    (void)hl; (void)tm;
    for (int i = 0; i < n; i++) {
        char s[32];
        snprintf(s, sizeof s, "%d. %s %d", i + 1, t[i].name, t[i].score);
        vp_text(d, s, x, y - i * h * 1.6f, h, VP_TEXT_CENTER);
    }
}
const char *vp_hiscore_path(void) { return "/tmp/sim_highscores.txt"; }
const char *vp_save_dir(void) { return "/tmp"; }
void vp_audio_set_master(vp_audio *a, float v) { (void)a; (void)v; }

int vp_input_held(const vp_input *in, const char *a) { (void)in; return strcmp(a, "shield") == 0 && sim_held; }
int vp_input_pressed(const vp_input *in, const char *a) { (void)in; return strcmp(a, "start1") == 0 && sim_pressed_start; }
int vp_input_define(vp_input *in, const char *a, const char *s) { (void)in; (void)a; (void)s; return 0; }
int vp_input_load(vp_input *in, const char *p) { (void)in; (void)p; return 0; }
int vp_input_save(const vp_input *in, const char *p) { (void)in; (void)p; return 0; }
void vp_input_reserve_key(vp_input *in, int k) { (void)in; (void)k; }
int vp_input_key_pressed(const vp_input *in, int k) { (void)in; (void)k; return 0; }
const char *vp_input_controls(const vp_input *in, const char *a) { (void)in; (void)a; return "standard"; }
const char *vp_input_spelled(const vp_input *in, const char *a, const char *c, char *buf, size_t n)
{
    (void)in; (void)a; (void)c;
    snprintf(buf, n, "LEFT CTRL + LEFT ALT + SPACE + LEFT SHIFT");
    return buf;
}
int vp_input_menu_confirm(const vp_input *in) { (void)in; return 0; }
int vp_input_menu_up(const vp_input *in) { (void)in; return 0; }
int vp_input_menu_down(const vp_input *in) { (void)in; return 0; }
int vp_input_menu_left_pressed(const vp_input *in) { (void)in; return 0; }
int vp_input_menu_right_pressed(const vp_input *in) { (void)in; return 0; }
void vp_capture_moved(vp_capture *c) { (void)c; }
void vp_capture_begin(vp_capture *c, const vp_input *in, const char *a) { (void)c; (void)in; (void)a; }
int vp_capture_step(vp_capture *c, vp_input *in) { (void)c; (void)in; return VP_CAPTURE_WAITING; }
void vp_capture_standard(vp_capture *c, vp_input *in, const char *a) { (void)c; (void)in; (void)a; }
void vp_capture_toggle(vp_capture *c, vp_input *in, const char *a) { (void)c; (void)in; (void)a; }
void vp_capture_filter(vp_capture *c, int *l, int *r) { (void)c; (void)l; (void)r; }
const char *vp_capture_said(vp_capture *c) { (void)c; return NULL; }
void vp_capture_help(const vp_capture *c, const char **one, const char **two)
{
    (void)c;
    *one = "START  LAY OUT   LEFT / RIGHT  NONE OR STANDARD";
    *two = "DELETE  STANDARD";
}
vp_input *vp_game_input(vp_game *g) { (void)g; return NULL; }
vp_audio *vp_game_audio(vp_game *g) { (void)g; return NULL; }
double vp_game_time(vp_game *g) { (void)g; return sim_frame / (double)FPS; }
vp_voice vp_sound_play(vp_audio *a, vp_sound *s, float v, float p, int l) { (void)a; (void)s; (void)v; (void)p; (void)l; return 0; }
int vp_sound_from_samples(vp_audio *a, const float *s, int f, int c, int r, vp_sound **o) { (void)a; (void)s; (void)f; (void)c; (void)r; *o = NULL; return 0; }
int vp_run(const vp_game_desc *d, int argc, char **argv) { (void)d; (void)argc; (void)argv; return 0; }
int vp_sound_load(vp_audio *a, const char *p, vp_sound **o) { (void)a; (void)p; *o = NULL; return -1; }
vp_voice vp_music_play(vp_audio *a, const char *p, float v, int l, float f) { (void)a; (void)p; (void)v; (void)l; (void)f; return 0; }
void vp_voice_stop(vp_audio *a, vp_voice v, float f) { (void)a; (void)v; (void)f; }
const char *vp_asset_path(const char *r, char *buf, size_t n) { snprintf(buf, n, "%s", r); return buf; }
void vp_lamps_set(unsigned m) { (void)m; }
void vp_voice_set_volume(vp_audio *a, vp_voice v, float vol, float f) { (void)a; (void)v; (void)vol; (void)f; }
vp_voice vp_stream_play(vp_audio *a, int r, float v) { (void)a; (void)r; (void)v; return 0; }
int vp_stream_write(vp_audio *a, vp_voice v, const float *f, int n) { (void)a; (void)v; (void)f; return n; }
int vp_stream_queued(vp_audio *a, vp_voice v) { (void)a; (void)v; return 0; }
int stb_vorbis_decode_filename(const char *f, int *c, int *r, short **o) { (void)f; (void)c; (void)r; (void)o; return -1; }

/* vpbind.h: the shield button is the bot's; no keys, no menu input */
void vpb_update(void) {}
int vpb_held(const char *c) { return c && strstr(c, "Left_Ctrl") && sim_held; }
int vpb_pressed(const char *c) { (void)c; return 0; }
void vpb_normalize(char *c, size_t n) { (void)c; (void)n; }
void vpb_reserve_key(int k) { (void)k; }
int vpb_key_held(int k) { (void)k; return 0; }
int vpb_key_pressed(int k) { (void)k; return 0; }
int vpb_menu_confirm(vp_input *in) { (void)in; return 0; }
int vpb_menu_way_x(void) { return 0; }
int vpb_menu_way_y(void) { return 0; }
const char *vpb_spelled(const char *c, char *buf, size_t n) { (void)c; snprintf(buf, n, "YOKE BUTTON 1 + YOKE BUTTON 2 + LEFT CTRL + JOY1 BUTTON 1"); return buf; }
void vpb_layout_moved(vpb_layout *l) { (void)l; }
void vpb_layout_begin(vpb_layout *l, const char *h) { (void)l; (void)h; }
int vpb_layout_step(vpb_layout *l, vp_input *in, char *h, size_t n, const char *s) { (void)l; (void)in; (void)h; (void)n; (void)s; return VPB_WAITING; }
void vpb_layout_standard(vpb_layout *l, char *h, size_t n, const char *s) { (void)l; (void)h; (void)n; (void)s; }
void vpb_layout_toggle(vpb_layout *l, char *h, size_t n, const char *s) { (void)l; (void)h; (void)n; (void)s; }
void vpb_layout_filter(vpb_layout *l, int *a, int *b) { (void)l; (void)a; (void)b; }
const char *vpb_layout_said(vpb_layout *l) { (void)l; return NULL; }
void vpb_layout_help(const vpb_layout *l, const char **one, const char **two) { (void)l; (void)one; (void)two; }

/* ---- the player -------------------------------------------------------- */

/* Hold until the shield has passed the nearest enemy, then release; let go
 * early when something is about to reach the base. */
static int bot(void)
{
    int nearest = 1000;
    for (int i = 0; i < G.n_order; i++) {
        const obj *o = &G.pool[G.order[i]];
        if (o->dead || o->type >= T_HITTABLE || o->type == T_STARLET) continue;
        if (o->type == T_HX && o->scale >= 128) continue;
        if (o->scale < nearest) nearest = o->scale;
    }
    if (!sim_held) return G.shield_delay < 0 && nearest < 120;
    if (!G.shield_visible) return 1;
    if (G.shield_start > nearest + 6 || G.shield_start >= SHIELD_MAX) return 0;
    return 1;
}

/* A player with human reactions: sees an enemy only when it is close enough,
 * reacts after a delay, and lets go a little early or late. */
static unsigned human_seed = 1;
static int human_wait, human_target = -1;

static int human_rand(int n)
{
    human_seed = human_seed * 1103515245u + 12345u;
    return (int)((human_seed >> 16) % (unsigned)n);
}

static int human(void)
{
    int nearest = 1000;
    for (int i = 0; i < G.n_order; i++) {
        const obj *o = &G.pool[G.order[i]];
        if (o->dead || o->type >= T_HITTABLE || o->type == T_STARLET) continue;
        if (o->type == T_HX && o->scale >= 100) continue;
        if (o->scale < nearest) nearest = o->scale;
    }
    if (human_wait > 0) { human_wait--; return sim_held; }
    if (!sim_held) {
        if (G.shield_delay < 0 && nearest < 70 + human_rand(60)) {
            human_wait = 8 + human_rand(15);            /* reaction time */
            human_target = -1;
            return 1;
        }
        return 0;
    }
    if (!G.shield_visible) return 1;
    if (human_target < 0) human_target = nearest + human_rand(36) - 12;   /* misjudged */
    if (G.shield_start > human_target || G.shield_start >= SHIELD_MAX) {
        human_wait = 5 + human_rand(10);
        return 0;
    }
    return 1;
}

static void write_svg(const char *dir, long frame)
{
    char path[512];
    snprintf(path, sizeof path, "%s/frame_%05ld.svg", dir, frame);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='1024' height='768' viewBox='0 0 1024 768'>"
               "<rect width='1024' height='768' fill='black'/>\n");
    for (int i = 0; i < nseg; i++) {
        seg *s = &segs[i];
        float x0 = s->x0 / 4.0f, y0 = 768 - s->y0 * 768 / 4096.0f;
        float x1 = s->x1 / 4.0f, y1 = 768 - s->y1 * 768 / 4096.0f;
        if (x0 == x1 && y0 == y1)
            fprintf(f, "<circle cx='%.1f' cy='%.1f' r='1.2' fill='rgb(%d,%d,%d)'/>\n", x0, y0, s->r, s->g, s->b);
        else
            fprintf(f, "<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='rgb(%d,%d,%d)' stroke-width='1.3'/>\n",
                    x0, y0, x1, y1, s->r, s->g, s->b);
    }
    fprintf(f, "</svg>\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    long frames = argc > 1 ? atol(argv[1]) : 6000;
    const char *mode = argc > 2 ? argv[2] : "bot";
    const char *dir = argc > 3 ? argv[3] : NULL;

    /* bot, human and pattern play the Vectrex logic (classic); orig plays
     * the Android original's, with its demo player (who can die here) */
    int vectrex = strcmp(mode, "bot") == 0 || strcmp(mode, "human") == 0 || strcmp(mode, "pattern") == 0;
    G.fx = 1;
    G.classic_arg = vectrex || getenv("SIM_CLASSIC") != NULL;
    if (getenv("SIM_SEED")) human_seed = (unsigned)atoi(getenv("SIM_SEED"));
    init(NULL);
    int demo = strcmp(mode, "demo") == 0, title = strcmp(mode, "title") == 0;
    if (strcmp(mode, "entry") == 0) { G.score = 4321; entry_begin(); set_state(ST_ENTRY); title = 1; }
    if (strcmp(mode, "menu") == 0) {
        menu_open();
        if (getenv("SIM_PAGE")) service_show(atoi(getenv("SIM_PAGE")), 0);
        title = 1;
    }
    if (strcmp(mode, "gameover") == 0) { G.score = 1079; orig_death_start(); set_state(ST_GAMEOVER); title = 1; }
    if (strcmp(mode, "scores") == 0) { G.last_rank = 2; set_state(ST_HISCORE); title = 1; }
    if (demo) { init_game(1, 0); set_state(ST_PLAY); }
    else if (!title) { sim_pressed_start = 1; update(NULL, 1.0f / FPS); sim_pressed_start = 0; }
    if (G.fx && getenv("SIM_SEED")) O.seed = (unsigned)atoi(getenv("SIM_SEED")) * 2654435761u;
    if (G.fx && getenv("SIM_LONG")) O.demo_careless = 1e9f;     /* the bot never tires */

    long dead_at = -1;
    int max_phase = 0, max_objects = 0, max_lines = 0;
    long max_lines_at = 0, max_cost_at = 0;
    double max_cost = 0, max_cost_lit = 0;
    for (sim_frame = 1; sim_frame <= frames; sim_frame++) {
        if (strcmp(mode, "bot") == 0) sim_held = bot();
        else if (strcmp(mode, "human") == 0) sim_held = human();
        else if (strcmp(mode, "orig") == 0) sim_held = o_demo_button(1.0f / FPS);
        else if (strcmp(mode, "pattern") == 0) sim_held = (sim_frame % 70) < 55;
        update(NULL, 1.0f / FPS);
        if (G.state != ST_PLAY && !title && dead_at < 0) { dead_at = sim_frame; if (!getenv("SIM_DEATH")) break; }
        if (G.phase_count > max_phase) max_phase = G.phase_count;
        if (G.object_count > max_objects) max_objects = G.object_count;

        if (getenv("SIM_LINES")) {              /* the most lines a frame has */
            nseg = 0;
            draw(NULL, SIM_DVG);
            if (nseg > max_lines) { max_lines = nseg; max_lines_at = sim_frame; }
            double lit, blank;
            frame_cost(&lit, &blank);
            if (lit + blank > max_cost) { max_cost = lit + blank; max_cost_lit = lit; max_cost_at = sim_frame; }
            if (getenv("SIM_COST")) printf("cost f%ld state %d lines %d lit %.0f blank %.0f\n", sim_frame, G.state, nseg, lit, blank);
            if (getenv("SIM_PARTS") && sim_frame % 100 == 0) {    /* the parts: starfield, HUD */
                double l2, b2, l3, b3;
                int n1 = nseg;
                nseg = 0; bg_draw(SIM_DVG); frame_cost(&l2, &b2); int n2 = nseg;
                nseg = 0; if (G.state == ST_PLAY) draw_hud(SIM_DVG); frame_cost(&l3, &b3); int n3 = nseg;
                printf("parts f%ld all %d: %.0f+%.0f | stars %d: %.0f+%.0f | hud %d: %.0f+%.0f\n", sim_frame,
                       n1, lit, blank, n2, l2, b2, n3, l3, b3);
            }
        }
        int snap = 0;
        for (int i = 4; i < argc; i++) if (atol(argv[i]) == sim_frame) snap = 1;
        if (snap && dir) {
            nseg = 0;
            draw(NULL, SIM_DVG);
            write_svg(dir, sim_frame);
        }
        if (sim_frame % 500 == 0 && G.fx && !title) {
            int live[O_TYPES] = {0};
            for (int i = 0; i < O_MAX; i++) if (O.e[i].used) live[O.e[i].type]++;
            nseg = 0;
            draw(NULL, SIM_DVG);
            printf("f%5ld %3lds gts %.2f score %5d  F%d D%d K%d C%d A%d S%d P%d T%d c%d  sides %d  lines %d\n",
                   sim_frame, sim_frame / FPS, O.gts, G.score, live[O_FIGHTER], live[O_DIVER], live[O_KITE],
                   live[O_CARRIER], live[O_ALLY], live[O_STEALTH], live[O_POWERUP], live[O_KITE_TAIL],
                   live[O_CARRIER_CHILD], O.sides, nseg);
        } else if (sim_frame % 500 == 0) {
            int cnt[T_TYPES] = {0};
            for (int i = 0; i < G.n_order; i++) if (!G.pool[G.order[i]].dead) cnt[G.pool[G.order[i]].type]++;
            nseg = 0;
            draw(NULL, SIM_DVG);
            printf("f%5ld %3lds phase %2d score %6d objs %2d  X%d HX%d Hu%d Bo%d Sh%d Dr%d Ch%d St%d ?%d Ex%d Po%d SF%d  bonus %d  lines %d\n",
                   sim_frame, sim_frame / FPS, G.phase_count, G.score, G.object_count,
                   cnt[T_X], cnt[T_HX], cnt[T_HUNTER], cnt[T_BOMBER], cnt[T_SHOT], cnt[T_DRAGON],
                   cnt[T_CHILD], cnt[T_STARLET], cnt[T_BONUS], cnt[T_EXPLOSION], cnt[T_POPUP],
                   cnt[T_STARFIELD], G.bonus_active, nseg);
        }
    }
    if (getenv("SIM_LINES")) {
        printf("most lines: %d at frame %ld\n", max_lines, max_lines_at);
        printf("most length: %.0f (lit %.0f) at frame %ld\n", max_cost, max_cost_lit, max_cost_at);
    }
    printf("end: %s at frame %ld (%.1f s), score %d, highest phase %d, most objects %d\n",
           dead_at >= 0 ? "game over" : "still alive", dead_at >= 0 ? dead_at : frames,
           (dead_at >= 0 ? dead_at : frames) / (double)FPS, G.score, max_phase, max_objects);
    return 0;
}
