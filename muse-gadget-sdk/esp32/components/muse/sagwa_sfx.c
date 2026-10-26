/*
 * Realistic Feline Sound Engine for Sagwa (M5Stack CoreS3)
 */

#include "sagwa_sfx.h"
#include "sagwa_sfx_data.h"

#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "muse_audio.h"
#include "muse_settings.h"

/* Rig IDs matching avatar/muse_pixel.c */
#define RIG_IDLE      0
#define RIG_LOOK      1
#define RIG_LISTEN    2
#define RIG_THINK     3
#define RIG_SPEAK     4
#define RIG_WORK      5
#define RIG_CELEBRATE 6
#define RIG_PURR      7
#define RIG_SLEEP     8
#define RIG_WAKE      9
#define RIG_STRETCH   10
#define RIG_TILT      11

/* Each take plays this much higher or lower, at random, so no two are alike */
#define PITCH_SPREAD 0.04f

typedef struct {
    const sagwa_pcm_take_t *takes;
    int n;
} sfx_bank_t;

static const sfx_bank_t BANK[SAGWA_SFX_COUNT] = {
    [SAGWA_SFX_URGENT] = { SAGWA_TAKES_URGENT, SAGWA_TAKES_URGENT_N },
    [SAGWA_SFX_NOTIFY] = { SAGWA_TAKES_NOTIFY, SAGWA_TAKES_NOTIFY_N },
    [SAGWA_SFX_CHAT] = { SAGWA_TAKES_CHAT, SAGWA_TAKES_CHAT_N },
    [SAGWA_SFX_TRILL_LISTEN] = { SAGWA_TAKES_TRILL_LISTEN, SAGWA_TAKES_TRILL_LISTEN_N },
    [SAGWA_SFX_MURMUR_ACTIVITY] = { SAGWA_TAKES_MURMUR, SAGWA_TAKES_MURMUR_N },
    [SAGWA_SFX_YAWN] = { SAGWA_TAKES_YAWN, SAGWA_TAKES_YAWN_N },
    [SAGWA_SFX_STARTLE] = { SAGWA_TAKES_STARTLE, SAGWA_TAKES_STARTLE_N },
    [SAGWA_SFX_HUNT_CHATTER] = { SAGWA_TAKES_HUNT, SAGWA_TAKES_HUNT_N },
    [SAGWA_SFX_HAPPY_TRILL] = { SAGWA_TAKES_HAPPY, SAGWA_TAKES_HAPPY_N },
};

static int8_t s_last_take[SAGWA_SFX_COUNT];   /* take played last, plus one (0: none yet) */

static volatile sagwa_sfx_id_t s_pending_id = SAGWA_SFX_NONE;
static const int16_t *s_oneshot_pcm = NULL;
static size_t s_oneshot_len = 0;
static uint32_t s_oneshot_pos_q16 = 0;    /* read position, 16.16 fixed point */
static uint32_t s_oneshot_step_q16 = 0;   /* 65536 = original pitch */

/* A call for attention repeating until she is noticed */
static volatile sagwa_sfx_id_t s_repeat_id = SAGWA_SFX_NONE;
static volatile int s_repeat_left = 0;
static volatile int64_t s_repeat_next_us = 0;
static uint32_t s_repeat_gap_us = 0;

/* Continuous touch-reactive purring state */
static volatile int64_t s_purr_until_us = 0;
static size_t s_purr_pos = 0;
static float s_purr_env = 0.0f;

/* Cooldowns so autonomous sounds feel organic and never repetitive */
static int64_t s_last_auto_sfx_us = 0;
static volatile bool s_quiet;

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() >> 8) * (1.0f / 16777216.0f);
}

/* A take of `id`, not the one played last time if there's another */
static void lookup_sfx(sagwa_sfx_id_t id, const int16_t **pcm, size_t *len)
{
    *pcm = NULL;
    *len = 0;
    if (id <= SAGWA_SFX_NONE || id >= SAGWA_SFX_COUNT || !BANK[id].n) {
        return;
    }
    const sfx_bank_t *b = &BANK[id];
    int k = (int)(esp_random() % (uint32_t)b->n);
    if (b->n > 1 && k + 1 == s_last_take[id]) {
        k = (k + 1 + (int)(esp_random() % (uint32_t)(b->n - 1))) % b->n;
    }
    s_last_take[id] = (int8_t)(k + 1);
    *pcm = b->takes[k].pcm;
    *len = b->takes[k].len;
}

