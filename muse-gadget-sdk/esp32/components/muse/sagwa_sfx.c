/*
 * Realistic Feline Sound Engine for Sagwa (M5Stack CoreS3)
 */

#include "sagwa_sfx.h"
#include "sagwa_sfx_data.h"

#include <string.h>
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

static volatile sagwa_sfx_id_t s_pending_id = SAGWA_SFX_NONE;
static const int16_t *s_oneshot_pcm = NULL;
static size_t s_oneshot_len = 0;
static size_t s_oneshot_pos = 0;

/* Continuous touch-reactive purring state */
static volatile int64_t s_purr_until_us = 0;
static size_t s_purr_pos = 0;
static float s_purr_env = 0.0f;

/* Cooldowns so autonomous sounds feel organic and never repetitive */
static int64_t s_last_auto_sfx_us = 0;

static void lookup_sfx(sagwa_sfx_id_t id, const int16_t **pcm, size_t *len)
{
    switch (id) {
    case SAGWA_SFX_MEOW_ATTENTION:
        *pcm = SAGWA_PCM_MEOW_ATTENTION;
        *len = SAGWA_PCM_MEOW_ATTENTION_LEN;
        break;
    case SAGWA_SFX_MEOW_REPLY:
        *pcm = SAGWA_PCM_MEOW_REPLY;
        *len = SAGWA_PCM_MEOW_REPLY_LEN;
        break;
    case SAGWA_SFX_TRILL_LISTEN:
        *pcm = SAGWA_PCM_TRILL_LISTEN;
        *len = SAGWA_PCM_TRILL_LISTEN_LEN;
        break;
    case SAGWA_SFX_MURMUR_ACTIVITY:
        *pcm = SAGWA_PCM_MURMUR_ACTIVITY;
        *len = SAGWA_PCM_MURMUR_ACTIVITY_LEN;
        break;
    default:
        *pcm = NULL;
        *len = 0;
        break;
    }
}

void sagwa_sfx_trigger(sagwa_sfx_id_t id)
{
    if (!muse_settings_speaker_on()) {
        return;
    }
    s_pending_id = id;
}

void sagwa_sfx_stop_all(void)
{
    s_pending_id = SAGWA_SFX_NONE;
    s_oneshot_pcm = NULL;
    s_oneshot_len = 0;
    s_oneshot_pos = 0;
    s_purr_until_us = 0;
    s_purr_env = 0.0f;
}

void sagwa_sfx_pet_pulse(void)
{
    int64_t now = esp_timer_get_time();
    /* Sustain continuous purring for 1.5s after each touch/drag sample */
    s_purr_until_us = now + 1500000LL;
}

bool sagwa_sfx_is_purring(void)
{
    return (esp_timer_get_time() < s_purr_until_us) || (s_purr_env > 0.05f);
}

void sagwa_sfx_on_rig_change(int new_rig, muse_mode_t mode)
{
    if (!muse_settings_speaker_on() || muse_state_asleep()) {
        return;
    }
    int64_t now = esp_timer_get_time();

    if (mode == MUSE_MODE_THINKING) {
        if ((new_rig == RIG_WORK || new_rig == RIG_THINK) && (now - s_last_auto_sfx_us > 2500000LL)) {
            s_last_auto_sfx_us = now;
            sagwa_sfx_trigger(SAGWA_SFX_MURMUR_ACTIVITY);
        }
        return;
    }

    if (mode == MUSE_MODE_IDLE && !sagwa_sfx_is_purring()) {
        if (new_rig == RIG_SPEAK && (now - s_last_auto_sfx_us > 8000000LL)) {
            /* Sagwa is calling for attention in her idle cycle */
            s_last_auto_sfx_us = now;
            sagwa_sfx_trigger(SAGWA_SFX_MEOW_ATTENTION);
        } else if ((new_rig == RIG_STRETCH || new_rig == RIG_CELEBRATE || new_rig == RIG_WORK) &&
                   (now - s_last_auto_sfx_us > 14000000LL)) {
            /* Subtle feline activity sound ("mrrp") when stretching, waving paw, or prowling */
            s_last_auto_sfx_us = now;
            sagwa_sfx_trigger(SAGWA_SFX_MURMUR_ACTIVITY);
        }
    }
}

bool sagwa_sfx_step(int16_t *out_320)
{
    if (!muse_settings_speaker_on()) {
        s_pending_id = SAGWA_SFX_NONE;
        s_oneshot_pcm = NULL;
        s_purr_env = 0.0f;
        return false;
    }

    sagwa_sfx_id_t req = s_pending_id;
    if (req != SAGWA_SFX_NONE) {
        s_pending_id = SAGWA_SFX_NONE;
        lookup_sfx(req, &s_oneshot_pcm, &s_oneshot_len);
        s_oneshot_pos = 0;
    }

    bool want_purr = (esp_timer_get_time() < s_purr_until_us);
    bool has_oneshot = (s_oneshot_pcm != NULL && s_oneshot_pos < s_oneshot_len);

    if (!want_purr && s_purr_env <= 0.001f && !has_oneshot) {
        s_purr_env = 0.0f;
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
            mix += (int32_t)((float)p * s_purr_env);
        }

        if (s_oneshot_pcm && s_oneshot_pos < s_oneshot_len) {
            mix += s_oneshot_pcm[s_oneshot_pos++];
        }

        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out_320[i] = (int16_t)mix;
    }

    if (s_oneshot_pcm && s_oneshot_pos >= s_oneshot_len) {
        s_oneshot_pcm = NULL;
        s_oneshot_len = 0;
        s_oneshot_pos = 0;
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
