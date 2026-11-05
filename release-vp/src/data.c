/* data.c — tables and vector shapes, transcribed from the Vectrex source
 * (phaseDefinitions.i, data.i, VRelease.asm). Values as assembled. */
#include "release.h"

/* ---- phases ------------------------------------------------------------ */
/*        allow  wGr spd mRst mInc wAdd  shD shA  xD xA  hxD huA  boD boA boS  drSD drA drAD  dcA  minB  bnD bnA bnT  spR spI spM spMx  next */
static const phase_rec phase0_x      = {0x47, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30, 50, 1,15,10,  20};
static const phase_rec phase0_hx     = {0x4B, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30, 50, 1,15,10,  20};
static const phase_rec hunter_raid_1 = {0x08, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30, 10, 1,15,10,  10};
static const phase_rec bomber_raid_1 = {0x14, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30,  5, 1,15,10,  20};
static const phase_rec dragon_raid_1 = {0x20, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30, 10, 1,15,10,  10};
static const phase_rec phase1        = {0xFF, 4,2,2,0,1, 1,1, 1,1, 3, 1, 1,1,200, 8,2,1, 1, 50, 0,2,30, 50, 1,15,10,  50};
static const phase_rec phase2        = {0xFF, 4,2,3,1,1, 1,1, 1,1, 2, 1, 1,1, 20, 5,2,0, 1,200, 0,2,30, 40, 1,15, 9,  50};
static const phase_rec phase3        = {0xFF, 4,2,2,1,1, 1,1, 1,1, 1, 1, 1,1,200, 5,4,0, 1, 50, 0,2,30, 35, 1,15, 8,  50};
static const phase_rec phase4        = {0xFF, 3,3,20,0,2,1,1, 1,1, 1, 1, 1,1, 25, 5,4,0, 1, 50, 0,2,30, 30, 1,15, 7, 100};
static const phase_rec phase5        = {0xFF, 3,3,2,1,2, 1,1, 1,1, 1, 1, 1,1, 25, 5,4,0, 1, 50, 0,2,30, 25, 1,25, 1, 100};
static const phase_rec squad_raid_1  = {0x80, 4,2,2,0,1, 1,1, 1,1, 1, 1, 1,1, 25, 5,4,0, 1, 50, 0,2,30, 25, 1,25, 1,  24};
static const phase_rec phase6        = {0xFF, 3,3,1,1,2, 3,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 30, 1,25, 5,  50};
static const phase_rec squad_raid_2  = {0x80, 3,3,1,1,2, 3,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 25, 1,25, 1,  40};
static const phase_rec phase7        = {0xFF, 3,3,0,1,2, 1,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, -1,-1,20,-1, 100};
static const phase_rec phase8        = {0xFF, 3,3,0,1,3, 0,2, 0,1, 1, 2, 0,2, 40, 1,6,0, 2, 10, 0,3,15, 50, 1,15,20,   0};
static const phase_rec hunter_raid_2 = {0x08, 3,3,1,1,2, 3,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 10, 1,15,10,  10};
static const phase_rec bomber_raid_2 = {0x14, 3,3,0,1,2, 1,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 10, 1,15,10,  20};
static const phase_rec dragon_raid_2 = {0x20, 3,3,2,1,2, 0,2, 0,1, 1, 2, 0,2, 40, 1,6,0, 2, 10, 0,3,15, 10, 1,15,10,  10};
static const phase_rec squad_raid_3  = {0x80, 3,3,0,1,2, 1,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 25, 1,25, 1,  40};
static const phase_rec squad_raid_2a = {0x80, 3,3,20,0,2,3,1, 1,1, 1, 2, 0,1, 25, 4,4,0, 1, 25, 0,3,30, 25, 1,25, 1,  40};

