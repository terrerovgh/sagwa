// Sagwa from real animation clips for the M5Stack CoreS3.
//
// Every clip was generated from the shared hub poses (sleep, loaf, sit, stand,
// stalk) and starts and ends on them, so the player chains clips with no
// dissolve: a transition clip moves her between neighbouring hubs, a loop clip
// plays on a hub and comes back to it. Nothing is warped; what moves is what
// was drawn.
//
// The IMU only wakes her: when the device is really moved (not a tap or a
// buzz), a sleeping or resting Sagwa gets up, stretches and stands waiting a
// while before she decides what to do. Warmth makes her sleepier. What she's
// doing is counted for the settings (sagwa_stats) and named for the line
// under her feet.

#include "sagwa_clips.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/tjpgd.h"
#include "sdkconfig.h"

#include "muse_settings.h"
#include "muse_state.h"
#include "sagwa_stats.h"

#if CONFIG_MUSE_BOARD_M5STACK_CORES3
#include "cores3_sensors.h"
#endif

static const char *TAG = "sagwa";

#define PI_F 3.14159265f
#define PART_SUBTYPE 0x50
#define BLACK 26                 /* channel max below this is background */
#define BUFS 3                   /* decoded frames: shown, next, spare */

/* The rig ids sagwa_sfx syncs its sounds to (same numbers as the warped renderer) */
enum { RIG_IDLE = 0, RIG_LOOK, RIG_LISTEN, RIG_THINK, RIG_SPEAK, RIG_WORK, RIG_CELEBRATE, RIG_PURR,
       RIG_SLEEP, RIG_WAKE, RIG_STRETCH, RIG_TILT };

/* ---- The pack ---- */

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version, w, h, fps, nclips, reserved;
    uint32_t nframes, frame_table, data;
} pack_hdr_t;

typedef struct __attribute__((packed)) {
    char name[20];
    uint16_t first, count;
    uint8_t from, to, flags, pad;
} pack_clip_t;

typedef struct __attribute__((packed)) {
    uint32_t off, size;
} pack_frame_t;

enum { HUB_SLEEP = 0, HUB_LOAF, HUB_SIT, HUB_STAND, HUB_STALK, HUB_COUNT, HUB_NONE = HUB_COUNT };

typedef enum {
    CL_SLEEP_LOAF, CL_LOAF_SLEEP, CL_LOAF_SIT, CL_SIT_LOAF, CL_SIT_STAND, CL_STAND_SIT, CL_LOAF_STAND,
    CL_STAND_STALK, CL_STALK_STAND,
    CL_SIT_IDLE, CL_SIT_GROOM, CL_SIT_MEOW, CL_SIT_THINK, CL_SIT_STARTLE, CL_SIT_PURR,
    CL_LOAF_IDLE, CL_LOAF_LISTEN, CL_SLEEP_IDLE, CL_STAND_IDLE, CL_STAND_WALK, CL_STAND_BALANCE, CL_STALK_IDLE,
    CL_COUNT, CL_NONE = CL_COUNT
} clip_id_t;

static const char *const CLIP_NAMES[CL_COUNT] = {
    "sleep_to_loaf", "loaf_to_sleep", "loaf_to_sit", "sit_to_loaf", "sit_to_stand", "stand_to_sit",
    "loaf_to_stand", "stand_to_stalk", "stalk_to_stand",
    "sit_idle", "sit_groom", "sit_meow", "sit_think", "sit_startle", "sit_purr",
    "loaf_idle", "loaf_listen", "sleep_idle", "stand_idle", "stand_walk", "stand_balance", "stalk_idle",
};

/* Loops calm enough to cut short (a quick dissolve back to the hub) when something happens */
static bool calm(clip_id_t c)
{
    return c == CL_SIT_IDLE || c == CL_LOAF_IDLE || c == CL_SLEEP_IDLE || c == CL_STAND_IDLE ||
           c == CL_STAND_WALK || c == CL_SIT_THINK || c == CL_LOAF_LISTEN || c == CL_STALK_IDLE;
}

static const uint8_t *s_base;
static const pack_hdr_t *s_hdr;
static const pack_clip_t *s_clips;
static const pack_frame_t *s_frames;
static int s_clip[CL_COUNT];          /* pack index of each clip, or -1 */
static int s_hub_frame[HUB_COUNT];    /* a frame showing each hub, or -1 */
static int W, H;                      /* frame size */
static uint32_t s_data_len;           /* bytes of frame data in the partition */
static float s_fps;

/* ---- Decoding, on its own task ---- */

