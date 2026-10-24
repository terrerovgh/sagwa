// Sagwa Live-2.5D Hyperrealistic Feline Renderer for M5Stack CoreS3
// Uses the high-resolution Higgsfield Sagwa poses + Seedance greeting keyframes
// with a 50 FPS anatomical 2.5D deformer:
// - Saccadic feline eye fixation, sub-pixel iris/pupil shift, vertical slit-to-oval
//   pupil modulation, corneal parallax glint, and fur-sampled eyelid closure
// - 2.5D lower-jaw drop, whisker-pad puff, nose sniffing (olfateo), and multi-layer
//   anatomical mouth cleft with tongue & upper canine glints
// - Articulated left & right front paws (rhythmic "making biscuits" kneading,
//   wrist-pivoted paw wave & face-grooming nod, stalking stride, and bow-stretch flex)
// - 2-harmonic kinematic spine tail deformer (root-locked s^1.5 whip wave + tip flicks)
// - Posture graph with an idle scheduler (sleeps most of the time), agent states
//   shown through spring-driven face, ear, tail and breathing layers

#include "muse_pixel.h"
#include "sagwa_legacy.h"
#include "sagwa_frames.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_random.h"
#define SAGWA_BSS_ATTR EXT_RAM_BSS_ATTR
#else
#define SAGWA_BSS_ATTR
#endif

#define NW SAGWA_NATIVE_W
#define NH SAGWA_NATIVE_H
#define PI_F 3.14159265f
#define TAU_F 6.28318530f

enum {
    RIG_IDLE = 0,
    RIG_LOOK,
    RIG_LISTEN,
    RIG_THINK,
    RIG_SPEAK,
    RIG_WORK,
    RIG_CELEBRATE,
    RIG_PURR,
    RIG_SLEEP,
    RIG_WAKE,
    RIG_STRETCH,
    RIG_TILT,
    RIG_COUNT
};

enum {
    ACT_NONE = 0,
    ACT_PAW_WAVE = 1,
    ACT_PROWL = 2,
    ACT_STRETCH = 3,
};

typedef struct {
    float hx, hy, hr;          /* Head center & radius */
    float nx, ny;              /* Neck pivot */
    float elx, ely, erx, ery;  /* Left & right ear centers */
    float ear_r;
    float lx, ly, rx, ry;      /* Left & right open eye centers (0 if closed) */
    float eye_rx, eye_ry;      /* Tight iris aperture radii (~2.9 x 2.4 px) */
    float mx, my, mr;          /* Nose/muzzle cleft anchor & jaw radius */
    float cx, cy, cr;          /* Chest / ribcage breathing zone */
    float tbx, tby;            /* Tail spine root anchor on body */
    float ttx, tty;            /* Tail spine tip anchor */
    float tr_w;                /* Tail corridor half-width radius */
    float plx, ply, plr;       /* Left front paw center & radius */
    float prx, pry, prr;       /* Right front paw center & radius */
    float ax, ay, ar;          /* Special action zone (waving paw / prowl / stretch) */
    int action_type;
    int posture_group;         /* 0=standing, 1=sitting-front, 2=loaf/sleep */
} pose_rig_t;

static const pose_rig_t S_RIGS[RIG_COUNT] = {
    [RIG_IDLE] = {
        .hx = 95.0f, .hy = 38.0f, .hr = 24.0f, .nx = 89.0f, .ny = 54.0f,
        .elx = 81.0f, .ely = 21.0f, .erx = 110.0f, .ery = 19.0f, .ear_r = 12.0f,
        .lx = 88.2f, .ly = 44.1f, .rx = 104.7f, .ry = 41.6f, .eye_rx = 3.6f, .eye_ry = 3.0f,
        .mx = 96.5f, .my = 52.0f, .mr = 7.5f,
        .cx = 68.0f, .cy = 76.0f, .cr = 30.0f,
        .tbx = 26.0f, .tby = 54.0f, .ttx = 22.0f, .tty = 10.0f, .tr_w = 19.0f,
        .plx = 83.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 101.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 0
    },
    [RIG_LOOK] = {
        .hx = 52.0f, .hy = 34.0f, .hr = 24.0f, .nx = 56.0f, .ny = 52.0f,
        .elx = 35.0f, .ely = 21.0f, .erx = 50.0f, .ery = 14.0f, .ear_r = 12.0f,
        .lx = 50.6f, .ly = 37.2f, .rx = 61.6f, .ry = 31.7f, .eye_rx = 3.4f, .eye_ry = 2.9f,
        .mx = 57.5f, .my = 39.0f, .mr = 7.0f,
        .cx = 68.0f, .cy = 76.0f, .cr = 30.0f,
        .tbx = 88.0f, .tby = 54.0f, .ttx = 96.0f, .tty = 10.0f, .tr_w = 20.0f,
        .plx = 50.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 83.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 0
    },
    [RIG_LISTEN] = {
        .hx = 55.0f, .hy = 34.0f, .hr = 24.0f, .nx = 55.0f, .ny = 53.0f,
        .elx = 41.0f, .ely = 16.0f, .erx = 70.0f, .ery = 16.0f, .ear_r = 12.5f,
        .lx = 46.3f, .ly = 35.5f, .rx = 64.0f, .ry = 36.7f, .eye_rx = 3.7f, .eye_ry = 3.1f,
        .mx = 55.2f, .my = 45.2f, .mr = 7.5f,
        .cx = 56.0f, .cy = 82.0f, .cr = 28.0f,
        .tbx = 80.0f, .tby = 78.0f, .ttx = 99.0f, .tty = 28.0f, .tr_w = 19.0f,
        .plx = 50.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 69.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 1
    },
    [RIG_THINK] = {
        .hx = 64.0f, .hy = 39.0f, .hr = 24.0f, .nx = 64.0f, .ny = 56.0f,
        .elx = 43.0f, .ely = 30.0f, .erx = 68.0f, .ery = 16.0f, .ear_r = 12.0f,
        .lx = 59.4f, .ly = 46.4f, .rx = 73.3f, .ry = 36.3f, .eye_rx = 3.6f, .eye_ry = 3.0f,
        .mx = 68.5f, .my = 47.8f, .mr = 7.2f,
        .cx = 62.0f, .cy = 84.0f, .cr = 28.0f,
        .tbx = 83.0f, .tby = 114.0f, .ttx = 97.0f, .tty = 88.0f, .tr_w = 17.0f,
        .plx = 53.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 73.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 1
    },
    [RIG_SPEAK] = {
        .hx = 89.0f, .hy = 34.0f, .hr = 24.0f, .nx = 86.0f, .ny = 52.0f,
        .elx = 77.0f, .ely = 14.0f, .erx = 106.0f, .ery = 18.0f, .ear_r = 12.0f,
        .lx = 81.4f, .ly = 32.5f, .rx = 97.8f, .ry = 36.4f, .eye_rx = 3.6f, .eye_ry = 3.0f,
        .mx = 88.0f, .my = 46.5f, .mr = 9.0f,
        .cx = 68.0f, .cy = 76.0f, .cr = 30.0f,
        .tbx = 30.0f, .tby = 54.0f, .ttx = 34.0f, .tty = 18.0f, .tr_w = 19.0f,
        .plx = 76.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 95.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 0
    },
    [RIG_WORK] = {
        .hx = 105.0f, .hy = 84.0f, .hr = 23.0f, .nx = 88.0f, .ny = 80.0f,
        .elx = 96.0f, .ely = 66.0f, .erx = 116.0f, .ery = 64.0f, .ear_r = 11.5f,
        .lx = 104.7f, .ly = 90.5f, .rx = 116.0f, .ry = 89.4f, .eye_rx = 3.4f, .eye_ry = 2.9f,
        .mx = 110.5f, .my = 98.0f, .mr = 7.0f,
        .cx = 58.0f, .cy = 76.0f, .cr = 32.0f,
        .tbx = 20.0f, .tby = 52.0f, .ttx = 28.0f, .tty = 10.0f, .tr_w = 19.0f,
        .plx = 76.0f, .ply = 114.0f, .plr = 15.0f,
        .prx = 98.0f, .pry = 108.0f, .prr = 14.0f,
        .ax = 68.0f, .ay = 86.0f, .ar = 38.0f, .action_type = ACT_PROWL, .posture_group = 0
    },
    [RIG_CELEBRATE] = {
        .hx = 53.0f, .hy = 36.0f, .hr = 22.0f, .nx = 58.0f, .ny = 52.0f,
        .elx = 32.0f, .ely = 28.0f, .erx = 58.0f, .ery = 14.0f, .ear_r = 11.5f,
        .lx = 45.4f, .ly = 41.0f, .rx = 60.8f, .ry = 32.3f, .eye_rx = 3.6f, .eye_ry = 3.0f,
        .mx = 56.2f, .my = 44.5f, .mr = 8.5f,
        .cx = 60.0f, .cy = 84.0f, .cr = 27.0f,
        .tbx = 84.0f, .tby = 96.0f, .ttx = 106.0f, .tty = 62.0f, .tr_w = 18.0f,
        .plx = 52.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 83.0f, .pry = 35.0f, .prr = 22.0f,
        .ax = 83.0f, .ay = 35.0f, .ar = 24.0f, .action_type = ACT_PAW_WAVE, .posture_group = 1
    },
    [RIG_PURR] = {
        .hx = 66.0f, .hy = 38.0f, .hr = 24.0f, .nx = 66.0f, .ny = 56.0f,
        .elx = 46.0f, .ely = 24.0f, .erx = 76.0f, .ery = 16.0f, .ear_r = 12.0f,
        .lx = 0.0f, .ly = 0.0f, .rx = 0.0f, .ry = 0.0f, .eye_rx = 0.0f, .eye_ry = 0.0f,
        .mx = 66.9f, .my = 44.5f, .mr = 7.0f,
        .cx = 62.0f, .cy = 84.0f, .cr = 29.0f,
        .tbx = 84.0f, .tby = 114.0f, .ttx = 98.0f, .tty = 88.0f, .tr_w = 17.0f,
        .plx = 54.0f, .ply = 113.0f, .plr = 14.0f,
        .prx = 74.0f, .pry = 113.0f, .prr = 14.0f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 1
    },
    [RIG_SLEEP] = {
        .hx = 94.0f, .hy = 94.0f, .hr = 23.0f, .nx = 82.0f, .ny = 88.0f,
        .elx = 86.0f, .ely = 72.0f, .erx = 114.0f, .ery = 88.0f, .ear_r = 11.0f,
        .lx = 0.0f, .ly = 0.0f, .rx = 0.0f, .ry = 0.0f, .eye_rx = 0.0f, .eye_ry = 0.0f,
        .mx = 95.5f, .my = 104.0f, .mr = 5.5f,
        .cx = 56.0f, .cy = 80.0f, .cr = 38.0f,
        .tbx = 24.0f, .tby = 112.0f, .ttx = 78.0f, .tty = 118.0f, .tr_w = 14.0f,
        .plx = 0.0f, .ply = 0.0f, .plr = 0.0f,
        .prx = 0.0f, .pry = 0.0f, .prr = 0.0f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 2
    },
    [RIG_WAKE] = {
        .hx = 70.0f, .hy = 62.0f, .hr = 24.0f, .nx = 66.0f, .ny = 78.0f,
        .elx = 48.0f, .ely = 40.0f, .erx = 86.0f, .ery = 36.0f, .ear_r = 12.0f,
        .lx = 60.0f, .ly = 67.6f, .rx = 81.0f, .ry = 63.2f, .eye_rx = 3.6f, .eye_ry = 3.0f,
        .mx = 71.8f, .my = 76.0f, .mr = 7.0f,
        .cx = 48.0f, .cy = 74.0f, .cr = 34.0f,
        .tbx = 18.0f, .tby = 94.0f, .ttx = 42.0f, .tty = 108.0f, .tr_w = 16.0f,
        .plx = 80.0f, .ply = 114.0f, .plr = 14.5f,
        .prx = 116.0f, .pry = 114.0f, .prr = 14.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 2
    },
    [RIG_STRETCH] = {
        .hx = 54.0f, .hy = 84.0f, .hr = 22.0f, .nx = 66.0f, .ny = 82.0f,
        .elx = 38.0f, .ely = 66.0f, .erx = 72.0f, .ery = 68.0f, .ear_r = 11.5f,
        .lx = 0.0f, .ly = 0.0f, .rx = 0.0f, .ry = 0.0f, .eye_rx = 0.0f, .eye_ry = 0.0f,
        .mx = 54.0f, .my = 93.0f, .mr = 6.5f,
        .cx = 88.0f, .cy = 70.0f, .cr = 32.0f,
        .tbx = 104.0f, .tby = 42.0f, .ttx = 90.0f, .tty = 10.0f, .tr_w = 20.0f,
        .plx = 26.0f, .ply = 117.0f, .plr = 15.5f,
        .prx = 50.0f, .pry = 114.0f, .prr = 15.5f,
        .ax = 54.0f, .ay = 96.0f, .ar = 42.0f, .action_type = ACT_STRETCH, .posture_group = 0
    },
    [RIG_TILT] = {
        .hx = 78.0f, .hy = 38.0f, .hr = 24.0f, .nx = 70.0f, .ny = 54.0f,
        .elx = 66.0f, .ely = 14.0f, .erx = 98.0f, .ery = 26.0f, .ear_r = 12.0f,
        .lx = 70.0f, .ly = 35.6f, .rx = 86.6f, .ry = 42.9f, .eye_rx = 3.7f, .eye_ry = 3.1f,
        .mx = 75.5f, .my = 46.2f, .mr = 7.5f,
        .cx = 64.0f, .cy = 80.0f, .cr = 28.0f,
        .tbx = 38.0f, .tby = 54.0f, .ttx = 34.0f, .tty = 20.0f, .tr_w = 18.0f,
        .plx = 54.0f, .ply = 113.0f, .plr = 13.5f,
        .prx = 70.0f, .pry = 113.0f, .prr = 13.5f,
        .ax = 0, .ay = 0, .ar = 0, .action_type = ACT_NONE, .posture_group = 1
    },
};

