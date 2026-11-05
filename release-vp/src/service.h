/* service.h - the service menu (F2 or TAB): the gameplay, the controls and the sound, changed on the
 * machine, as in the cabinet's other vector games (after Pinout's and Vector Racer's). What it saves
 * goes to save/settings.ini; leaving it (SAVE or CANCEL) goes back to the title. */
#ifndef RELEASE_SERVICE_H
#define RELEASE_SERVICE_H

#include <vectorpie/vp.h>

#include "vpbind.h"

/* The buttons a player can lay out: the one button. */
enum { SB_SHIELD, SERVICE_BUTTONS };

/* What it sets. */
typedef struct service_settings {
    /* the gameplay */
    int  original;                      /* yes: the Android original's gameplay; no: the Vectrex version's */
    int  attract_sound;                 /* yes: music and effects on the attract pages and in the demo */
    int  reset_best;                    /* yes: SAVE resets the high scores to the standard table */
    /* the controls */
    char button[SERVICE_BUTTONS][VPB_LENGTH];   /* vpbind.h's controls, '+' between several */
    char menu_key[VPB_LENGTH];          /* what opens this menu besides F2 and TAB */
    /* the sound: percent, 100 as the game was mixed */
    int  volume, music_volume, effects_volume;
} service_settings;

enum { SERVICE_OPEN = 0, SERVICE_SAVED, SERVICE_CANCELLED };

/* Once at the start: TAB kept from laying out. */
void service_init(void);

/* A button's standard (SB_ index): the cabinet's yoke and buttons, the PC's keys. */
const char *service_standard(int b);

/* Once a game step, menu open or not: 1 when F2, TAB (or MENU BUTTON's) has just been pressed.
 * vpb_update must have run this step. */
int service_hotkey(void);

/* What opens the menu besides F2 and TAB ("" or "key_F2": nothing else). */
void service_menu_key(const char *controls);

/* The game's version (game.ini), shown in the menu's corner. */
void service_version(const char *version);

void service_open(const service_settings *now);

/* One step; `hotkey` service_hotkey's answer (the key again cancels). */
int service_update(vp_input *in, int hotkey);

/* The settings as they stand in the menu (to take over on SERVICE_SAVED). */
const service_settings *service_get(void);

void service_draw(vp_dvg *d);

/* For testing: turn straight to a page, 0 the main one .. 3, on its `row`th row. */
void service_show(int page, int row);

/* The game's side (main.c): hear the volumes being set, a click, a sample effect. */
void service_hear(int volume, int music_volume, int effects_volume);
void service_click(int ok);
void service_preview(void);

#endif /* RELEASE_SERVICE_H */
