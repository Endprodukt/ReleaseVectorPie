/* service.c - the service menu (see service.h).
 *
 * The cabinet's other vector games' menu (Pinout's, after Vector Racer's and Time Pilot 3D's): up /
 * down (any stick, hat or the arrows) go through the rows; START 1 (or Enter) takes a row, left /
 * right then set it, START 1 lets it go again; left / right set a row without taking it first. A
 * button's row: START 1, then the button (or key) to add; DELETE gives it its standard again. Nothing
 * is taken over until SAVE. The gameplay, the controls and the sound each have a page; the sound's
 * rows let you hear what they set.
 */
#include "service.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

enum {
    R_GAMEPLAY, R_CONTROLS, R_SOUND, R_SAVE, R_CANCEL, R_BACK,
    R_STYLE, R_ATTRACT, R_RESET,
    R_BUTTON, R_BUTTON_LAST = R_BUTTON + SERVICE_BUTTONS - 1, R_MENUKEY,
    R_VOLUME, R_MUSIC, R_EFFECTS,
    ROWS
};

enum { PAGE_MAIN, PAGE_GAMEPLAY, PAGE_CONTROLS, PAGE_SOUND };
static const int main_rows[] = { R_GAMEPLAY, R_CONTROLS, R_SOUND, R_CANCEL, R_SAVE };
static const int gameplay_rows[] = { R_STYLE, R_ATTRACT, R_RESET, R_BACK };
static const int control_rows[] = { R_BUTTON, R_MENUKEY, R_BACK };
static const int sound_rows[] = { R_VOLUME, R_MUSIC, R_EFFECTS, R_BACK };
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define REPEAT_DELAY   24               /* a held direction: frames before it repeats, and between */
#define REPEAT_EVERY   4

static const char *const row_names[ROWS] = {
    "GAMEPLAY", "CONTROLS", "SOUND", "SAVE", "CANCEL", "BACK",
    "STYLE", "ATTRACT SOUND", "RESET HIGHSCORES",
    "SHIELD BUTTON", "MENU BUTTON",
    "MASTER VOLUME", "MUSIC VOLUME", "EFFECTS VOLUME"
};

static void turn_to(int page);

static struct {
    service_settings now;
    int   page, cursor, row;
    int   editing;
    vpb_layout lay;                     /* laying out a button (vpbind.h) */
    int   held_v, held_h;               /* frames a direction has been held */
    int   preview;                      /* frames until the effects' row sounds again */
    int   tab;
    unsigned frames;
} S;

static const int *page_rows(int *count)
{
    if (S.page == PAGE_GAMEPLAY) { *count = COUNT(gameplay_rows); return gameplay_rows; }
    if (S.page == PAGE_CONTROLS) { *count = COUNT(control_rows); return control_rows; }
    if (S.page == PAGE_SOUND) { *count = COUNT(sound_rows); return sound_rows; }
    *count = COUNT(main_rows);
    return main_rows;
}

/* What opens the menu: F2 and TAB always (F2 the cabinet's key for every game's menu), so that nobody
 * is locked out, and whatever MENU BUTTON has besides. */
static char s_menu_key[VPB_LENGTH] = "key_F2";

void service_menu_key(const char *controls)
{
    snprintf(s_menu_key, sizeof s_menu_key, "%s", controls && controls[0] ? controls : "key_F2");
}

static char s_version[32];
void service_version(const char *version)
{
    snprintf(s_version, sizeof s_version, "%s", version ? version : "");
}

void service_init(void) { vpb_reserve_key(SDL_SCANCODE_TAB); }

int service_hotkey(void)
{
    int tab = vpb_key_held(SDL_SCANCODE_F2) || vpb_key_held(SDL_SCANCODE_TAB) || vpb_held(s_menu_key), hit = tab && !S.tab;
    S.tab = tab;
    return hit;
}

void service_open(const service_settings *now)
{
    int tab = S.tab, i;
    memset(&S, 0, sizeof S);
    S.tab = tab;
    S.now = *now;
    S.now.reset_best = 0;
    for (i = 0; i < SERVICE_BUTTONS; i++) vpb_normalize(S.now.button[i], sizeof S.now.button[i]);
    if (!S.now.menu_key[0]) snprintf(S.now.menu_key, sizeof S.now.menu_key, "key_F2");
    S.row = main_rows[0];
}