const phase_rec *const phase_list[PHASE_COUNT] = {
    &phase0_x, &phase0_hx, &hunter_raid_1, &phase1, &squad_raid_1, &phase1,
    &bomber_raid_1, &phase2, &squad_raid_1, &phase2, &dragon_raid_1, &phase3,
    &squad_raid_2a, &phase3, &hunter_raid_2, &phase4, &squad_raid_2, &phase4,
    &bomber_raid_2, &phase5, &squad_raid_2, &phase5, &dragon_raid_2, &phase6,
    &squad_raid_3, &phase6, &bomber_raid_2, &dragon_raid_2, &phase7,
    &squad_raid_3, &phase7, &hunter_raid_2, &hunter_raid_2, &phase8,
};

/* ---- shield hit correction tables --------------------------------------- */

const unsigned char poly_correction[4][72] = {
    { 0,0,1,3,3,4,5,7,7,8, 8,9,9,9,9,9,10,10,10,11, 11,11,11,11,12,12,12,12,12,12,
      12,12,12,13,13,13,13,13,12,12, 12,12,12,12,12,12,12,11,11,11, 11,11,10,10,10,9,9,9,9,9,
      9,8,8,7,7,5,4,3,3,1, 0,0 },
    { 0,1,1,1,2,2,2,3,3,3, 5,5,5,5,7,7,8,8,9,9, 9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,
      9,9,8,8,7,7,5,5,5,5, 3,3,3,2,2,2,1,1,1,0 },
    { 0,0,1,1,1,2,2,3,3,3, 3,4,4,4,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5,
      4,5,5,5,5,5,5,5,4,4, 3,3,3,3,3,2,2,1,1,1, 0,0 },
    { 0,1,1,1,2,2,2,3,3,3, 3,4,4,4,4,4,4,4,4,4, 5,5,5,5,4,4,4,4,4,4,
      4,4,4,3,3,3,3,3,2,2, 2,1,1,1,0 },
};

/* ---- shapes ------------------------------------------------------------- */

static const short x0[] = { 2, -63,0, 63,0,  2, 0,63, 0,-63, 0 };
static const short x1[] = { 2, -50,0, 50,0,  2, 0,50, 0,-50, 0 };
static const short x2[] = { 2, -38,0, 38,0,  2, 0,38, 0,-38, 0 };
const short *const shape_x[4] = { x0, x1, x2, x1 };

#define HUNTER(a,b,c,d,e,f) { 4, a,b, c,d, e,f, a,b, 0 }
static const short h0[]  = HUNTER(-26,62,   0,-62,  26,62);
static const short h1[]  = HUNTER(-41,53,  16,-60,   9,67);
static const short h2[]  = HUNTER(-54,41,  31,-54,  -8,67);
static const short h3[]  = HUNTER(-62,25,  44,-44, -25,62);
static const short h4[]  = HUNTER(-67,8,   54,-31, -41,54);
static const short h5[]  = HUNTER(-67,-9,  60,-16, -53,41);
static const short h6[]  = HUNTER(-62,-26, 62,0,   -62,26);
static const short h7[]  = HUNTER(-53,-41, 60,16,  -67,9);
static const short h8[]  = HUNTER(-41,-54, 54,31,  -67,-8);
static const short h9[]  = HUNTER(-25,-62, 44,44,  -62,-25);
static const short h10[] = HUNTER(-8,-67,  31,54,  -54,-41);
static const short h11[] = HUNTER(9,-67,   16,60,  -41,-53);
static const short h12[] = HUNTER(26,-62,   0,62,  -26,-62);
static const short h13[] = HUNTER(41,-53, -16,60,   -9,-67);
static const short h14[] = HUNTER(54,-41, -31,54,    8,-67);
static const short h15[] = HUNTER(62,-25, -44,44,   25,-62);
static const short h16[] = HUNTER(67,-8,  -54,31,   41,-54);
static const short h17[] = HUNTER(67,9,   -60,16,   53,-41);
static const short h18[] = HUNTER(62,26,  -62,0,    62,-26);
static const short h19[] = HUNTER(53,41,  -60,-16,  67,-9);
static const short h20[] = HUNTER(41,54,  -54,-31,  67,8);
static const short h21[] = HUNTER(25,62,  -44,-44,  62,25);
static const short h22[] = HUNTER(8,67,   -31,-54,  54,41);
static const short h23[] = HUNTER(-9,67,  -16,-60,  41,53);
const short *const shape_hunter[24] = {
    h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14,h15,h16,h17,h18,h19,h20,h21,h22,h23
};