static uint16_t *s_buf[BUFS];
static volatile int s_buf_frame[BUFS] = { -1, -1, -1 };
static volatile int s_want[2] = { -1, -1 };   /* frame needed now, and the one after */
static volatile int s_front = -1;              /* buffer on screen */
static TaskHandle_t s_decoder;
static uint32_t s_decode_us_max;

typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint16_t *dst;
} jsrc_t;

static UINT jin(JDEC *jd, BYTE *buf, UINT n)
{
    jsrc_t *s = jd->device;
    size_t left = s->len - s->pos;
    if (n > left) n = (UINT)left;
    if (buf) memcpy(buf, s->src + s->pos, n);
    s->pos += n;
    return n;
}

static UINT jout(JDEC *jd, void *bitmap, JRECT *r)
{
    jsrc_t *s = jd->device;
    const uint8_t *rgb = bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        uint16_t *row = s->dst + y * W;
        for (int x = r->left; x <= r->right; x++, rgb += 3) {
            if (x >= W || y >= H) continue;
            uint8_t m = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
            m = m > rgb[2] ? m : rgb[2];
            row[x] = m < BLACK ? 0 : (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
        }
    }
    return 1;
}

static bool decode(int frame, uint16_t *dst)
{
    static EXT_RAM_BSS_ATTR uint8_t pool[3100];
    static int s_errors;
    const pack_frame_t *f = &s_frames[frame];
    if (f->off > s_data_len || f->size > s_data_len - f->off) {
        if (s_errors++ < 5) ESP_LOGW(TAG, "frame %d: outside the pack", frame);
        return false;
    }
    jsrc_t src = { .src = s_base + s_hdr->data + f->off, .len = f->size, .dst = dst };
    JDEC jd;
    JRESULT rc = jd_prepare(&jd, jin, pool, sizeof(pool), &src);
    if (rc == JDR_OK) {
        rc = jd_decomp(&jd, jout, 0);
    }
    if (rc != JDR_OK && s_errors++ < 5) {
        ESP_LOGW(TAG, "frame %d (%u bytes at %u): decode error %d", frame, (unsigned)f->size, (unsigned)f->off, rc);
    }
    return rc == JDR_OK;
}

static int buf_with(int frame)
{
    for (int i = 0; i < BUFS; i++) {
        if (s_buf_frame[i] == frame) return i;
    }
    return -1;
}

static void decoder_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        for (int k = 0; k < 2; k++) {
            int want = s_want[k];
            if (want < 0 || buf_with(want) >= 0) continue;
            /* A buffer that's neither on screen nor holding the other wanted frame */
            int other = s_want[1 - k], slot = -1;
            for (int i = 0; i < BUFS; i++) {
                if (i != s_front && s_buf_frame[i] != other) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) continue;
            s_buf_frame[slot] = -1;
            int64_t t0 = esp_timer_get_time();
            if (decode(want, s_buf[slot])) {
                s_buf_frame[slot] = want;
            }
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            if (us > s_decode_us_max) {
                s_decode_us_max = us;
                if (us > 120000) {   /* a visible stutter at 12 fps */
                    ESP_LOGW(TAG, "slow frame decode: %u ms", (unsigned)(us / 1000));
                }
            }
        }
    }
}

/* ---- Behaviour ---- */

typedef enum { ACT_IDLE, ACT_BOOT, ACT_LISTEN, ACT_THINK, ACT_SPEAK, ACT_ERROR, ACT_PET, ACT_OFF } act_t;

typedef struct {
    float x, v;
} spring_t;

static struct {
    bool init;
    float t;
    act_t act;
    int hub, goal;
    clip_id_t clip;           /* playing, or CL_NONE while holding the hub */
    float clip_t, speed, hold;
    bool mirror;              /* facing left */
    float dwell, afterglow, energy, last_groom, last_play, last_meow;
    /* a short dissolve from a copy of the last frame, for cuts and turns */
    float fade, fade_len;
    spring_t x;               /* sideways position, while walking */
    float temp0, temp;
    bool have_temp0;
    float moving_s, alert_until, wake_cool, last_sensor_t;
    bool woken;               /* on her way up because the device moved */
    float wait_until;         /* standing, waiting to see what happens */
} S;

static uint16_t *s_fade_buf;
static int s_target_px = 240;

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() >> 8) * (1.0f / 16777216.0f);
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}

static float x_limit(void);

static bool has(clip_id_t c)
{
    return c < CL_COUNT && s_clip[c] >= 0;
}

static int clip_from(clip_id_t c)
{
    return s_clips[s_clip[c]].from;
}