static void start_oneshot(sagwa_sfx_id_t id)
{
    lookup_sfx(id, &s_oneshot_pcm, &s_oneshot_len);
    s_oneshot_pos_q16 = 0;
    s_oneshot_step_q16 = (uint32_t)(65536.0f * (1.0f + frand(-PITCH_SPREAD, PITCH_SPREAD)));
}

void sagwa_sfx_trigger(sagwa_sfx_id_t id)
{
    if (!muse_settings_speaker_on()) {
        return;
    }
    s_pending_id = id;
}

void sagwa_sfx_trigger_repeat(sagwa_sfx_id_t id, int times, uint32_t gap_ms)
{
    if (!muse_settings_speaker_on()) {
        return;
    }
    s_repeat_gap_us = gap_ms * 1000u;
    s_repeat_next_us = esp_timer_get_time() + (int64_t)s_repeat_gap_us;
    s_repeat_left = times;
    s_repeat_id = id;
    s_pending_id = id;
}

void sagwa_sfx_noticed(void)
{
    s_repeat_left = 0;
    s_repeat_id = SAGWA_SFX_NONE;
}

void sagwa_sfx_stop_all(void)
{
    sagwa_sfx_noticed();
    s_pending_id = SAGWA_SFX_NONE;
    s_oneshot_pcm = NULL;
    s_oneshot_len = 0;
    s_oneshot_pos_q16 = 0;
    s_purr_until_us = 0;
    s_purr_env = 0.0f;
}

void sagwa_sfx_pet_pulse(void)
{
    int64_t now = esp_timer_get_time();
    /* Being touched answers any call for attention */
    sagwa_sfx_noticed();
    /* Sustain continuous purring for 1.5s after each touch/drag sample */
    s_purr_until_us = now + 1500000LL;
}

bool sagwa_sfx_is_purring(void)
{
    return (esp_timer_get_time() < s_purr_until_us) || (s_purr_env > 0.05f);
}

void sagwa_sfx_set_quiet(bool quiet)
{
    s_quiet = quiet;
}

void sagwa_sfx_on_rig_change(int new_rig, muse_mode_t mode)
{
    if (!muse_settings_speaker_on() || muse_state_asleep() || s_quiet) {
        return;
    }
    int64_t now = esp_timer_get_time();

    if (mode == MUSE_MODE_THINKING) {
        if ((new_rig == RIG_WORK || new_rig == RIG_THINK) && (now - s_last_auto_sfx_us > 4000000LL)) {
            s_last_auto_sfx_us = now;
            sagwa_sfx_trigger(SAGWA_SFX_MURMUR_ACTIVITY);
        }
        return;
    }

    if (mode != MUSE_MODE_IDLE || sagwa_sfx_is_purring() || s_oneshot_pcm || s_pending_id != SAGWA_SFX_NONE) {
        return;
    }
    /* Her idle body language: a sound for what she starts doing, not too often */
    sagwa_sfx_id_t id = SAGWA_SFX_NONE;
    int64_t cool = 14000000LL;
    switch (new_rig) {
    /* Her own little call: a soft mrrp, never one of the sounds that mean a message */
    case RIG_SPEAK: id = SAGWA_SFX_MURMUR_ACTIVITY; cool = 8000000LL; break;
    case RIG_STRETCH: id = SAGWA_SFX_YAWN; break;
    case RIG_WORK: id = SAGWA_SFX_HUNT_CHATTER; cool = 25000000LL; break;
    case RIG_TILT: id = SAGWA_SFX_STARTLE; cool = 20000000LL; break;
    case RIG_CELEBRATE: id = SAGWA_SFX_HAPPY_TRILL; break;
    default: break;
    }
    if (id != SAGWA_SFX_NONE && now - s_last_auto_sfx_us > cool) {
        s_last_auto_sfx_us = now;
        sagwa_sfx_trigger(id);
    }
}