/* Decompressed frame cache (3 slots so greet interpolation + transitions never thrash) */
static SAGWA_BSS_ATTR uint16_t s_cache[3][NW * NH];
static int s_cache_idx[3] = { -1, -1, -1 };
static int s_cache_lru = 0;

static SAGWA_BSS_ATTR uint16_t s_work_a[NW * NH];
static SAGWA_BSS_ATTR uint16_t s_work_b[NW * NH];
static SAGWA_BSS_ATTR uint16_t s_out_fb[NW * NH];
static int s_target_px = NW;

static const uint16_t *get_raw_frame(int frame_id)
{
    if (frame_id < 0) frame_id = 0;
    if (frame_id >= SAGWA_TOTAL_FRAMES) frame_id = SAGWA_TOTAL_FRAMES - 1;
    for (int k = 0; k < 3; k++) {
        if (s_cache_idx[k] == frame_id) {
            return s_cache[k];
        }
    }
    int slot = s_cache_lru;
    s_cache_lru = (s_cache_lru + 1) % 3;
    s_cache_idx[slot] = frame_id;

    uint16_t *dst = s_cache[slot];
    uint32_t pos = SAGWA_RLE_OFFSETS[frame_id];
    uint32_t end = SAGWA_RLE_OFFSETS[frame_id + 1];
    int out_i = 0;
    while (pos < end && out_i < NW * NH) {
        uint16_t tok = SAGWA_RLE_DATA[pos++];
        if (tok & 0x8000) {
            int run = tok & 0x7fff;
            if (out_i + run > NW * NH) run = NW * NH - out_i;
            memset(&dst[out_i], 0, (size_t)run * sizeof(uint16_t));
            out_i += run;
        } else {
            int lit = tok;
            if (out_i + lit > NW * NH) lit = NW * NH - out_i;
            memcpy(&dst[out_i], &SAGWA_RLE_DATA[pos], (size_t)lit * sizeof(uint16_t));
            pos += lit;
            out_i += lit;
        }
    }
    if (out_i < NW * NH) {
        memset(&dst[out_i], 0, (size_t)(NW * NH - out_i) * sizeof(uint16_t));
    }
    return dst;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float smoothstepf(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Fast RGB565 blend: w in 0..256 */
static inline uint16_t blend565(uint16_t a, uint16_t b, int w)
{
    if (w <= 0) return a;
    if (w >= 256) return b;
    if (a == b) return a;
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = (ar * (256 - w) + br * w + 128) >> 8;
    int g = (ag * (256 - w) + bg * w + 128) >> 8;
    int bl = (ab * (256 - w) + bb * w + 128) >> 8;
    uint16_t res = (uint16_t)((r << 11) | (g << 5) | bl);
    return (res == 0 && (a != 0 || b != 0) && w > 32 && w < 224) ? 0x0821 : res;
}

/* Sub-pixel Q8 bilinear sample from a 136x136 RGB565 buffer */
static inline uint16_t sample_bilinear_q8(const uint16_t *src, int fx_q8, int fy_q8)
{
    int x0 = fx_q8 >> 8;
    int y0 = fy_q8 >> 8;
    if ((unsigned)x0 >= (unsigned)(NW - 1) || (unsigned)y0 >= (unsigned)(NH - 1)) {
        if (x0 >= 0 && x0 < NW && y0 >= 0 && y0 < NH) {
            return src[y0 * NW + x0];
        }
        return 0;
    }
    int wx = fx_q8 & 0xff;
    int wy = fy_q8 & 0xff;
    const uint16_t *row0 = &src[y0 * NW + x0];
    const uint16_t *row1 = row0 + NW;
    uint16_t c00 = row0[0], c10 = row0[1];
    uint16_t c01 = row1[0], c11 = row1[1];
    if ((c00 | c10 | c01 | c11) == 0) {
        return 0;
    }
    if (c00 == c10 && c00 == c01 && c00 == c11) {
        return c00;
    }
    uint16_t top = blend565(c00, c10, wx);
    uint16_t bot = blend565(c01, c11, wx);
    return blend565(top, bot, wy);
}

typedef struct {
    float breath;        /* -1..1 chest expansion */
    float head_rot;      /* radians */
    float head_dx, head_dy;
    float ear_l, ear_r;  /* independent ear swivel (-1..1) */
    float tail_phase;    /* primary travelling wave phase */
    float tail_amp;      /* tail sway amplitude in px */
    float tail_flick;    /* secondary tip flick offset */
    float blink;         /* 0..1 eyelid closure */
    float gaze_x, gaze_y;/* saccadic eye gaze (-1..1) */
    float pupil_dilate;  /* 0..1 vertical slit -> dilated oval */
    float mouth_open;    /* 0..1 lower jaw articulation */
    float sniff;         /* 0..1 rapid feline nose/whisker sniff */
    float knead_l;       /* 0..1 left front paw lift & toe spread */
    float knead_r;       /* 0..1 right front paw lift & toe spread */
    float action_phase;
    float action_amp;
    float body_squash_y; /* vertical crouch/stretch offset in px */
    float body_lead_x;   /* horizontal weight shift in px */
    float greet_pos;     /* RIG_IDLE only: Seedance frame position, 0 = still pose */
} anim_params_t;

/*
 * Prepare the unwarped 136x136 base buffer for a rig state:
 * - For RIG_IDLE, shows the Seedance standing sequence at ap->greet_pos (which
 *   natively contains real eye blinks in GREET_3..5 and head tilts in GREET_6..11);
 *   at 0 it is the still GREET_0 frame and gets the eye shader like any pose.
 * - For open-eye poses, applies Fur-Sampled Eyelid Closure (sampling the exact
 *   brow/cheek fur column directly above/below each eye pixel so outer cream fur
 *   and inner Siamese mask fur close seamlessly with zero flat oval artifacts),
 *   plus slit-to-oval pupil modulation and corneal catchlight glint.
 * - For vocalizing poses, renders a multi-layer anatomical feline mouth cleft
 *   (dark upper lip philtrum arch, tiny white upper canine glints, concave
 *   rose-pink tongue, and dark lower lip rim) while warp_pose_25d drops the real chin fur.
 */
static void prepare_base_pose(int rig_id, const anim_params_t *ap, uint16_t *dst)
{
    const uint16_t *raw_pose = NULL;
    bool is_seedance_idle = (rig_id == RIG_IDLE && ap->greet_pos > 0.05f);

    if (rig_id == RIG_IDLE && !is_seedance_idle) {
        raw_pose = get_raw_frame(SAGWA_FRAME_GREET_0);
        memcpy(dst, raw_pose, sizeof(uint16_t) * NW * NH);
    } else if (is_seedance_idle) {
        /* Blend the two Seedance frames either side of greet_pos */
        float pos = clampf(ap->greet_pos, 0.0f, (float)(SAGWA_GREET_FRAMES - 1));
        int f0 = (int)pos;
        int f1 = f0 + 1 < SAGWA_GREET_FRAMES ? f0 + 1 : f0;
        int w = (int)((pos - (float)f0) * 256.0f);
        const uint16_t *p0 = get_raw_frame(SAGWA_FRAME_GREET_0 + f0);
        const uint16_t *p1 = get_raw_frame(SAGWA_FRAME_GREET_0 + f1);
        for (int i = 0; i < NW * NH; i++) {
            uint16_t a = p0[i], b = p1[i];
            if (a == 0 || b == 0) {
                dst[i] = (w < 128) ? a : b;
            } else {
                dst[i] = blend565(a, b, w);
            }
        }
        raw_pose = dst;
    } else {
        raw_pose = get_raw_frame(SAGWA_FRAME_IDLE + rig_id);
        memcpy(dst, raw_pose, sizeof(uint16_t) * NW * NH);
    }

    const pose_rig_t *r = &S_RIGS[rig_id];

    /*
     * 1. Hyperrealistic Eye Shader (Open-Eye Poses):
     *    Skip synthetic eye painting on RIG_IDLE because GREET_0..11 already has
     *    native Seedance eyelids & moving head coordinates.
     *    For all other open-eye poses, applies True 2.5D Brow-Fur Eyelid Stretch,
     *    vertical feline slit-to-oval pupil modulation, and corneal parallax glint.
     */
    if (!is_seedance_idle && r->eye_rx > 0.5f) {
        float blink = clampf(ap->blink, 0.0f, 1.0f);
        float exs[2] = { r->lx, r->rx };
        float eys[2] = { r->ly, r->ry };
        const uint16_t lash_col  = 0x18C3; /* #18100C dark feline lash line */
        const uint16_t glint_col = 0xFFFF; /* pure corneal catchlight */
        const uint16_t pupil_col = 0x0821; /* deep obsidian vertical pupil */

        for (int e = 0; e < 2; e++) {
            float cx = exs[e], cy = eys[e];
            float rx = r->eye_rx, ry = r->eye_ry;
            int x0 = (int)(cx - rx - 1.5f), x1 = (int)(cx + rx + 1.5f);
            int y0 = (int)(cy - ry - 1.5f), y1 = (int)(cy + ry + 1.5f);
            if (x0 < 2) x0 = 2;
            if (x1 >= NW - 2) x1 = NW - 3;
            if (y0 < 4) y0 = 4;
            if (y1 >= NH - 4) y1 = NH - 5;

            /* Vertical feline slit-to-oval pupil & parallax corneal glint */
            float px_c = cx + ap->gaze_x * 0.85f;
            float py_c = cy + ap->gaze_y * 0.60f;
            float prx = 0.76f + 0.76f * ap->pupil_dilate;
            float pry = 1.90f + 0.38f * ap->pupil_dilate;
            float gx_c = cx - 0.85f + ap->gaze_x * 0.32f;
            float gy_c = cy - 0.85f + ap->gaze_y * 0.25f;

            /* Upper lid sweeps down (-1.10 -> +0.72), lower lid rises (+1.10 -> +0.48) */
            float upper_lid_y = -1.10f + blink * 1.82f;
            float lower_lid_y =  1.10f - blink * 0.62f;

            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    float nx = ((float)x - cx) / rx;
                    float ny = ((float)y - cy) / ry;
                    float d2 = nx * nx + ny * ny;
                    if (d2 > 1.22f) continue;

                    uint16_t cur = dst[y * NW + x];
                    if (cur == 0) continue;

                    /* Natural feline almond lid arch */
                    float arch = -0.15f * (1.0f - nx * nx);
                    float u_edge = upper_lid_y + arch;
                    float l_edge = lower_lid_y + arch * 0.4f;

                    if (blink > 0.03f && (ny <= u_edge || ny >= l_edge)) {
                        /*
                         * True 2.5D Brow-Fur Eyelid Stretch:
                         * Continuously stretches the real brow fur patch [cy-ry-2.8 .. cy-ry-0.8]
                         * above column x downward over the eye socket (and cheek fur upward),
                         * preserving the exact cream temple fur & dark Siamese mask gradient!
                         */
                        float sy_fur;
                        if (ny <= u_edge) {
                            float u_t = clampf((ny + 1.15f) / (u_edge + 1.20f), 0.0f, 1.0f);
                            sy_fur = (cy - ry - 2.8f) + u_t * 1.8f;
                        } else {
                            float l_t = clampf((1.15f - ny) / (1.20f - l_edge), 0.0f, 1.0f);
                            sy_fur = (cy + ry + 2.4f) - l_t * 1.4f;
                        }
                        uint16_t fur_col = sample_bilinear_q8(
                            raw_pose, x << 8, (int)(sy_fur * 256.0f + 0.5f));
                        if (fur_col == 0) fur_col = cur;

                        float dist_edge = (ny <= u_edge) ? (u_edge - ny) : (ny - l_edge);
                        if (dist_edge < 0.36f) {
                            /* Crisp dark feline eyelash crease along the lid meeting line */
                            int lw = (int)((1.0f - dist_edge / 0.36f) * 165.0f);
                            fur_col = blend565(fur_col, lash_col, lw);
                        }
                        int bw = (int)(clampf(dist_edge * 4.5f + 0.55f, 0.0f, 1.0f) * 256.0f);
                        dst[y * NW + x] = blend565(cur, fur_col, bw);
                    } else if (d2 <= 0.85f && blink < 0.78f) {
                        /* Slit-to-oval pupil & wet corneal catchlight inside iris */
                        float p_dx = ((float)x - px_c) / prx;
                        float p_dy = ((float)y - py_c) / pry;
                        float p_d2 = p_dx * p_dx + p_dy * p_dy;
                        float g_dx = (float)x - gx_c;
                        float g_dy = (float)y - gy_c;
                        float g_d2 = g_dx * g_dx + g_dy * g_dy;
                        if (g_d2 < 0.78f) {
                            int gw = (int)((0.78f - g_d2) * (1.0f / 0.78f) * 220.0f);
                            dst[y * NW + x] = blend565(cur, glint_col, gw);
                        } else if (p_d2 < 0.82f) {
                            int pw = (int)((0.82f - p_d2) * (1.0f / 0.82f) * 175.0f);
                            dst[y * NW + x] = blend565(cur, pupil_col, pw);
                        }
                    }
                }
            }
        }
    }

    /*
     * 2. Anatomical Multi-Layer Feline Mouth Cleft:
     *    Note: RIG_SPEAK and RIG_CELEBRATE already have real open mouths in the
     *    Higgsfield artwork; for those we articulate their lower chin in warp_pose_25d.
     */
    if (rig_id != RIG_SPEAK && rig_id != RIG_CELEBRATE && ap->mouth_open > 0.05f && r->mr > 1.0f) {
        float open = clampf(ap->mouth_open, 0.0f, 1.0f);
        float rx = r->mr * (0.34f + 0.16f * open);
        float ry = r->mr * (0.14f + 0.28f * open);
        float mcx = r->mx;
        float mcy = r->my + 0.85f * open;
        int x0 = (int)(mcx - rx - 1.0f), x1 = (int)(mcx + rx + 1.0f);
        int y0 = (int)(mcy - ry - 1.0f), y1 = (int)(mcy + ry + 1.0f);
        if (x0 < 0) x0 = 0;
        if (x1 >= NW) x1 = NW - 1;
        if (y0 < 0) y0 = 0;
        if (y1 >= NH) y1 = NH - 1;

        const uint16_t lip_dark_col   = 0x2862; /* #280C10 deep feline upper lip shadow */
        const uint16_t palate_col     = 0x5965; /* #5A2C2A shaded upper mouth interior */
        const uint16_t tongue_mid_col = 0xB30C; /* #B56163 warm rose tongue body */
        const uint16_t tongue_tip_col = 0xE4B1; /* #E6968C bright pink tongue tip */
        const uint16_t fang_col       = 0xF79E; /* #F7F3F0 tiny upper canine glint */

        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float nx = ((float)x - mcx) / rx;
                float ny = ((float)y - mcy) / ry;
                /* Feline inverted-V upper lip arch */
                float upper_arch = -0.75f + 0.35f * nx * nx;
                float lower_arch =  0.85f - 0.55f * nx * nx;
                if (ny < upper_arch || ny > lower_arch || fabsf(nx) > 0.95f) continue;
                if (dst[y * NW + x] == 0) continue;

                uint16_t mc = palate_col;
                float edge_dist = fminf(ny - upper_arch, lower_arch - ny);
                if (edge_dist < 0.22f) {
                    mc = lip_dark_col;
                } else if (open > 0.35f && ny < -0.10f && fabsf(nx) > 0.42f && fabsf(nx) < 0.68f) {
                    mc = fang_col;
                } else if (ny > 0.05f) {
                    int tw = (int)(clampf((ny - 0.05f) * 1.4f, 0.0f, 1.0f) * 256.0f);
                    mc = blend565(tongue_mid_col, tongue_tip_col, tw);
                }
                int mw = (int)(clampf(edge_dist * 3.2f + 0.35f, 0.0f, 1.0f) * open * 235.0f);
                dst[y * NW + x] = blend565(dst[y * NW + x], mc, mw);
            }
        }
    }
}