static int clip_to(clip_id_t c)
{
    return s_clips[s_clip[c]].to;
}

/* The transition clip from hub a to neighbouring hub b, if there is one */
static clip_id_t edge(int a, int b)
{
    for (int c = 0; c < CL_COUNT; c++) {
        if (has(c) && clip_from(c) == a && clip_to(c) == b && a != b) return (clip_id_t)c;
    }
    return CL_NONE;
}

/* First transition on the shortest way from a to b */
static clip_id_t next_edge(int a, int b)
{
    int prev[HUB_COUNT], q[HUB_COUNT], qh = 0, qt = 0;
    bool seen[HUB_COUNT] = { false };
    q[qt++] = a;
    seen[a] = true;
    while (qh < qt) {
        int h = q[qh++];
        if (h == b) {
            while (prev[h] != a) h = prev[h];
            return edge(a, h);
        }
        for (int n = 0; n < HUB_COUNT; n++) {
            if (!seen[n] && edge(h, n) != CL_NONE) {
                seen[n] = true;
                prev[n] = h;
                q[qt++] = n;
            }
        }
    }
    return CL_NONE;
}

static void start_fade(float len)
{
    int b = s_front;
    if (b >= 0 && s_fade_buf) {
        memcpy(s_fade_buf, s_buf[b], (size_t)W * H * 2);
        S.fade = 0.0f;
        S.fade_len = len;
    }
}

static void play(clip_id_t c, float speed)
{
    switch (c) {
    case CL_LOAF_STAND: sagwa_stats_count(SAGWA_N_STRETCH); break;
    case CL_STAND_STALK: sagwa_stats_count(SAGWA_N_PLAY); break;
    case CL_SIT_GROOM: sagwa_stats_count(SAGWA_N_GROOM); break;
    case CL_STAND_WALK: sagwa_stats_count(SAGWA_N_WALK); break;
    case CL_LOAF_SLEEP: sagwa_stats_count(SAGWA_N_NAP); break;
    case CL_SIT_MEOW:
        if (S.act == ACT_IDLE) sagwa_stats_count(SAGWA_N_MEOW);
        break;
    default: break;
    }
    S.clip = c;
    S.clip_t = 0.0f;
    S.speed = speed;
    S.hold = 0.0f;
}

static void hold(float secs)
{
    S.clip = CL_NONE;
    S.hold = secs;
}

static void set_goal(int hub)
{
    S.goal = hub;
}

/* Idle: where to go from this hub, by how tired and warm she is */
static void decide_idle(void)
{
    float e = S.energy, r = frand(0.0f, 1.0f);
    switch (S.hub) {
    case HUB_SLEEP:
        set_goal(HUB_LOAF);
        break;
    case HUB_LOAF: {
        float w_sleep = 0.3f + 1.2f * (1.0f - e), w_sit = 0.35f * e, w_up = e > 0.5f ? 0.3f * e : 0.03f;
        r *= w_sleep + w_sit + w_up;
        set_goal(r < w_sleep ? HUB_SLEEP : r < w_sleep + w_sit ? HUB_SIT : HUB_STAND);
        break;
    }
    case HUB_SIT: {
        float w_down = 0.35f + 0.8f * (1.0f - e), w_up = 0.35f * e;
        r *= w_down + w_up + 0.15f;
        if (r < w_down) set_goal(HUB_LOAF);
        else if (r < w_down + w_up) set_goal(HUB_STAND);
        else S.dwell = frand(20.0f, 60.0f);
        break;
    }
    case HUB_STAND: {
        float w_play = (e > 0.55f && S.t - S.last_play > 1200.0f && has(CL_STAND_STALK)) ? 0.3f : 0.0f;
        r *= 0.7f + w_play;
        if (r < 0.7f) set_goal(HUB_SIT);
        else {
            S.last_play = S.t;
            set_goal(HUB_STALK);
        }
        break;
    }
    default:
        set_goal(HUB_STAND);
        break;
    }
    if (S.goal == S.hub) S.goal = HUB_NONE;
}

static void new_dwell(void)
{
    static const float D[HUB_COUNT][2] = { { 360, 1500 }, { 60, 300 }, { 40, 180 }, { 20, 60 }, { 6, 14 } };
    S.dwell = frand(D[S.hub][0], D[S.hub][1]);
}