const service_settings *service_get(void) { return &S.now; }

void service_show(int page, int row)
{
    int count;
    const int *rows;
    turn_to(page < 0 || page > PAGE_SOUND ? PAGE_MAIN : page);
    rows = page_rows(&count);
    S.cursor = row < 0 ? 0 : row >= count ? count - 1 : row;
    S.row = rows[S.cursor];
}

static void hear(void)
{
    service_hear(S.now.volume, S.now.music_volume, S.now.effects_volume);
}

/* A row that holds a number: where it is, its range, how much a step changes it. */
static int *row_number(int row, int *lo, int *hi, int *step)
{
    *lo = 0; *hi = 100; *step = 5;
    switch (row) {
    case R_VOLUME:  return &S.now.volume;
    case R_MUSIC:   *hi = 150; return &S.now.music_volume;
    case R_EFFECTS: *hi = 150; return &S.now.effects_volume;
    }
    return NULL;
}

static int *row_switch(int row)
{
    switch (row) {
    case R_STYLE:   return &S.now.original;
    case R_ATTRACT: return &S.now.attract_sound;
    case R_RESET:   return &S.now.reset_best;
    }
    return NULL;
}

static int is_button(int row) { return (row >= R_BUTTON && row <= R_BUTTON_LAST) || row == R_MENUKEY; }

/* A button's standard: the cabinet's yoke triggers and its first buttons, the PC's left Ctrl. */
const char *service_standard(int b)
{
    static const char *const standard[SERVICE_BUTTONS] = {
        "yoke_button1+yoke_button2+key_Left_Ctrl+joy1_button1" };
    return b >= 0 && b < SERVICE_BUTTONS ? standard[b] : "";
}

/* A row that lays out controls: what it has, and its standard. */
static char *laid_of(int row, const char **standard)
{
    if (row == R_MENUKEY) { if (standard) *standard = "key_F2"; return S.now.menu_key; }
    if (standard) *standard = service_standard(row - R_BUTTON);
    return S.now.button[row - R_BUTTON];
}

static void click(int ok) { service_click(ok); }

static void change(int row, int steps)
{
    int lo, hi, step, *number = row_number(row, &lo, &hi, &step), *onoff = row_switch(row);
    if (!steps) return;
    if (is_button(row)) {               /* none, or its standard */
        const char *standard;
        char *has = laid_of(row, &standard);
        vpb_layout_toggle(&S.lay, has, sizeof S.now.menu_key, standard);
        click(0);
        return;
    }
    if (onoff) {
        *onoff = !*onoff;
        click(0);
        return;
    }
    if (!number) return;
    *number += steps * step;
    if (*number < lo) *number = lo;
    if (*number > hi) *number = hi;
    hear();
}

/* The effects' and the master's rows sound an effect now and then; the music plays on as it was. */
static void sound_for_row(void)
{
    if (S.preview > 0) { S.preview--; return; }
    if (S.row == R_EFFECTS || S.row == R_VOLUME) { service_preview(); S.preview = 60; }
}

static void turn_to(int page)
{
    int count;
    vpb_layout_moved(&S.lay);
    S.page = page;
    S.cursor = 0;
    S.row = page_rows(&count)[0];
}

/* A direction held down: once, then again and again. */
static int repeated(int held)
{
    if (!held) return 0;
    if (held == 1) return 1;
    return held > REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_EVERY == 0;
}

static void capture(vp_input *in)
{
    const char *standard;
    char *has = laid_of(S.row, &standard);
    int got = vpb_layout_step(&S.lay, in, has, sizeof S.now.menu_key, standard);
    if (got == VPB_CHANGED) click(1);
    else if (got == VPB_LEFT) click(0);
}