/* Warp a prepared 136x136 pose using its 2.5D anatomical rig into out_buf */
static void warp_pose_25d(int rig_id, const uint16_t *src, const anim_params_t *ap, uint16_t *out_buf)
{
    const pose_rig_t *r = &S_RIGS[rig_id];
    float sin_h = sinf(ap->head_rot);
    float cos_h = cosf(ap->head_rot);
    float inv_hr2 = 1.0f / (r->hr * r->hr);
    float inv_er2 = 1.0f / (r->ear_r * r->ear_r);
    float inv_cr2 = 1.0f / (r->cr * r->cr);
    float inv_mr2 = r->mr > 1.0f ? 1.0f / (r->mr * r->mr) : 0.0f;
    float inv_plr2 = r->plr > 1.0f ? 1.0f / (r->plr * r->plr) : 0.0f;
    float inv_prr2 = r->prr > 1.0f ? 1.0f / (r->prr * r->prr) : 0.0f;
    float inv_ar2 = r->ar > 1.0f ? 1.0f / (r->ar * r->ar) : 0.0f;

    /* Precompute kinematic tail spine vector & perpendicular normal */
    float tvx = r->ttx - r->tbx;
    float tvy = r->tty - r->tby;
    float t_len2 = tvx * tvx + tvy * tvy;
    float t_len = t_len2 > 1.0f ? sqrtf(t_len2) : 1.0f;
    float inv_tlen2 = 1.0f / t_len2;
    /* Perpendicular normal vector to the tail spine */
    float tnx = -tvy / t_len;
    float tny =  tvx / t_len;
    float inv_trw = r->tr_w > 1.0f ? 1.0f / r->tr_w : 0.0f;

    bool has_eyes = (!(rig_id == RIG_IDLE && ap->greet_pos > 0.05f) && r->eye_rx > 0.5f);
    float inv_eyel_rx2 = has_eyes ? 1.0f / (r->eye_rx * r->eye_rx) : 0.0f;
    float inv_eyel_ry2 = has_eyes ? 1.0f / (r->eye_ry * r->eye_ry) : 0.0f;

    for (int y = 0; y < NH; y++) {
        /* Ground anchor keeps rear paws/hips planted at bottom of canvas */
        float ground_lock = (y > 110) ? clampf((126.0f - (float)y) / 16.0f, 0.0f, 1.0f) : 1.0f;
        /* Vertical body crouch/stretch around ground plane y=124 */
        float base_sy = (float)y - ap->body_squash_y * ground_lock * ((124.0f - (float)y) / 100.0f);

        for (int x = 0; x < NW; x++) {
            float sx = (float)x - ap->body_lead_x * ground_lock;
            float sy = base_sy;

            /* 1. Head rotation & nod around neck pivot */
            float hdx = (float)x - r->hx;
            float hdy = (float)y - r->hy;
            float hd2 = (hdx * hdx + hdy * hdy) * inv_hr2;
            if (hd2 < 1.65f) {
                float hw = 1.0f - hd2 * (1.0f / 1.65f);
                hw = hw * hw * (3.0f - 2.0f * hw);
                float rx = (float)x - r->nx;
                float ry = (float)y - r->ny;
                float rot_x = r->nx + rx * cos_h - ry * sin_h - ap->head_dx;
                float rot_y = r->ny + rx * sin_h + ry * cos_h - ap->head_dy;
                sx += (rot_x - (float)x) * hw;
                sy += (rot_y - (float)y) * hw;

                /*
                 * 1b. Sub-Pixel Iris/Pupil Saccadic Gaze Shift inside open eyes:
                 *     Shifts the actual sapphire iris & dark pupil texture inside the
                 *     eye socket without moving the eyelids or surrounding fur!
                 */
                if (has_eyes && ap->blink < 0.55f) {
                    float el_dx = (float)x - r->lx, el_dy = (float)y - r->ly;
                    float el_d2 = el_dx * el_dx * inv_eyel_rx2 + el_dy * el_dy * inv_eyel_ry2;
                    if (el_d2 < 0.90f) {
                        float ew = (0.90f - el_d2) * (1.0f / 0.90f);
                        sx -= ap->gaze_x * 1.25f * ew;
                        sy -= ap->gaze_y * 0.90f * ew;
                    } else {
                        float er_dx = (float)x - r->rx, er_dy = (float)y - r->ry;
                        float er_d2 = er_dx * er_dx * inv_eyel_rx2 + er_dy * er_dy * inv_eyel_ry2;
                        if (er_d2 < 0.90f) {
                            float ew = (0.90f - er_d2) * (1.0f / 0.90f);
                            sx -= ap->gaze_x * 1.25f * ew;
                            sy -= ap->gaze_y * 0.90f * ew;
                        }
                    }
                }

                /*
                 * 1c. 2.5D Lower-Jaw Drop, Chin-Closure, Whisker-Pad Puff & Nose Sniffing:
                 *     - In RIG_SPEAK & RIG_CELEBRATE (natively open-mouthed), pulls the
                 *       lower white chin fur upward by up to 4.4 px when mouth_open->0,
                 *       cleanly closing the mouth between syllables!
                 *     - In closed-mouth poses, drops the chin fur downward and puffs
                 *       the left/right whisker pads outward when vocalizing!
                 */
                if (inv_mr2 > 0.0f) {
                    float mdx = (float)x - r->mx;
                    float mdy = (float)y - r->my;
                    float md2 = (mdx * mdx + mdy * mdy) * inv_mr2;
                    if (md2 < 1.55f) {
                        float mw = 1.0f - md2 * (1.0f / 1.55f);
                        mw = mw * mw * (3.0f - 2.0f * mw);
                        if (rig_id == RIG_SPEAK || rig_id == RIG_CELEBRATE) {
                            float close_amt = 1.0f - clampf(ap->mouth_open, 0.0f, 1.0f);
                            if (mdy >= -4.2f && close_amt > 0.02f) {
                                float jaw_w = smoothstepf(-4.2f, -0.5f, mdy);
                                sy += close_amt * 5.2f * mw * jaw_w;
                            }
                        } else if (ap->mouth_open > 0.02f) {
                            if (mdy >= -0.5f) {
                                sy -= ap->mouth_open * 2.8f * mw;
                            }
                            if (fabsf(mdx) > 1.0f && mdy < 2.0f) {
                                float dir = (mdx > 0.0f) ? 1.0f : -1.0f;
                                sx -= dir * ap->mouth_open * 0.95f * mw;
                            }
                        }
                        /* Rapid feline nose-sniffing & whisker-pad twitch */
                        if (ap->sniff > 0.01f && mdy <= 1.5f) {
                            float dir = (mdx >= 0.0f) ? 1.0f : -1.0f;
                            sx -= dir * ap->sniff * 0.85f * mw;
                            sy += ap->sniff * 0.65f * mw;
                        }
                    }
                }
            }

            /* 2. Independent Left & Right Ear Swivel: + pricks forward and up, - flattens out and down */
            float eldx = (float)x - r->elx, eldy = (float)y - r->ely;
            float eld2 = (eldx * eldx + eldy * eldy) * inv_er2;
            if (eld2 < 1.0f) {
                float ew = (1.0f - eld2);
                ew *= ew;
                sx -= ap->ear_l * 2.8f * ew;
                sy += ap->ear_l * 1.5f * ew;
            }
            float erdx = (float)x - r->erx, erdy = (float)y - r->ery;
            float erd2 = (erdx * erdx + erdy * erdy) * inv_er2;
            if (erd2 < 1.0f) {
                float ew = (1.0f - erd2);
                ew *= ew;
                sx += ap->ear_r * 2.8f * ew;
                sy += ap->ear_r * 1.5f * ew;
            }

            /* 3. Chest & Ribcage Continuous Breathing (+ subtle shoulder rock with kneading) */
            float cdx = (float)x - r->cx;
            float cdy = (float)y - r->cy;
            float cd2 = (cdx * cdx + cdy * cdy) * inv_cr2;
            if (cd2 < 1.0f) {
                float cw = (1.0f - cd2);
                cw *= cw;
                float exp_scale = ap->breath * 0.048f * cw;
                float knead_rock = (ap->knead_l - ap->knead_r) * 0.75f * cw;
                sx -= (cdx * exp_scale + knead_rock);
                sy -= (cdy * exp_scale - ap->breath * 1.25f * cw);
            }

            /*
             * 4. Articulated Left & Right Front Paws:
             *    Rhythmic feline kneading ("hacer panecillos") lifts each paw by up to
             *    3.4 px while flexing the wrist and spreading the toes horizontally.
             */
            if (inv_plr2 > 0.0f && ap->knead_l > 0.01f) {
                float pdx = (float)x - r->plx;
                float pdy = (float)y - r->ply;
                float pd2 = (pdx * pdx + pdy * pdy) * inv_plr2;
                if (pd2 < 1.0f) {
                    float pw = (1.0f - pd2);
                    pw *= pw;
                    /* Lift paw upward, flex wrist slightly inward, and spread toes */
                    sy += ap->knead_l * 3.4f * pw;
                    sx -= (pdx * 0.14f - 0.8f) * ap->knead_l * pw;
                }
            }
            if (inv_prr2 > 0.0f && ap->knead_r > 0.01f && r->action_type != ACT_PAW_WAVE) {
                float pdx = (float)x - r->prx;
                float pdy = (float)y - r->pry;
                float pd2 = (pdx * pdx + pdy * pdy) * inv_prr2;
                if (pd2 < 1.0f) {
                    float pw = (1.0f - pd2);
                    pw *= pw;
                    sy += ap->knead_r * 3.4f * pw;
                    sx -= (pdx * 0.14f + 0.8f) * ap->knead_r * pw;
                }
            }

            /*
             * 5. 2-Harmonic Kinematic Spine Tail Deformer:
             *    Projects (x,y) onto the tail spine segment (tbx,tby)->(ttx,tty).
             *    Displaces perpendicular to the spine with an s^1.4 whip profile
             *    (strictly 0 at the body root s=0, curving in an S-wave & flicking at the tip s->1).
             */
            if (hd2 >= 1.15f && cd2 >= 0.68f && inv_trw > 0.0f) {
                float px = (float)x - r->tbx;
                float py = (float)y - r->tby;
                float s_spine = (px * tvx + py * tvy) * inv_tlen2;
                if (s_spine > 0.0f && s_spine < 1.30f) {
                    float s_clamped = clampf(s_spine, 0.0f, 1.0f);
                    float proj_x = s_clamped * tvx;
                    float proj_y = s_clamped * tvy;
                    float d_perp2 = (px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y);
                    float trw2 = r->tr_w * r->tr_w;
                    if (d_perp2 < trw2) {
                        float w_corr = 1.0f - d_perp2 / trw2;
                        w_corr *= w_corr;
                        /* Nonlinear whip stiffness: 0 at root, flexible S-wave whip at tip */
                        float s_whip = s_clamped * sqrtf(s_clamped);
                        float wave1 = sinf(ap->tail_phase - 2.7f * s_clamped);
                        float wave2 = 0.42f * sinf(ap->tail_phase * 2.0f - 5.2f * s_clamped + ap->tail_flick);
                        float disp = ap->tail_amp * s_whip * w_corr * (0.72f * wave1 + wave2);
                        sx -= tnx * disp;
                        sy -= tny * disp;
                    }
                }
            }

            /* 6. Special Action Zones (Wrist-Pivoted Paw Wave/Groom, Prowl Step, Bow Stretch) */
            if (r->action_type != ACT_NONE) {
                float adx = (float)x - r->ax;
                float ady = (float)y - r->ay;
                float ad2 = (adx * adx + ady * ady) * inv_ar2;
                if (ad2 < 1.0f) {
                    float aw = (1.0f - ad2);
                    aw *= aw;
                    if (r->action_type == ACT_PAW_WAVE) {
                        /* Pivot raised right paw around wrist/elbow (73, 54) in a grooming/waving arc */
                        float ang = sinf(ap->action_phase) * 0.25f * aw;
                        float s_a = sinf(ang), c_a = cosf(ang);
                        float wx = (float)x - 73.0f;
                        float wy = (float)y - 54.0f;
                        sx += (73.0f + wx * c_a - wy * s_a - (float)x);
                        sy += (54.0f + wx * s_a + wy * c_a - (float)y) - cosf(ap->action_phase * 2.0f) * 2.2f * aw;
                    } else if (r->action_type == ACT_PROWL) {
                        sx -= sinf(ap->action_phase) * ap->action_amp * aw;
                        sy -= cosf(ap->action_phase * 2.0f) * (ap->action_amp * 0.45f) * aw;
                    } else if (r->action_type == ACT_STRETCH) {
                        /* Reach forepaws forward & bow chest down */
                        float stretch_pull = sinf(ap->action_phase);
                        sx += stretch_pull * ap->action_amp * aw;
                        sy -= stretch_pull * (ap->action_amp * 0.55f) * aw;
                    }
                }
            }

            int fx_q8 = (int)(sx * 256.0f + 0.5f);
            int fy_q8 = (int)(sy * 256.0f + 0.5f);
            out_buf[y * NW + x] = sample_bilinear_q8(src, fx_q8, fy_q8);
        }
    }
}