/* Which loop to play on this hub, for what's going on */
static clip_id_t pick_loop(void)
{
    act_t a = S.act;
    bool alert = S.t < S.alert_until || a == ACT_LISTEN;
    switch (S.hub) {
    case HUB_SLEEP:
        return has(CL_SLEEP_IDLE) ? CL_SLEEP_IDLE : CL_NONE;
    case HUB_LOAF:
        if ((alert || a == ACT_THINK || a == ACT_SPEAK) && has(CL_LOAF_LISTEN)) return CL_LOAF_LISTEN;
        return has(CL_LOAF_IDLE) ? CL_LOAF_IDLE : CL_NONE;
    case HUB_SIT:
        if (a == ACT_PET && has(CL_SIT_PURR)) return CL_SIT_PURR;
        if (a == ACT_SPEAK && has(CL_SIT_MEOW)) return CL_SIT_MEOW;
        if ((a == ACT_THINK || a == ACT_ERROR) && has(CL_SIT_THINK)) return CL_SIT_THINK;
        if (a == ACT_IDLE && !alert && has(CL_SIT_GROOM) && S.t - S.last_groom > 120.0f && frand(0, 1) < 0.25f) {
            S.last_groom = S.t;
            return CL_SIT_GROOM;
        }
        if (a == ACT_IDLE && has(CL_SIT_MEOW) && S.t - S.last_meow > 600.0f && frand(0, 1) < 0.05f) {
            S.last_meow = S.t;
            return CL_SIT_MEOW;   /* now and then she calls out */
        }
        return has(CL_SIT_IDLE) ? CL_SIT_IDLE : CL_NONE;
    case HUB_STAND:
        if (a == ACT_IDLE && !alert && S.t >= S.wait_until && has(CL_STAND_WALK) && frand(0, 1) < 0.45f) {
            /* only where there's room ahead */
            float ahead = S.mirror ? S.x.x + x_limit() : x_limit() - S.x.x;
            if (ahead > 40.0f) return CL_STAND_WALK;
        }
        return has(CL_STAND_IDLE) ? CL_STAND_IDLE : CL_NONE;
    case HUB_STALK:
        return has(CL_STALK_IDLE) ? CL_STALK_IDLE : CL_NONE;
    }
    return CL_NONE;
}

/* The current clip ended (or a hold ran out): what next */
static void next(void)
{
    bool busy = S.act != ACT_IDLE && S.act != ACT_BOOT && S.act != ACT_OFF;
    if (S.goal != HUB_NONE && S.goal != S.hub) {
        clip_id_t c = next_edge(S.hub, S.goal);
        if (c != CL_NONE) {
            /* Facing: transitions out of the front-facing sit keep it; walking can flip it at the sit */
            play(c, busy ? 1.35f : 1.0f);
            return;
        }
        S.goal = HUB_NONE;
    }
    /* Turn round while sitting, facing front, if she'd walk into the edge */
    if (S.hub == HUB_SIT && S.fade >= 1.0f) {
        float edge_x = (S.x.x > 0) ? 1.0f : -1.0f;
        bool facing_edge = (S.mirror ? -1.0f : 1.0f) == edge_x;
        if (facing_edge && fabsf(S.x.x) > 20.0f) {
            S.mirror = !S.mirror;
            start_fade(0.35f);
        }
    }
    clip_id_t loop = pick_loop();
    if (loop == CL_NONE) {
        hold(1.0f);
    } else {
        play(loop, 1.0f);
    }
}

static act_t activity_of(const muse_pose_t *p)
{
    if (p->mode == MUSE_MODE_OFF) return ACT_OFF;
    if (p->happy > 0.05f) return ACT_PET;
    switch (p->mode) {
    case MUSE_MODE_BOOT: return ACT_BOOT;
    case MUSE_MODE_LISTENING: return ACT_LISTEN;
    case MUSE_MODE_THINKING: return ACT_THINK;
    case MUSE_MODE_SPEAKING: return ACT_SPEAK;
    case MUSE_MODE_ERROR: return ACT_ERROR;
    default: return ACT_IDLE;
    }
}

/* Cut a calm loop short so a change shows now, not when the loop ends */
static void cut_if_calm(void)
{
    if (S.clip != CL_NONE && calm(S.clip) && clip_from(S.clip) == clip_to(S.clip)) {
        start_fade(0.25f);
        hold(0.0f);
    }
}