int service_update(vp_input *in, int hotkey)
{
    int accept, left, right, up, down, count;
    const int *rows = page_rows(&count);

    S.frames++;
    sound_for_row();
    if (hotkey) return SERVICE_CANCELLED;
    if (S.lay.capturing) { capture(in); return SERVICE_OPEN; }

    /* the menu's own controls, whatever is laid out: any stick, hat, the arrow keys; START 1, Enter
     * and the keypad's Enter say yes */
    accept = vpb_menu_confirm(in);
    left = vpb_menu_way_x() < 0;
    right = vpb_menu_way_x() > 0;
    vpb_layout_filter(&S.lay, &left, &right);
    if (is_button(S.row) && !S.editing && (vpb_key_pressed(SDL_SCANCODE_DELETE) || vpb_key_pressed(SDL_SCANCODE_KP_PERIOD))) {
        const char *standard;
        char *has = laid_of(S.row, &standard);
        vpb_layout_standard(&S.lay, has, sizeof S.now.menu_key, standard);
        click(1);
        return SERVICE_OPEN;
    }
    up = vpb_menu_way_y() < 0;
    down = vpb_menu_way_y() > 0;
    S.held_h = left != right ? S.held_h + 1 : 0;
    S.held_v = up != down ? S.held_v + 1 : 0;
    if (repeated(S.held_h)) change(S.row, right ? 1 : -1);

    if (S.editing) {
        if (accept) { S.editing = 0; click(0); }
        return SERVICE_OPEN;
    }
    if (repeated(S.held_v)) {
        vpb_layout_moved(&S.lay);
        S.cursor += down ? 1 : -1;
        if (S.cursor < 0) S.cursor = 0;
        if (S.cursor >= count) S.cursor = count - 1;
        S.row = rows[S.cursor];
        S.preview = 0;
        click(0);
    }
    if (!accept) return SERVICE_OPEN;
    if (S.row == R_SAVE) return SERVICE_SAVED;
    if (S.row == R_CANCEL) return SERVICE_CANCELLED;
    click(1);
    if (S.row == R_GAMEPLAY) turn_to(PAGE_GAMEPLAY);
    else if (S.row == R_CONTROLS) turn_to(PAGE_CONTROLS);
    else if (S.row == R_SOUND) turn_to(PAGE_SOUND);
    else if (S.row == R_BACK) turn_to(PAGE_MAIN);
    else if (is_button(S.row)) vpb_layout_begin(&S.lay, laid_of(S.row, NULL));
    else if (row_switch(S.row)) change(S.row, 1);
    else S.editing = 1;
    return SERVICE_OPEN;
}

/* --- drawing (board units, y up) ---------------------------------------------- */

static vp_dvg *s_d;