/*
 * Behaviour: postures, an idle scheduler and spring-driven layers.
 *
 * Sagwa holds one posture for minutes and only ever moves to a neighbouring
 * one (S_EDGE_S). What the agent is doing shows in her face, ears, tail and
 * breathing, never by forcing her body into another pose. Every layer value
 * reaches its target through a critically damped spring or an integrated
 * phase, so a state change mid-motion continues from where it was.
 *
 * Until there are real transition clips, a posture change is a long dissolve
 * (1 to 2.2 s) with one blink at its middle and a slight dip of the body.
 */

typedef enum {
    POST_SLEEP = 0, /* curled up asleep */
    POST_LOAF,      /* sphinx loaf, resting awake */
    POST_SIT,       /* sitting, facing the viewer: where the agent works */
    POST_STAND,     /* standing */
    POST_STALK,     /* playful hunting crouch */
    POST_STRETCH,   /* bow stretch, only on the way up from the loaf */
    POST_COUNT
} posture_t;

#define POST_NONE POST_COUNT

static const int S_POSTURE_RIG[POST_COUNT] = {
    RIG_SLEEP, RIG_WAKE, RIG_LISTEN, RIG_IDLE, RIG_WORK, RIG_STRETCH,
};

/* Idle dwell range, seconds */
static const float S_DWELL[POST_COUNT][2] = {
    { 360.0f, 1500.0f }, { 60.0f, 300.0f }, { 40.0f, 180.0f },
    { 20.0f, 60.0f }, { 6.0f, 14.0f }, { 2.6f, 2.6f },
};