static void on_activity(act_t was, act_t now)
{
    S.act = now;
    if (now == ACT_THINK && was != ACT_SPEAK) sagwa_stats_count(SAGWA_N_WORK);
    if (now == ACT_PET) sagwa_stats_count(SAGWA_N_PURR);
    if (now != ACT_IDLE) S.wait_until = 0.0f;
    if (now == ACT_IDLE) {
        S.afterglow = (was == ACT_BOOT) ? frand(8.0f, 20.0f) : frand(30.0f, 90.0f);
        if (S.goal != HUB_SLEEP) S.goal = HUB_NONE;
        return;
    }
    if (now == ACT_OFF) {
        sagwa_stats_save();
        set_goal(HUB_SLEEP);
        cut_if_calm();
        return;
    }
    if (now == ACT_BOOT) return;
    /* Busy: wake her if asleep, sit her up to talk, otherwise keep the body */
    if (S.hub == HUB_SLEEP || S.goal == HUB_SLEEP) set_goal(HUB_LOAF);
    else if (S.hub == HUB_STALK) set_goal(HUB_STAND);
    else S.goal = HUB_NONE;
    if (now == ACT_SPEAK && S.hub == HUB_LOAF && has(CL_LOAF_SIT)) set_goal(HUB_SIT);
    cut_if_calm();
}

/* ---- Sensors: real movement wakes her; warmth ---- */

#define MOVE_DPS 40.0f      /* turning faster than this, for MOVE_S, is being moved */
#define MOVE_S 0.45f        /* taps, buzzes and the speaker's shake don't last this long */

/* The device was moved: up she gets, with a stretch, and waits standing */
static void woken_by_motion(void)
{
    sagwa_stats_count(SAGWA_N_WOKEN);
    S.woken = true;
    S.alert_until = S.t + 60.0f;
    set_goal(HUB_STAND);
    cut_if_calm();
}

static void sense(float dt)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    S.wake_cool -= dt;
    if (S.t - S.last_sensor_t < 0.05f) return;
    float step = S.t - S.last_sensor_t;
    S.last_sensor_t = S.t;
    cores3_sensors_snapshot_t s;
    cores3_sensors_read(&s);
    if (!s.imu_valid) return;
    if (!S.have_temp0) {
        S.temp0 = s.temperature_c;
        S.have_temp0 = true;
    }
    S.temp = s.temperature_c;
    float gyro = sqrtf(s.gyro_x * s.gyro_x + s.gyro_y * s.gyro_y + s.gyro_z * s.gyro_z);
    S.moving_s = gyro > MOVE_DPS ? S.moving_s + step : fmaxf(0.0f, S.moving_s - 2.0f * step);
    bool resting = S.hub == HUB_SLEEP || S.hub == HUB_LOAF || S.goal == HUB_SLEEP || S.goal == HUB_LOAF;
    if (S.moving_s >= MOVE_S && S.wake_cool <= 0.0f && muse_settings_motion_wake() &&
        (S.act == ACT_IDLE || S.act == ACT_BOOT)) {
        S.wake_cool = 20.0f;
        S.moving_s = 0.0f;
        if (resting) {
            ESP_LOGI(TAG, "moved (%.0f dps): getting up", gyro);
            woken_by_motion();
        } else {
            S.alert_until = S.t + 30.0f;   /* awake already: she just watches */
        }
    }
#else
    (void)dt;
#endif
}

/* ---- Physics of the whole body ---- */

#define WALK_PX_S 46.0f     /* frame pixels per second, matched to the walk cycle */

static float x_limit(void)
{
    /* How far either side the canvas can go: the screen is 320, the sprite W at this size */
    float k = (float)s_target_px / (float)W;
    float room = (320.0f - (float)W * k) * 0.5f / k + 30.0f;
    return room > 0.0f ? room : 0.0f;
}

static void physics(float dt)
{
    /* Sideways only by walking, easing into and out of the stride */
    float lim = x_limit();
    bool walking = S.clip == CL_STAND_WALK;
    float want_v = walking ? (S.mirror ? -WALK_PX_S : WALK_PX_S) : 0.0f;
    S.x.v += (want_v - S.x.v) * 4.0f * dt;
    S.x.x += S.x.v * dt;
    if (S.x.x > lim) {
        S.x.x = lim;
        S.x.v = 0.0f;
    } else if (S.x.x < -lim) {
        S.x.x = -lim;
        S.x.v = 0.0f;
    }
    /* Walked up to the edge: stop and stand */
    if (walking && fabsf(S.x.x) >= lim - 1.0f && ((S.x.x > 0) != S.mirror)) {
        start_fade(0.25f);
        hold(0.6f);
    }
}

/* ---- Per frame ---- */

static void init_state(const muse_pose_t *pose)
{
    memset(&S, 0, sizeof(S));
    S.init = true;
    S.t = pose->t;
    S.act = ACT_BOOT;
    S.hub = HUB_LOAF;
    S.goal = HUB_NONE;
    S.clip = CL_NONE;
    S.energy = 0.55f;
    S.afterglow = frand(8.0f, 20.0f);
    S.fade = 1.0f;
    S.last_groom = S.last_play = S.last_meow = -1e6f;
    new_dwell();
    hold(0.5f);
}