static const short b0[] = { 9, 0,69, 51,51, 69,0, 51,-51, 0,-69, -51,-51, -69,0, -51,51, 0,69, 0 };
static const short b1[] = { 9, -5,69, 46,56, 69,5, 56,-46, 5,-69, -46,-56, -69,-5, -56,46, -5,69, 0 };
static const short b2[] = { 9, -11,69, 41,59, 69,11, 59,-41, 11,-69, -41,-59, -69,-11, -59,41, -11,69, 0 };
static const short b3[] = { 9, -18,68, 36,63, 68,18, 63,-36, 18,-68, -36,-63, -68,-18, -63,36, -18,68, 0 };
static const short b4[] = { 9, -23,65, 30,66, 65,23, 66,-30, 23,-65, -30,-66, -65,-23, -66,30, -23,65, 0 };
static const short b5[] = { 9, -29,63, 24,69, 63,29, 69,-24, 29,-63, -24,-69, -63,-29, -69,24, -29,63, 0 };
static const short b6[] = { 9, -34,60, 18,70, 60,34, 70,-18, 34,-60, -18,-70, -60,-34, -70,18, -34,60, 0 };
static const short b7[] = { 9, -39,57, 12,72, 57,39, 72,-12, 39,-57, -12,-72, -57,-39, -72,12, -39,57, 0 };
static const short b8[] = { 9, -44,53, 6,73, 53,44, 73,-6, 44,-53, -6,-73, -53,-44, -73,6, -44,53, 0 };
const short *const shape_bomber[9] = { b0,b1,b2,b3,b4,b5,b6,b7,b8 };

const short shape_shot[] = { 5, 0,0, 40,40, 80,0, 40,-40, 0,0, 0 };

static const short d0[] = { 5, -74,0, 0,74, 74,0, 0,-74, -74,0, 0 };
static const short d1[] = { 5, -70,0, 0,70, 70,0, 0,-70, -70,0, 0 };
static const short d2[] = { 5, -66,0, 0,66, 66,0, 0,-66, -66,0, 0 };
const short *const shape_dragon[4] = { d0, d1, d2, d1 };

const short shape_child[] = { 2, -18,-18, 18,18,  2, -18,18, 18,-18, 0 };

static const short s0[]  = { 11, -20,36, -76,34, -41,-8, -56,-63, -3,-42, 42,-73, 39,-17, 82,16, 28,30, 8,84, -20,36, 0 };
static const short s1[]  = { 11, -24,33, -79,25, -40,-13, -48,-69, 2,-42, 50,-68, 41,-12, 80,25, 24,33, -2,84, -24,33, 0 };
static const short s2[]  = { 11, -28,31, -82,16, -38,-17, -40,-74, 7,-42, 57,-62, 42,-8, 76,34, 20,36, -11,84, -28,31, 0 };
static const short s3[]  = { 11, -31,27, -83,6, -36,-21, -32,-78, 11,-41, 64,-55, 42,-3, 72,43, 16,38, -21,82, -31,27, 0 };
static const short s4[]  = { 11, -34,23, -83,-3, -33,-25, -22,-81, 16,-39, 70,-47, 42,2, 67,51, 12,39, -30,79, -34,23, 0 };
static const short s5[]  = { 11, -36,19, -82,-12, -30,-29, -13,-83, 20,-37, 75,-39, 42,7, 60,58, 7,40, -39,75, -36,19, 0 };
static const short s6[]  = { 11, -38,15, -80,-22, -27,-32, -3,-84, 24,-34, 79,-30, 41,12, 53,64, 3,41, -47,70, -38,15, 0 };
static const short s7[]  = { 11, -40,11, -77,-31, -23,-35, 6,-84, 28,-31, 82,-21, 39,16, 46,70, -2,41, -55,64, -40,11, 0 };
static const short s8[]  = { 11, -41,6, -73,-39, -19,-37, 16,-83, 31,-28, 83,-11, 37,21, 37,75, -7,40, -62,58, -41,6, 0 };
static const short s9[]  = { 11, -41,1, -68,-48, -14,-39, 25,-80, 34,-24, 84,-2, 35,25, 29,78, -11,39, -68,50, -41,1, 0 };
static const short s10[] = { 11, -41,-3, -62,-55, -10,-41, 34,-77, 37,-20, 84,8, 32,28, 20,81, -16,38, -73,42, -41,-3, 0 };
const short *const shape_starlet[11] = { s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10 };