/* Dissolve length between neighbouring postures, seconds; 0 = not neighbours */
static const float S_EDGE_S[POST_COUNT][POST_COUNT] = {
    /*             SLEEP  LOAF   SIT    STAND  STALK  STRETCH */
    [POST_SLEEP]   = { 0.0f, 1.6f, 0.0f, 0.0f, 0.0f, 0.0f },
    [POST_LOAF]    = { 2.2f, 0.0f, 1.3f, 0.0f, 0.0f, 1.2f },
    [POST_SIT]     = { 0.0f, 1.5f, 0.0f, 1.1f, 0.0f, 0.0f },
    [POST_STAND]   = { 0.0f, 0.0f, 1.2f, 0.0f, 1.0f, 0.0f },
    [POST_STALK]   = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    [POST_STRETCH] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
};

#define STRETCH_HOLD_S 2.6f
#define HEAD_DISSOLVE_S 0.45f
#define GREET_LAST 11.0f       /* last Seedance frame of the standing head turn */
#define GREET_GESTURE_S 7.4f

typedef enum {
    ACTV_IDLE = 0,
    ACTV_BOOT,
    ACTV_LISTEN,
    ACTV_THINK,
    ACTV_SPEAK,
    ACTV_ERROR,
    ACTV_PET,
    ACTV_OFF,
} activity_t;

typedef struct {
    float x, v;
} spring_t;

typedef struct {
    bool init;
    uint32_t rng;
    float t;
    activity_t act;

    posture_t posture, goal;
    int sit_rig;                   /* head variant while sitting */
    bool in_trans;
    posture_t trans_to;
    int trans_from_rig;
    float trans_from_greet, trans_u, trans_dur;
    int head_from_rig;             /* head dissolve inside POST_SIT */
    float head_u;

    float dwell, afterglow, energy, stretch_left, last_play, last_change, hop_pause;

    float breath_ph;
    spring_t breath_rate, breath_amp, purr;
    float lid_base, blink_start, blink_len, next_blink, slow_blink_until;
    float next_sacc, gaze_tx, gaze_ty;
    spring_t gaze_x, gaze_y, head_rot, head_dy, pupil;
    float next_twitch;
    spring_t ear_l, ear_r;
    float mouth, yawn_at;
    float tail_ph, flick_ph;
    spring_t tail_amp, tail_hz, flick_amp;
    spring_t crouch, lead_x;
    float next_shift, shift_target;
    float knead_ph;
    spring_t knead_amp;
    float next_sniff, sniff_until, sniff_amt;
    float greet_pos, greet_start, next_greet;
    float action_ph;
    spring_t action_amp;
} sagwa_beh_t;

static sagwa_beh_t B;

static uint32_t rng_next(void)
{
    uint32_t x = B.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    B.rng = x ? x : 0x9e3779b9u;
    return B.rng;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (float)(rng_next() >> 8) * (1.0f / 16777216.0f);
}