void sagwa_clips_render(const muse_pose_t *pose)
{
    if (!pose || !s_hdr) return;
    if (!S.init) init_state(pose);
    float dt = pose->t - S.t;
    if (dt < 0.0f || dt > 0.1f) dt = 0.02f;
    S.t = pose->t;

    act_t a = activity_of(pose);
    if (a != S.act) on_activity(S.act, a);
    sense(dt);

    /* Her day, for the settings */
    static const sagwa_time_t HUB_TIME[HUB_COUNT] = { SAGWA_T_SLEEP, SAGWA_T_REST, SAGWA_T_SIT, SAGWA_T_STAND,
                                                      SAGWA_T_PLAY };
    sagwa_stats_add_time(HUB_TIME[S.hub], dt);
    if (S.act == ACT_THINK || S.act == ACT_SPEAK) sagwa_stats_add_time(SAGWA_T_WORK, dt);

    /* Energy: drains awake, refills asleep; warmth makes her drowsier */
    bool asleep = S.hub == HUB_SLEEP && S.clip != CL_SLEEP_LOAF;
    float warm = S.have_temp0 && S.temp > S.temp0 + 4.0f ? 1.5f : 1.0f;
    S.energy = clampf(S.energy + (asleep ? dt / 1200.0f : -dt * warm / 1500.0f), 0.0f, 1.0f);

    /* Idle scheduling at a hub (not while she waits, up after being moved) */
    if (S.act == ACT_IDLE && S.goal == HUB_NONE && S.t >= S.wait_until) {
        if (S.afterglow > 0.0f) S.afterglow -= dt;
        else S.dwell -= dt;
        if (S.afterglow <= 0.0f && S.dwell <= 0.0f) {
            decide_idle();
            if (S.goal == HUB_NONE) new_dwell();
            else cut_if_calm();
        }
    }

    /* Advance the clip; at its end, land on its hub and pick the next */
    if (S.clip != CL_NONE) {
        const pack_clip_t *c = &s_clips[s_clip[S.clip]];
        S.clip_t += dt * S.speed;
        if (S.clip_t * s_fps >= (float)c->count) {
            int to = c->to;
            if (to != S.hub) {
                S.hub = to;
                if (S.goal == S.hub) {
                    S.goal = HUB_NONE;
                    new_dwell();
                }
                if (S.woken && S.hub == HUB_STAND) {
                    S.woken = false;
                    S.wait_until = S.t + frand(45.0f, 90.0f);
                }
            }
            next();
        }
    } else {
        S.hold -= dt;
        if (S.hold <= 0.0f) next();
    }
    if (S.fade < 1.0f) S.fade += dt / (S.fade_len > 0.01f ? S.fade_len : 0.25f);

    physics(dt);

    /* The frame to show, and the next to have ready */
    int frame, after;
    if (S.clip != CL_NONE) {
        const pack_clip_t *c = &s_clips[s_clip[S.clip]];
        int i = (int)(S.clip_t * s_fps);
        if (i >= c->count) i = c->count - 1;
        frame = c->first + i;
        after = i + 1 < c->count ? frame + 1 : frame;
    } else {
        frame = after = s_hub_frame[S.hub];
    }
    s_want[0] = frame;
    s_want[1] = after;
    int b = buf_with(frame);
    if (b >= 0) s_front = b;
    if (s_decoder) xTaskNotifyGive(s_decoder);
}

/* ---- Drawing: the frame scaled to the canvas, her feet on its floor ---- */

void sagwa_clips_set_size(int px)
{
    s_target_px = px > 16 ? px : 16;
}