#define QMARK(a,b,c,d,e,f,g,h,i,j,k,l,m,n) { 5, a,b, c,d, e,f, g,h, i,j,  2, k,l, m,n, 0 }
static const short q0[]  = QMARK(-29,36, 0,60, 29,36, 0,3, 0,-36,      0,-61, 4,-61);
static const short q1[]  = QMARK(-40,24, -20,56, 13,45, 0,3, 12,-34,   21,-57, 26,-54);
static const short q2[]  = QMARK(-47,7, -40,44, -2,47, -2,2, 24,-27,   40,-44, 44,-40);
static const short q3[]  = QMARK(-46,-8, -54,26, -19,43, -3,1, 33,-15, 54,-26, 57,-20);
static const short q4[]  = QMARK(-40,-24, -60,4, -34,32, -3,0, 36,-2,  61,-4, 61,0);
static const short q5[]  = QMARK(-28,-38, -57,-15, -44,17, -3,0, 36,9, 59,15, 57,22);
static const short q6[]  = QMARK(-11,-46, -48,-36, -47,0, -2,-2, 29,22, 49,36, 44,40);
static const short q7[]  = QMARK(4,-47, -31,-50, -44,-15, -1,-3, 19,31, 31,52, 26,54);
static const short q8[]  = QMARK(20,-42, -9,-59, -36,-31, 0,-3, 5,36,  10,60, 4,61);
static const short q9[]  = QMARK(35,-31, 9,-59, -21,-42, 0,-3, -5,36,  -10,60, -15,59);
static const short q10[] = QMARK(44,-15, 31,-50, -4,-47, 1,-3, -19,31, -31,52, -36,49);
static const short q11[] = QMARK(47,0, 48,-36, 11,-46, 2,-2, -29,22,   -49,36, -52,31);
static const short q12[] = QMARK(44,17, 57,-15, 28,-38, 3,0, -36,9,    -59,15, -60,9);
static const short q13[] = QMARK(34,32, 60,4, 40,-25, 3,0, -36,-2,     -61,-4, -60,-10);
static const short q14[] = QMARK(19,43, 54,26, 47,-9, 3,1, -33,-15,    -54,-26, -52,-32);
static const short q15[] = QMARK(2,47, 40,44, 47,7, 2,2, -24,-27,      -40,-44, -36,-49);
static const short q16[] = QMARK(-12,44, 20,56, 40,24, 0,3, -12,-34,   -21,-57, -15,-59);
const short *const shape_bonus[17] = {
    q0,q1,q2,q3,q4,q5,q6,q7,q8,q9,q10,q11,q12,q13,q14,q15,q16
};

const short shape_icon_shield[] = { 7, 0,0, -93,124, -93,251, 0,251, 93,251, 93,124, 0,0, 0 };
const short shape_icon_faster[] = { 4, 0,0, 76,51, 76,151, 0,202,  2, 0,101, -76,51,
                                    3, -76,151, 0,101, -127,101, 0 };
const short shape_icon_expand[] = { 4, 0,0, 63,64, 63,126, 0,190,  3, 95,95, 158,95, 127,126,
                                    2, 127,64, 158,95,  4, 95,222, 190,158, 190,32, 95,-32, 0 };
