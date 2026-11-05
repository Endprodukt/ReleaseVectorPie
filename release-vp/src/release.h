/* release.h — shared definitions of the VectorPie port of Release.
 *
 * Ported from Malban's Vectrex version (public domain, with the permission
 * of Gimogames). Game logic keeps the Vectrex units: SCALE is the radial
 * distance (unsigned byte), ANGLE is in doubled degrees (0..719, spawns up
 * to 762), one update = one Vectrex frame at 50 Hz. See docs/spec_core.md
 * and docs/spec_objects.md for the source of every number.
 */
#ifndef RELEASE_H
#define RELEASE_H

/* ---- phase records (phaseDefinitions.i) ------------------------------- */

#define ALLOW_STAR      0x01
#define ALLOW_X         0x02
#define ALLOW_HIDDEN_X  0x04
#define ALLOW_HUNTER    0x08
#define ALLOW_BOMBER    0x10
#define ALLOW_DRAGON    0x20
#define ALLOW_BONUS     0x40
#define ALLOW_SQUAD     0x80

typedef struct {
    int allow;
    int w_growth, speed, minor_reset, minor_inc, w_adder;  /* shield */
    int shot_delay, shot_addi;
    int x_delay, x_addi;
    int hx_delay;
    int hunter_addi;
    int bomber_delay, bomber_addi, bomber_start;
    int dr_scale_delay, dr_angle_addi, dr_angle_delay;
    int child_addi;
    int min_bomb;
    int bonus_delay, bonus_addi, bonus_time;
    int sp_reset, sp_inc, sp_min, sp_max_inc;              /* -1 = keep */
    int next_at;                                           /* 0 = final */
} phase_rec;

#define PHASE_COUNT 34
extern const phase_rec *const phase_list[PHASE_COUNT];

/* ---- vector shapes (data.i) --------------------------------------------
 * Each shape is a list of polylines: a point count n, then n (x, y) points,
 * ..., ended by 0. Coordinates in sprite units, relative to the object. */

extern const short *const shape_x[4];
extern const short *const shape_hunter[24];
extern const short *const shape_bomber[9];
extern const short shape_shot[];
extern const short *const shape_dragon[4];
extern const short shape_child[];
extern const short *const shape_starlet[11];
extern const short *const shape_bonus[17];
extern const short shape_icon_shield[];
extern const short shape_icon_faster[];
extern const short shape_icon_expand[];

/* polygon_<n>_div_correction (VRelease.asm:941-972), index 0 = 5 sides */
extern const unsigned char poly_correction[4][72];

#endif