static inline uint16_t blend565(uint16_t a, uint16_t b, int w)
{
    if (w <= 0) return a;
    if (w >= 256) return b;
    int r = (((a >> 11) & 31) * (256 - w) + ((b >> 11) & 31) * w) >> 8;
    int g = (((a >> 5) & 63) * (256 - w) + ((b >> 5) & 63) * w) >> 8;
    int bl = ((a & 31) * (256 - w) + (b & 31) * w) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static inline uint16_t sample(const uint16_t *src, int fx, int fy)   /* Q8 */
{
    int x0 = fx >> 8, y0 = fy >> 8;
    if ((unsigned)x0 >= (unsigned)(W - 1) || (unsigned)y0 >= (unsigned)(H - 1)) return 0;
    const uint16_t *p = src + y0 * W + x0;
    uint16_t c00 = p[0], c10 = p[1], c01 = p[W], c11 = p[W + 1];
    if ((c00 | c10 | c01 | c11) == 0) return 0;
    int wx = fx & 255, wy = fy & 255;
    return blend565(blend565(c00, c10, wx), blend565(c01, c11, wx), wy);
}

void sagwa_clips_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    int front = s_front;
    if (front < 0 || !s_hdr) {
        for (int y = y0; y <= y1; y++) memset(dst + (y - y0) * stride_px, 0, (size_t)(x1 - x0 + 1) * 2);
        return;
    }
    const uint16_t *src = s_buf[front];
    float k = (float)s_target_px / (float)W;
    /* Her feet: where the hubs' ground line falls in the frame */
    float pivot_fx = (float)W * 0.5f, pivot_fy = (float)H * 0.924f;
    float pivot_cx = (float)s_target_px * 0.5f;
    float pivot_cy = (float)s_target_px - (float)H * (1.0f - 0.924f) * k;
    const float c = 1.0f, s = 0.0f;   /* she stands on the screen's floor: no tilt */
    float mir = S.mirror ? -1.0f : 1.0f;
    /* frame = pivot_f + R(-ang) * (canvas - pivot_c) / k, with x mirrored */
    float axx = mir * c / k, axy = mir * s / k;
    float ayx = -s / k, ayy = c / k;
    bool fading = S.fade < 1.0f && s_fade_buf;
    int fw = fading ? (int)(clampf(S.fade, 0.0f, 1.0f) * 256.0f) : 256;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = dst + (y - y0) * stride_px;
        float dy = (float)y - pivot_cy, dx = (float)x0 - pivot_cx;
        int fx = (int)((pivot_fx + axx * dx + axy * dy) * 256.0f);
        int fy = (int)((pivot_fy + ayx * dx + ayy * dy) * 256.0f);
        int sx = (int)(axx * 256.0f), sy = (int)(ayx * 256.0f);
        for (int x = x0; x <= x1; x++, fx += sx, fy += sy) {
            uint16_t px = sample(src, fx, fy);
            if (fading) px = blend565(sample(s_fade_buf, fx, fy), px, fw);
            *row++ = px;
        }
    }
}

int sagwa_clips_get_rig(void)
{
    switch (S.clip) {
    case CL_STAND_WALK: case CL_STAND_STALK: case CL_STALK_STAND: case CL_STALK_IDLE: return RIG_WORK;
    case CL_LOAF_STAND: return RIG_STRETCH;
    case CL_SIT_MEOW: return RIG_SPEAK;
    case CL_SIT_THINK: return RIG_THINK;
    case CL_SIT_PURR: return RIG_PURR;
    case CL_SIT_STARTLE: case CL_STAND_BALANCE: return RIG_TILT;
    default: break;
    }
    return S.hub == HUB_SLEEP ? RIG_SLEEP : S.hub == HUB_LOAF ? RIG_WAKE : S.hub == HUB_STAND ? RIG_IDLE : RIG_LISTEN;
}

/* "is working" -> "working", "Creating image" -> "creating image" */
static void tidy(const char *in, char *out, size_t cap)
{
    if (!strncmp(in, "is ", 3)) in += 3;
    strlcpy(out, in, cap);
    if (out[0] >= 'A' && out[0] <= 'Z' && !(out[1] >= 'A' && out[1] <= 'Z')) out[0] = (char)(out[0] - 'A' + 'a');
}

void sagwa_clips_activity(char *out, size_t cap)
{
    const char *what = NULL;
    char muse[48];
    switch (S.act) {
    case ACT_LISTEN: what = "listening"; break;
    case ACT_THINK: case ACT_SPEAK: case ACT_ERROR:
        /* What Muse's chat says the agent is doing, when it says */
        muse_state_activity(muse, sizeof(muse));
        if (muse[0]) {
            tidy(muse, out, cap);
            return;
        }
        what = S.act == ACT_THINK ? "thinking" : S.act == ACT_SPEAK ? "talking" : "something went wrong";
        break;
    case ACT_PET: what = "purring"; break;
    case ACT_OFF: what = "going to sleep"; break;
    default: break;
    }
    if (!what) {
        switch (S.clip) {
        case CL_SLEEP_LOAF: what = "waking up"; break;
        case CL_LOAF_SLEEP: what = "dozing off"; break;
        case CL_LOAF_SIT: what = "sitting up"; break;
        case CL_SIT_LOAF: what = "lying down"; break;
        case CL_SIT_STAND: what = "getting up"; break;
        case CL_STAND_SIT: what = "sitting down"; break;
        case CL_LOAF_STAND: what = "stretching"; break;
        case CL_STAND_STALK: what = "on the hunt"; break;
        case CL_STALK_STAND: what = "pouncing"; break;
        case CL_STALK_IDLE: what = "stalking"; break;
        case CL_SIT_GROOM: what = "grooming"; break;
        case CL_SIT_MEOW: what = "meowing"; break;
        case CL_SIT_PURR: what = "purring"; break;
        case CL_SIT_THINK: what = "thinking"; break;
        case CL_STAND_WALK: what = "walking"; break;
        case CL_LOAF_LISTEN: what = "listening"; break;
        default:
            what = S.hub == HUB_SLEEP ? "sleeping"
                 : S.hub == HUB_LOAF ? "resting"
                 : S.hub == HUB_STAND ? (S.t < S.wait_until ? "waiting" : "standing")
                 : S.hub == HUB_STALK ? "stalking" : "sitting";
            break;
        }
    }
    strlcpy(out, what, cap);
}