static void text(const char *s, float x, float y, float h, int align, int r, int g, int b)
{
    vp_color(s_d, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    vp_text(s_d, s, x, y, h, align);
}

static void line(float x0, float y0, float x1, float y1)
{
    vp_moveto(s_d, (int)x0, (int)y0);
    vp_lineto(s_d, (int)x1, (int)y1);
}

static void diamond(float x, float y, float r)
{
    line(x - r, y, x, y + r); line(x, y + r, x + r, y); line(x + r, y, x, y - r); line(x, y - r, x - r, y);
}

/* a slider: the part set bright, the rest dim, a knob where they meet */
static void slider(float x, float y, float width, float part, int lit)
{
    float knob = x + width * part;
    int bright = lit ? 255 : 150;
    vp_color(s_d, 0, (uint8_t)bright, (uint8_t)bright);
    line(x, y - 40, x, y + 40);
    line(x, y, knob, y);
    vp_color(s_d, 40, 60, 110);
    line(knob, y, x + width, y);
    line(x + width, y - 40, x + width, y + 40);
    if (lit) vp_color(s_d, 255, 255, 0); else vp_color(s_d, 200, 200, 200);
    diamond(knob, y, 45);
}

#define ROW_H   120.0f                  /* the rows' lettering */
#define VALUE_X 2450.0f                 /* where a row's value begins */

/* a row's value, smaller when it would run off the right */
static void value_text(const char *s, float y, int r, int g, int b)
{
    float size = ROW_H, room = 3950.0f - VALUE_X;
    if (vp_text_width(s, size) > room) size = size * room / vp_text_width(s, size);
    text(s, VALUE_X, y, size, VP_TEXT_LEFT, r, g, b);
}

void service_draw(vp_dvg *d)
{
    int count, i;
    const int *rows = page_rows(&count);
    char value[64];
    s_d = d;
    vp_text_font(VP_FONT_ASTEROIDS);
    text(S.page == PAGE_GAMEPLAY ? "GAMEPLAY" : S.page == PAGE_CONTROLS ? "CONTROLS" : S.page == PAGE_SOUND ? "SOUND"
         : "SERVICE MENU", 2048, 3550, 200, VP_TEXT_CENTER, 255, 255, 0);
    if (s_version[0]) {                                     /* the version, small in the corner */
        char v[48];
        snprintf(v, sizeof v, "VERSION %s", s_version);
        text(v, 3950, 3900, 60, VP_TEXT_RIGHT, 60, 110, 255);
    }
    for (i = 0; i < count; i++) {
        int row = rows[i], here = row == S.row, set = here && (S.editing || S.lay.capturing);
        int lo, hi, step, *number = row_number(row, &lo, &hi, &step), *onoff = row_switch(row);
        /* a button's controls stand under its name, the whole width: they are long */
        float y = 3150.0f - (S.page == PAGE_CONTROLS ? 440.0f : 340.0f) * (float)i;
        int r = here ? 255 : 60, g = here ? 255 : 110, b = 255;
        if (row == R_SAVE || row == R_CANCEL) continue;         /* side by side, below */
        if (here) text(">", 180, y, ROW_H, VP_TEXT_RIGHT, 255, 255, 0);
        text(row_names[row], 240, y, ROW_H, VP_TEXT_LEFT, r, g, b);
        if (set) { r = 255; g = 255; b = 0; }
        if (row == R_GAMEPLAY || row == R_CONTROLS || row == R_SOUND) {
            value_text("OPEN", y, r, g, b);
        } else if (row == R_STYLE) {
            value_text(*onoff ? "ORIGINAL" : "VECTREX", y, r, g, b);
        } else if (number) {
            slider(VALUE_X, y + ROW_H / 2, 950, (float)(*number - lo) / (float)(hi - lo), here);
            snprintf(value, sizeof value, "%d", *number);
            text(value, 3950, y, ROW_H, VP_TEXT_RIGHT, r, g, b);
        } else if (onoff) {
            value_text(*onoff ? "YES" : "NO", y, r, g, b);
        } else if (S.lay.capturing && here && ((S.frames / 15) & 1)) {
            text("PRESS A BUTTON", 320, y - 190, 100, VP_TEXT_LEFT, 255, 255, 0);
        } else if (is_button(row)) {
            char all[256];
            float size = 100.0f, room = 3950.0f - 320.0f;
            vpb_spelled(laid_of(row, NULL), all, sizeof all);
            if (!all[0]) snprintf(all, sizeof all, "NONE");
            if (row == R_MENUKEY) strncat(all, strcmp(laid_of(row, NULL), "key_F2") ? " + F2 + TAB" : " + TAB", sizeof all - strlen(all) - 1);
            if (vp_text_width(all, size) > room) size = size * room / vp_text_width(all, size);
            text(all, 320, y - 190, size, VP_TEXT_LEFT, r, g, b);
        }
    }
    if (S.page == PAGE_CONTROLS) text("F2 AND TAB ALWAYS OPEN THIS MENU", 2048, 1250, 80, VP_TEXT_CENTER, 120, 120, 120);
    if (S.page == PAGE_MAIN) {
        if (S.row == R_CANCEL) text("CANCEL", 1400, 1500, 170, VP_TEXT_CENTER, 255, 255, 255);
        else text("CANCEL", 1400, 1520, 120, VP_TEXT_CENTER, 200, 60, 60);
        if (S.row == R_SAVE) text("SAVE", 2700, 1500, 170, VP_TEXT_CENTER, 255, 255, 255);
        else text("SAVE", 2700, 1520, 120, VP_TEXT_CENTER, 0, 200, 0);
        text("LEAVING THE MENU GOES BACK TO THE TITLE", 2048, 1150, 70, VP_TEXT_CENTER, 120, 120, 120);
    }
    {
        const char *said = vpb_layout_said(&S.lay);
        const char *one = "UP, DOWN - ROW   LEFT, RIGHT - SET", *two = "ENTER, START 1 - TAKE";
        if (said) text(said, 2048, 640, 80, VP_TEXT_CENTER, 255, 80, 80);
        if (S.editing) { one = "LEFT, RIGHT - SET"; two = "ENTER, START 1 - DONE"; }
        else if (S.lay.capturing || is_button(S.row)) vpb_layout_help(&S.lay, &one, &two);
        text(one, 2048, 480, 70, VP_TEXT_CENTER, 0, 150, 0);
        text(two, 2048, 340, 70, VP_TEXT_CENTER, 0, 150, 0);
    }
}