/* Critically damped spring toward `target`; omega in rad/s (~4/omega to settle). */
static void spring_to(spring_t *s, float target, float omega, float dt)
{
    float h = dt;
    int n = 1;
    while (h * omega > 0.25f && n < 16) {
        h *= 0.5f;
        n *= 2;
    }
    for (int i = 0; i < n; i++) {
        float a = omega * omega * (target - s->x) - 2.0f * omega * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
}

static int posture_rig(posture_t p)
{
    return p == POST_SIT ? B.sit_rig : S_POSTURE_RIG[p];
}

static posture_t next_hop(posture_t from, posture_t to)
{
    posture_t prev[POST_COUNT];
    bool seen[POST_COUNT] = { false };
    posture_t queue[POST_COUNT];
    int qh = 0, qt = 0;
    queue[qt++] = from;
    seen[from] = true;
    while (qh < qt) {
        posture_t p = queue[qh++];
        if (p == to) {
            while (prev[p] != from) {
                p = prev[p];
            }
            return p;
        }
        for (int n = 0; n < POST_COUNT; n++) {
            if (S_EDGE_S[p][n] > 0.0f && !seen[n]) {
                seen[n] = true;
                prev[n] = p;
                queue[qt++] = (posture_t)n;
            }
        }
    }
    return POST_NONE;
}

static void start_trans(posture_t to)
{
    float d = S_EDGE_S[B.posture][to];
    if (d <= 0.0f) {
        return;
    }
    /* Woken by the agent: lift the head a little quicker than waking alone */
    if (B.posture == POST_SLEEP && B.act != ACTV_IDLE && B.act != ACTV_OFF) {
        d = 1.1f;
    }
    B.in_trans = true;
    B.trans_to = to;
    B.trans_from_rig = posture_rig(B.posture);
    B.trans_from_greet = B.greet_pos;
    B.trans_u = 0.0f;
    B.trans_dur = d;
    B.head_u = 1.0f;
    /* The dissolve carries its own blink; hold spontaneous ones until after it */
    if (B.next_blink < B.t + d + 0.6f) {
        B.next_blink = B.t + d + frand(0.6f, 2.0f);
    }
}

static void step_toward_goal(void)
{
    if (B.goal == POST_NONE || B.goal == B.posture) {
        B.goal = POST_NONE;
        return;
    }
    posture_t n = next_hop(B.posture, B.goal);
    if (n == POST_NONE) {
        B.goal = POST_NONE;
        return;
    }
    start_trans(n);
}

static void go_to(posture_t p)
{
    B.goal = p;
    B.hop_pause = 0.0f;
    if (!B.in_trans && B.posture != POST_STRETCH) {
        step_toward_goal();
    }
}

static void set_sit_rig(int rig)
{
    if (rig == B.sit_rig) {
        return;
    }
    if (B.posture == POST_SIT && !B.in_trans) {
        B.head_from_rig = B.sit_rig;
        B.head_u = 0.0f;
    }
    B.sit_rig = rig;
}

static void start_greet(float delay)
{
    if (B.greet_start < 0.0f) {
        B.next_greet = B.t + delay;
    }
}

static void new_dwell(void)
{
    B.dwell = frand(S_DWELL[B.posture][0], S_DWELL[B.posture][1]);
}

static void beh_init(const muse_pose_t *pose)
{
    memset(&B, 0, sizeof(B));
    B.init = true;
#ifdef ESP_PLATFORM
    B.rng = esp_random() | 1u;
#else
    B.rng = 0x5a6a1234u;
#endif
    B.t = pose->t;
    B.act = ACTV_BOOT;
    B.posture = POST_LOAF;
    B.goal = POST_NONE;
    B.sit_rig = RIG_LISTEN;
    B.head_u = 1.0f;
    B.energy = 0.55f;
    B.afterglow = frand(8.0f, 20.0f);
    new_dwell();
    B.last_play = -1e6f;
    B.last_change = B.t;
    B.lid_base = 1.0f;             /* eyes open slowly at boot */
    B.yawn_at = B.t + 0.9f;
    B.next_blink = B.t + 2.5f;
    B.next_sacc = B.t + 1.5f;
    B.next_twitch = B.t + frand(4.0f, 9.0f);
    B.next_shift = B.t + frand(6.0f, 10.0f);
    B.next_sniff = B.t + frand(6.0f, 15.0f);
    B.greet_start = -1.0f;
    B.next_greet = 1e9f;
    B.breath_rate.x = 24.0f;
    B.breath_amp.x = 0.85f;
    B.tail_amp.x = 1.5f;
    B.tail_hz.x = 0.12f;
    B.pupil.x = 0.5f;
}

static activity_t activity_of(const muse_pose_t *pose)
{
    if (pose->mode == MUSE_MODE_OFF) return ACTV_OFF;
    if (pose->happy > 0.05f) return ACTV_PET;
    switch (pose->mode) {
    case MUSE_MODE_BOOT:      return ACTV_BOOT;
    case MUSE_MODE_LISTENING: return ACTV_LISTEN;
    case MUSE_MODE_THINKING:  return ACTV_THINK;
    case MUSE_MODE_SPEAKING:  return ACTV_SPEAK;
    case MUSE_MODE_ERROR:     return ACTV_ERROR;
    default:                  return ACTV_IDLE;
    }
}

static void on_activity(activity_t was, activity_t now)
{
    B.act = now;
    if (now == ACTV_IDLE) {
        /* Stay attentive where she is, then settle down */
        B.afterglow = (was == ACTV_BOOT) ? frand(8.0f, 20.0f) : frand(30.0f, 90.0f);
        if (was == ACTV_SPEAK || was == ACTV_PET) {
            B.slow_blink_until = B.t + 3.0f;
        }
        if (B.goal != POST_NONE && B.goal != POST_SLEEP) {
            B.goal = POST_NONE;
        }
        return;
    }
    if (now == ACTV_OFF) {
        if (B.posture != POST_SLEEP) {
            B.yawn_at = B.t + 0.2f;
        }
        go_to(POST_SLEEP);
        return;
    }
    if (now == ACTV_BOOT) {
        return;
    }
    /* The agent is busy: keep the body, wake her if needed, look at the viewer */
    if (B.posture == POST_SLEEP || (B.in_trans && B.trans_to == POST_SLEEP) || B.goal == POST_SLEEP) {
        go_to(POST_LOAF);
    } else if (B.posture == POST_STALK || B.posture == POST_STRETCH ||
               (B.in_trans && (B.trans_to == POST_STALK || B.trans_to == POST_STRETCH))) {
        go_to(POST_STAND);
    } else {
        B.goal = POST_NONE;
    }
    if ((now == ACTV_LISTEN || now == ACTV_SPEAK) && B.posture == POST_STAND && !B.in_trans) {
        start_greet(0.15f);
    }
    B.next_sacc = B.t;             /* look at what changed right away */
}

/* Idle: pick the next posture from where she is and how tired she is */
static void decide(void)
{
    float e = B.energy;
    float r = frand(0.0f, 1.0f);
    switch (B.posture) {
    case POST_SLEEP:
        go_to(POST_LOAF);
        return;
    case POST_LOAF: {
        float w_sleep = 0.3f + 1.2f * (1.0f - e);
        float w_sit = 0.35f * e;
        float w_up = e > 0.5f ? 0.25f * e : 0.03f;
        r *= w_sleep + w_sit + w_up;
        if (r < w_sleep) {
            go_to(POST_SLEEP);
        } else if (r < w_sleep + w_sit) {
            go_to(POST_SIT);
        } else {
            /* Getting up from the loaf always goes through a stretch */
            B.goal = POST_STAND;
            start_trans(POST_STRETCH);
        }
        return;
    }
    case POST_SIT: {
        float w_down = 0.35f + 0.8f * (1.0f - e);
        float w_stand = 0.30f * e;
        r *= w_down + w_stand + 0.15f;
        if (r < w_down) {
            go_to(POST_LOAF);
        } else if (r < w_down + w_stand) {
            go_to(POST_STAND);
        } else {
            B.dwell = frand(S_DWELL[POST_SIT][0], S_DWELL[POST_SIT][1]) * 0.5f;
        }
        return;
    }
    case POST_STAND: {
        float w_play = (e > 0.55f && B.t - B.last_play > 1200.0f) ? 0.30f : 0.0f;
        r *= 0.65f + w_play + 0.10f;
        if (r < 0.65f) {
            go_to(POST_SIT);
        } else if (r < 0.65f + w_play) {
            B.last_play = B.t;
            go_to(POST_STALK);
        } else {
            start_greet(0.0f);
            B.dwell = frand(S_DWELL[POST_STAND][0], S_DWELL[POST_STAND][1]);
        }
        return;
    }
    default:
        go_to(POST_STAND);
        return;
    }
}

static void update_posture(float dt)
{
    bool asleep = B.posture == POST_SLEEP && !B.in_trans;
    B.energy = clampf(B.energy + (asleep ? dt / 1200.0f : -dt / 1500.0f), 0.0f, 1.0f);

    if (B.in_trans) {
        B.trans_u += dt / B.trans_dur;
        if (B.trans_u >= 1.0f) {
            posture_t from = B.posture;
            B.posture = B.trans_to;
            B.in_trans = false;
            B.last_change = B.t;
            B.greet_pos = 0.0f;
            B.greet_start = -1.0f;
            B.next_greet = 1e9f;
            if (from == POST_SLEEP) {
                B.yawn_at = B.t + 0.4f;
            }
            if (B.posture == POST_STRETCH) {
                B.stretch_left = STRETCH_HOLD_S;
                B.action_ph = 0.0f;
            } else if (B.goal != POST_NONE && B.goal != B.posture) {
                /* Settle a moment before the next step of a longer way */
                B.hop_pause = frand(1.0f, 2.2f);
            } else {
                B.goal = POST_NONE;
                new_dwell();
                if (B.posture == POST_STAND && frand(0.0f, 1.0f) < 0.5f) {
                    start_greet(frand(2.0f, 5.0f));
                }
            }
        }
    } else if (B.posture == POST_STRETCH) {
        B.stretch_left -= dt;
        if (B.stretch_left <= 0.0f) {
            if (B.goal == POST_NONE) {
                B.goal = POST_STAND;
            }
            step_toward_goal();
        }
    } else if (B.act == ACTV_IDLE && B.goal == POST_NONE) {
        if (B.afterglow > 0.0f) {
            B.afterglow -= dt;
        } else {
            B.dwell -= dt;
        }
        if (B.afterglow <= 0.0f && B.dwell <= 0.0f) {
            decide();
        }
    }

    if (B.head_u < 1.0f) {
        B.head_u += dt / HEAD_DISSOLVE_S;
    }

    /*
     * Sitting keeps one body. The think and tilt drawings sit differently
     * (tail on the other side), so thinking and errors show through the
     * layers instead; only petting swaps to the closed-eye purring face.
     */
    set_sit_rig(B.act == ACTV_PET ? RIG_PURR : RIG_LISTEN);

    if (B.hop_pause > 0.0f && !B.in_trans) {
        B.hop_pause -= dt;
        if (B.hop_pause <= 0.0f) {
            step_toward_goal();
        }
    }

    /* Standing head turn toward the viewer, from the Seedance clip */
    if (B.posture == POST_STAND && !B.in_trans) {
        if (B.greet_start < 0.0f && B.t >= B.next_greet) {
            B.greet_start = B.t;
            B.next_greet = 1e9f;
        }
        if (B.greet_start >= 0.0f) {
            float u = (B.t - B.greet_start) / GREET_GESTURE_S;
            if (u >= 1.0f) {
                B.greet_pos = 0.0f;
                B.greet_start = -1.0f;
                if (B.act == ACTV_IDLE) {
                    B.next_greet = B.t + frand(12.0f, 25.0f);
                }
            } else {
                float tri = u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u;
                B.greet_pos = GREET_LAST * smoothstepf(0.0f, 1.0f, tri);
            }
        }
    }
}

static void update_layers(const muse_pose_t *pose, float dt)
{
    activity_t a = B.act;
    posture_t p = B.in_trans ? B.trans_to : B.posture;
    bool awake = p != POST_SLEEP;
    float t = B.t;

    /* Breathing: rate in breaths per minute, inhale 40 % / exhale 60 % */
    float rate = !awake ? 20.0f : a == ACTV_ERROR ? 36.0f : p == POST_STALK ? 34.0f :
                 a == ACTV_LISTEN ? 30.0f : a == ACTV_PET ? 26.0f : p == POST_LOAF ? 24.0f : 28.0f;
    spring_to(&B.breath_rate, rate, 0.8f, dt);
    spring_to(&B.breath_amp, awake ? 0.85f : 1.25f, 1.0f, dt);
    spring_to(&B.purr, a == ACTV_PET ? 1.0f : 0.0f, 3.0f, dt);
    B.breath_ph += dt * B.breath_rate.x / 60.0f;
    B.breath_ph -= floorf(B.breath_ph);

    /* Eyelids: a resting level plus discrete blinks */
    float lid = !awake ? 1.0f : a == ACTV_PET ? 0.35f : a == ACTV_OFF ? 0.55f :
                (p == POST_LOAF && B.energy < 0.4f) ? 0.45f : p == POST_LOAF ? 0.15f :
                a == ACTV_THINK ? 0.15f : a == ACTV_LISTEN ? 0.0f : 0.05f;
    B.lid_base += (lid - B.lid_base) * fminf(1.0f, dt * (awake ? 3.0f : 1.2f));
    bool native_eyes = p == POST_STAND && B.greet_pos > 0.05f;
    if (!B.in_trans && awake && !native_eyes && t >= B.next_blink) {
        bool slow = a == ACTV_PET || t < B.slow_blink_until || (p == POST_LOAF && B.energy < 0.4f);
        B.blink_start = t;
        B.blink_len = slow ? 1.7f : 0.22f;
        B.next_blink = t + (slow ? frand(3.0f, 5.0f) : a == ACTV_LISTEN ? frand(7.0f, 14.0f) : frand(4.0f, 11.0f));
    }

    /* Gaze: quick saccades between fixations, head follows late */
    if (t >= B.next_sacc) {
        float interval;
        if (!awake) {
            B.gaze_tx = 0.0f; B.gaze_ty = 0.0f; interval = 3.0f;
        } else if (a == ACTV_LISTEN || a == ACTV_SPEAK || a == ACTV_PET) {
            B.gaze_tx = frand(-0.08f, 0.08f); B.gaze_ty = frand(-0.12f, 0.0f); interval = frand(1.5f, 3.5f);
        } else if (a == ACTV_THINK) {
            B.gaze_tx = frand(0.0f, 1.0f) < 0.5f ? -0.65f : 0.6f; B.gaze_ty = -0.5f; interval = frand(2.5f, 5.0f);
        } else if (a == ACTV_ERROR) {
            B.gaze_tx = frand(-0.4f, 0.4f); B.gaze_ty = frand(-0.2f, 0.2f); interval = frand(0.8f, 2.0f);
        } else if (p == POST_STALK) {
            B.gaze_tx = 0.7f + frand(-0.1f, 0.1f); B.gaze_ty = 0.1f; interval = frand(0.6f, 1.4f);
        } else {
            B.gaze_tx = frand(-0.8f, 0.8f); B.gaze_ty = frand(-0.5f, 0.3f); interval = frand(1.2f, 4.0f);
        }
        B.next_sacc = t + interval;
    }
    spring_to(&B.gaze_x, B.gaze_tx, 45.0f, dt);
    spring_to(&B.gaze_y, B.gaze_ty, 45.0f, dt);
    float rot = !awake ? 0.0f : B.gaze_x.x * 0.035f + (a == ACTV_THINK ? 0.04f : 0.0f) +
                (a == ACTV_PET ? 0.05f * sinf(t * 1.4f) : 0.0f);
    spring_to(&B.head_rot, rot, 5.0f, dt);
    float lvl = clampf(pose->level, 0.0f, 1.0f);
    float hdy = !awake ? 0.0f : a == ACTV_LISTEN ? -1.5f : a == ACTV_ERROR ? 1.0f :
                a == ACTV_SPEAK ? -0.5f + 1.2f * lvl : 0.0f;
    spring_to(&B.head_dy, hdy, 7.0f, dt);

    float pupil = a == ACTV_LISTEN ? 0.9f : a == ACTV_PET ? 0.95f : p == POST_STALK ? 1.0f :
                  a == ACTV_ERROR ? 0.85f : a == ACTV_SPEAK ? 0.7f : a == ACTV_THINK ? 0.6f : 0.45f;
    spring_to(&B.pupil, pupil, 4.0f, dt);

    /* Ears: + forward, - flattened; a quick flick now and then */
    float el, er;
    if (!awake) {
        el = er = -0.1f;
    } else if (a == ACTV_LISTEN) {
        el = er = 0.9f;
    } else if (a == ACTV_ERROR) {
        el = er = -0.9f;
    } else if (a == ACTV_PET) {
        el = er = -0.3f;
    } else if (a == ACTV_THINK) {
        float side = (fmodf(t, 5.2f) < 2.6f) ? 1.0f : -1.0f;
        el = 0.45f * side;
        er = -0.45f * side;
    } else if (a == ACTV_SPEAK || p == POST_STALK) {
        el = er = a == ACTV_SPEAK ? 0.45f : 0.8f;
    } else {
        el = er = 0.15f;
    }
    if (t >= B.next_twitch) {
        float kick = frand(0.0f, 1.0f) < 0.5f ? 22.0f : -22.0f;
        if (frand(0.0f, 1.0f) < 0.5f) {
            B.ear_l.v += kick;
        } else {
            B.ear_r.v += kick;
        }
        if (!awake) {
            B.sniff_until = t + 0.3f;   /* dreaming: a whisker twitches with the ear */
            B.sniff_amt = 0.3f;
        }
        B.next_twitch = t + (awake ? frand(8.0f, 30.0f) : frand(40.0f, 120.0f));
    }
    spring_to(&B.ear_l, el, 14.0f, dt);
    spring_to(&B.ear_r, er, 14.0f, dt);

    /* Mouth: follows the voice when speaking; yawns on waking */
    float mouth = 0.0f;
    if (a == ACTV_SPEAK && awake) {
        mouth = clampf((lvl - 0.04f) * 1.8f, 0.0f, 0.6f);
    }
    float yu = (t - B.yawn_at) / 1.8f;
    if (yu >= 0.0f && yu < 1.0f) {
        mouth = fmaxf(mouth, 0.9f * sinf(yu * PI_F));
    }
    B.mouth += (mouth - B.mouth) * fminf(1.0f, dt / (mouth > B.mouth ? 0.04f : 0.12f));

    /* Tail: amplitude (px), sway frequency (Hz) and tip flick, all sprung */
    float ta, th, tf;
    if (!awake) { ta = 1.0f; th = 0.08f; tf = 0.0f; }
    else if (a == ACTV_ERROR) { ta = 8.0f; th = 1.4f; tf = 0.6f; }
    else if (a == ACTV_THINK) { ta = 3.2f; th = 0.6f; tf = 1.2f; }
    else if (a == ACTV_PET) { ta = 4.5f; th = 0.25f; tf = 0.4f; }
    else if (a == ACTV_SPEAK) { ta = 5.0f; th = 0.3f; tf = 0.3f; }
    else if (a == ACTV_LISTEN) { ta = 2.2f; th = 0.2f; tf = 0.2f; }
    else if (p == POST_STALK) { ta = 5.5f; th = 2.2f; tf = 1.6f; }
    else if (p == POST_STRETCH) { ta = 6.0f; th = 0.5f; tf = 0.6f; }
    else { ta = 4.5f; th = 0.18f; tf = 0.3f; }
    spring_to(&B.tail_amp, ta, 2.5f, dt);
    spring_to(&B.tail_hz, th, 2.5f, dt);
    spring_to(&B.flick_amp, tf, 2.5f, dt);
    B.tail_ph = fmodf(B.tail_ph + TAU_F * B.tail_hz.x * dt, TAU_F * 64.0f);
    B.flick_ph = fmodf(B.flick_ph + 9.0f * dt, TAU_F * 64.0f);

    /* Body: crouch on error, a slow weight shift while awake */
    spring_to(&B.crouch, a == ACTV_ERROR ? -3.0f : 0.0f, 6.0f, dt);
    if (t >= B.next_shift) {
        B.shift_target = awake ? frand(-1.0f, 1.0f) : 0.0f;
        B.next_shift = t + frand(6.0f, 10.0f);
    }
    spring_to(&B.lead_x, B.shift_target, 1.5f, dt);

    /* Kneading while petted and sitting */
    spring_to(&B.knead_amp, (a == ACTV_PET && p == POST_SIT) ? 1.0f : 0.0f, 3.0f, dt);
    B.knead_ph = fmodf(B.knead_ph + 3.8f * dt, TAU_F * 64.0f);

    /* Sniffing while standing */
    if (p == POST_STAND && a == ACTV_IDLE && t >= B.next_sniff) {
        B.sniff_until = t + 0.65f;
        B.sniff_amt = 1.0f;
        B.next_sniff = t + frand(6.0f, 15.0f);
    }

    /* Prowl step while playing; one slow reach during the stretch */
    if (B.posture == POST_STRETCH && !B.in_trans) {
        B.action_ph = PI_F * clampf(1.0f - B.stretch_left / STRETCH_HOLD_S, 0.0f, 1.0f);
        spring_to(&B.action_amp, 3.8f, 6.0f, dt);
    } else {
        B.action_ph = fmodf(B.action_ph + 3.2f * dt, TAU_F * 64.0f);
        spring_to(&B.action_amp, p == POST_STALK ? 3.0f : 0.0f, 4.0f, dt);
    }
}

/* Snapshot of the layers for one rig */
static void layers_to_params(anim_params_t *ap)
{
    memset(ap, 0, sizeof(*ap));
    float t = B.t;

    float ph = B.breath_ph;
    float wave = ph < 0.4f ? -cosf(PI_F * ph / 0.4f) : cosf(PI_F * (ph - 0.4f) / 0.6f);
    ap->breath = B.breath_amp.x * wave + 0.15f * B.purr.x * sinf(t * 25.0f);

    float u = (t - B.blink_start) / (B.blink_len > 0.0f ? B.blink_len : 0.22f);
    float spike = 0.0f;
    if (u >= 0.0f && u < 1.0f) {
        spike = u < 0.32f ? smoothstepf(0.0f, 0.32f, u) : u < 0.45f ? 1.0f : 1.0f - smoothstepf(0.45f, 1.0f, u);
    }
    ap->blink = fmaxf(B.lid_base, spike);

    ap->gaze_x = B.gaze_x.x;
    ap->gaze_y = B.gaze_y.x;
    ap->head_rot = B.head_rot.x;
    ap->head_dy = B.head_dy.x;
    ap->pupil_dilate = clampf(B.pupil.x, 0.0f, 1.0f);
    ap->ear_l = clampf(B.ear_l.x, -1.2f, 1.2f);
    ap->ear_r = clampf(B.ear_r.x, -1.2f, 1.2f);
    ap->mouth_open = clampf(B.mouth, 0.0f, 1.0f);
    ap->tail_phase = B.tail_ph;
    ap->tail_amp = B.tail_amp.x;
    ap->tail_flick = B.flick_amp.x * sinf(B.flick_ph);
    ap->body_squash_y = B.crouch.x;
    ap->body_lead_x = B.lead_x.x;
    if (t < B.sniff_until) {
        ap->sniff = B.sniff_amt * (0.5f + 0.5f * sinf(t * 52.0f));
    }
    float kl = sinf(B.knead_ph), kr = sinf(B.knead_ph + PI_F);
    ap->knead_l = kl > 0.0f ? kl * kl * B.knead_amp.x : 0.0f;
    ap->knead_r = kr > 0.0f ? kr * kr * B.knead_amp.x : 0.0f;
    ap->action_phase = B.action_ph;
    ap->action_amp = B.action_amp.x;
    ap->greet_pos = B.greet_pos;
}

uint32_t sagwa_legacy_accent(muse_mode_t mode)
{
    switch (mode) {
    case MUSE_MODE_LISTENING: return 0x64b5f6;
    case MUSE_MODE_THINKING:  return 0xd4b896;
    case MUSE_MODE_SPEAKING:  return 0xf5f0e6;
    case MUSE_MODE_ERROR:     return 0xff6b6b;
    default:                  return 0x7cb9e8;
    }
}

static void render_rig(int rig, const anim_params_t *ap, uint16_t *out)
{
    prepare_base_pose(rig, ap, s_work_a);
    warp_pose_25d(rig, s_work_a, ap, out);
}

/* out = a dissolved toward b; silhouette edges switch over a narrower window */
static void dissolve(uint16_t *a, const uint16_t *b, float w_core, float w_edge)
{
    int bw_core = (int)(w_core * 256.0f + 0.5f);
    int bw_edge = (int)(w_edge * 256.0f + 0.5f);
    for (int i = 0; i < NW * NH; i++) {
        uint16_t pa = a[i], pb = b[i];
        a[i] = blend565(pa, pb, (pa == 0 || pb == 0) ? bw_edge : bw_core);
    }
}

void sagwa_legacy_render(const muse_pose_t *pose)
{
    if (!pose) return;

    if (!B.init) {
        beh_init(pose);
    }
    float dt = (pose->t >= B.t) ? (pose->t - B.t) : 0.02f;
    if (dt > 0.1f) dt = 0.1f;
    B.t = pose->t;

    activity_t act = activity_of(pose);
    if (act != B.act) {
        on_activity(B.act, act);
    }
    update_posture(dt);
    update_layers(pose, dt);

    anim_params_t ap;
    layers_to_params(&ap);

    if (B.in_trans) {
        /* Posture change: long dissolve, one blink at its middle, a slight dip */
        float u = clampf(B.trans_u, 0.0f, 1.0f);
        float arc = sinf(u * PI_F);
        anim_params_t ap_a = ap, ap_b = ap;
        ap_a.greet_pos = B.trans_from_greet;
        ap_b.greet_pos = 0.0f;
        ap_a.blink = ap_b.blink = fmaxf(ap.blink, arc * 0.95f);
        ap_a.body_squash_y = ap_b.body_squash_y = ap.body_squash_y - 3.0f * arc;
        render_rig(B.trans_from_rig, &ap_a, s_out_fb);
        render_rig(posture_rig(B.trans_to), &ap_b, s_work_b);
        dissolve(s_out_fb, s_work_b, smoothstepf(0.25f, 0.75f, u), smoothstepf(0.40f, 0.60f, u));
    } else if (B.head_u < 1.0f) {
        /* Head variant change while sitting: same body, short dissolve */
        float w = smoothstepf(0.0f, 1.0f, B.head_u);
        render_rig(B.head_from_rig, &ap, s_out_fb);
        render_rig(B.sit_rig, &ap, s_work_b);
        dissolve(s_out_fb, s_work_b, w, smoothstepf(0.2f, 0.8f, B.head_u));
    } else {
        render_rig(posture_rig(B.posture), &ap, s_out_fb);
    }
}

void sagwa_legacy_set_size(int px)
{
    s_target_px = px > 16 ? px : 16;
}

void sagwa_legacy_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    int sz = s_target_px > 0 ? s_target_px : NW;
    if (sz == NW) {
        for (int y = y0; y <= y1; y++) {
            uint16_t *row = dst + (y - y0) * stride_px;
            if ((unsigned)y >= (unsigned)NH) {
                memset(row, 0, (size_t)(x1 - x0 + 1) * sizeof(uint16_t));
                continue;
            }
            const uint16_t *src_row = &s_out_fb[y * NW];
            for (int x = x0; x <= x1; x++) {
                row[x - x0] = ((unsigned)x < (unsigned)NW) ? src_row[x] : 0;
            }
        }
        return;
    }

    /* High-precision Q8 bilinear scaling for any target size (e.g. 168px center -> 86px corner) */
    int step_q8 = (NW * 256) / sz;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = dst + (y - y0) * stride_px;
        int fy_q8 = y * step_q8;
        int fx_q8 = x0 * step_q8;
        for (int x = x0; x <= x1; x++) {
            *row++ = sample_bilinear_q8(s_out_fb, fx_q8, fy_q8);
            fx_q8 += step_q8;
        }
    }
}

int sagwa_legacy_get_rig(void)
{
    if (!B.init) {
        return RIG_WAKE;
    }
    return posture_rig(B.in_trans ? B.trans_to : B.posture);
}