int sagwa_clips_offset_x(void)
{
    return (int)lroundf(S.x.x * (float)s_target_px / (float)W);
}

/* ---- Start-up ---- */

bool sagwa_clips_init(void)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)PART_SUBTYPE, "sagwa");
    if (!part) {
        ESP_LOGW(TAG, "no sagwa partition: using the drawn poses");
        return false;
    }
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "can't map the sagwa partition");
        return false;
    }
    const pack_hdr_t *h = map;
    if (memcmp(h->magic, "SGWC", 4) || h->version != 1 || !h->nclips || !h->nframes ||
        h->data > part->size || h->w > 320 || h->h > 320) {
        ESP_LOGW(TAG, "no clip pack in the sagwa partition: using the drawn poses");
        esp_partition_munmap(handle);
        return false;
    }
    s_base = map;
    s_hdr = h;
    s_clips = (const pack_clip_t *)(s_base + sizeof(pack_hdr_t));
    s_frames = (const pack_frame_t *)(s_base + h->frame_table);
    s_data_len = part->size - h->data;
    if (h->frame_table + (uint64_t)h->nframes * sizeof(pack_frame_t) > h->data ||
        sizeof(pack_hdr_t) + (uint64_t)h->nclips * sizeof(pack_clip_t) > h->frame_table) {
        ESP_LOGW(TAG, "clip pack tables don't fit: using the drawn poses");
        s_hdr = NULL;
        return false;
    }
    W = h->w;
    H = h->h;
    s_fps = h->fps ? h->fps : 12;
    for (int i = 0; i < HUB_COUNT; i++) s_hub_frame[i] = -1;
    for (int c = 0; c < CL_COUNT; c++) {
        s_clip[c] = -1;
        for (int i = 0; i < h->nclips; i++) {
            if (!strncmp(s_clips[i].name, CLIP_NAMES[c], sizeof(s_clips[i].name))) {
                s_clip[c] = i;
                if (s_clips[i].from < HUB_COUNT && s_hub_frame[s_clips[i].from] < 0) {
                    s_hub_frame[s_clips[i].from] = s_clips[i].first;
                }
                if (s_clips[i].to < HUB_COUNT && s_hub_frame[s_clips[i].to] < 0) {
                    s_hub_frame[s_clips[i].to] = s_clips[i].first + s_clips[i].count - 1;
                }
                break;
            }
        }
    }
    for (int i = 0; i < HUB_COUNT; i++) {
        if (s_hub_frame[i] < 0) s_hub_frame[i] = 0;
    }
    for (int i = 0; i < BUFS; i++) {
        s_buf[i] = heap_caps_calloc((size_t)W * H, 2, MALLOC_CAP_SPIRAM);
    }
    s_fade_buf = heap_caps_calloc((size_t)W * H, 2, MALLOC_CAP_SPIRAM);
    if (!s_buf[0] || !s_buf[1] || !s_buf[2] || !s_fade_buf ||
        xTaskCreatePinnedToCoreWithCaps(decoder_task, "sagwa_dec", 4096, NULL, 4, &s_decoder, 0,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "out of memory for the clip player");
        s_hdr = NULL;
        return false;
    }
    int found = 0;
    for (int c = 0; c < CL_COUNT; c++) found += s_clip[c] >= 0;
    ESP_LOGI(TAG, "clip pack: %d of %d clips, %u frames %dx%d @ %d fps", found, CL_COUNT, (unsigned)h->nframes, W, H,
             (int)s_fps);
    return true;
}