bool sagwa_sfx_step(int16_t *out_320)
{
    if (!muse_settings_speaker_on()) {
        s_pending_id = SAGWA_SFX_NONE;
        s_oneshot_pcm = NULL;
        s_purr_env = 0.0f;
        sagwa_sfx_noticed();
        return false;
    }

    int64_t now = esp_timer_get_time();
    if (s_repeat_left > 0 && !s_oneshot_pcm && s_pending_id == SAGWA_SFX_NONE && now >= s_repeat_next_us) {
        s_repeat_left--;
        s_repeat_next_us = now + (int64_t)s_repeat_gap_us;
        s_pending_id = s_repeat_id;
    }

    sagwa_sfx_id_t req = s_pending_id;
    if (req != SAGWA_SFX_NONE) {
        s_pending_id = SAGWA_SFX_NONE;
        start_oneshot(req);
    }

    bool want_purr = (now < s_purr_until_us);
    bool has_oneshot = (s_oneshot_pcm != NULL && (s_oneshot_pos_q16 >> 16) + 1 < s_oneshot_len);

    if (!want_purr && s_purr_env <= 0.001f && !has_oneshot) {
        s_purr_env = 0.0f;
        s_oneshot_pcm = NULL;
        return false;
    }

    /* Smooth per-sample envelope step: 120 ms attack, 380 ms release */
    const float env_up = 1.0f / (0.12f * (float)MUSE_AUDIO_RATE);
    const float env_dn = 1.0f / (0.38f * (float)MUSE_AUDIO_RATE);

    for (size_t i = 0; i < MUSE_AUDIO_CHUNK; i++) {
        int32_t mix = 0;

        if (want_purr) {
            s_purr_env += env_up;
            if (s_purr_env > 1.0f) s_purr_env = 1.0f;
        } else if (s_purr_env > 0.0f) {
            s_purr_env -= env_dn;
            if (s_purr_env < 0.0f) s_purr_env = 0.0f;
        }

        if (s_purr_env > 0.001f) {
            int32_t p = SAGWA_PCM_PURR_LOOP[s_purr_pos];
            s_purr_pos = (s_purr_pos + 1) % SAGWA_PCM_PURR_LOOP_LEN;
            /* Purr ducks under a meow so the meow stays clear */
            float duck = s_oneshot_pcm ? 0.45f : 1.0f;
            mix += (int32_t)((float)p * s_purr_env * duck);
        }

        if (s_oneshot_pcm) {
            size_t k = s_oneshot_pos_q16 >> 16;
            if (k + 1 < s_oneshot_len) {
                int32_t frac = (int32_t)(s_oneshot_pos_q16 & 0xFFFF);
                int32_t a = s_oneshot_pcm[k], b = s_oneshot_pcm[k + 1];
                mix += a + (((b - a) * frac) >> 16);
                s_oneshot_pos_q16 += s_oneshot_step_q16;
            } else {
                s_oneshot_pcm = NULL;
            }
        }

        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out_320[i] = (int16_t)mix;
    }

    if (s_oneshot_pcm && (s_oneshot_pos_q16 >> 16) + 1 >= s_oneshot_len) {
        s_oneshot_pcm = NULL;
        s_oneshot_len = 0;
        s_oneshot_pos_q16 = 0;
    }

    return true;
}

void sagwa_sfx_play_now(sagwa_sfx_id_t id)
{
    if (!muse_settings_speaker_on()) {
        return;
    }
    const int16_t *pcm = NULL;
    size_t len = 0;
    lookup_sfx(id, &pcm, &len);
    if (!pcm || !len) {
        return;
    }
    int16_t chunk[MUSE_AUDIO_CHUNK];
    for (size_t off = 0; off < len; off += MUSE_AUDIO_CHUNK) {
        size_t n = (len - off < MUSE_AUDIO_CHUNK) ? (len - off) : MUSE_AUDIO_CHUNK;
        if (n < MUSE_AUDIO_CHUNK) {
            memset(chunk, 0, sizeof(chunk));
        }
        memcpy(chunk, pcm + off, n * sizeof(int16_t));
        muse_audio_write(chunk, MUSE_AUDIO_CHUNK);
    }
}
