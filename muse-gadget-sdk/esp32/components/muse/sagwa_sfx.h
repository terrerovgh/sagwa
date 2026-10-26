/*
 * Realistic Feline Sound Engine for Sagwa (M5Stack CoreS3)
 *
 * Organic feline vocalizations cut from real cat recordings (sagwa/sounds/,
 * built into sagwa_sfx_data.h by sagwa/tools/make_sfx.py). Each kind of sound
 * has a few takes; one is picked at random (never the same twice running) and
 * played with a slight pitch change, so a kind is recognisable without ever
 * being the exact same clip:
 * - Insistent double meows for something important (repeat until noticed)
 * - Short bright chirps for an ordinary notification
 * - Soft low mrrows while chatting in an ongoing conversation
 * - Continuous touch-reactive purring while petting the screen
 * - Trills, murmurs, yawns, startles and hunting chatter for her body language
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "muse_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAGWA_SFX_NONE = 0,
    SAGWA_SFX_URGENT,           /* Loud insistent double meow: important, calling for attention */
    SAGWA_SFX_NOTIFY,           /* Short bright "brrp?" chirp: an ordinary new message */
    SAGWA_SFX_CHAT,             /* Soft low "mrrow": a reply while talking together */
    SAGWA_SFX_TRILL_LISTEN,     /* Rising "brrr-up!" trill after push-to-talk */
    SAGWA_SFX_MURMUR_ACTIVITY,  /* Soft throat "mrrp" when prowling or thinking */
    SAGWA_SFX_YAWN,             /* Slow low yawn lying down to sleep or stretching */
    SAGWA_SFX_STARTLE,          /* Surprised mew or little hiss */
    SAGWA_SFX_HUNT_CHATTER,     /* "ek-ek-ek" chattering while stalking */
    SAGWA_SFX_HAPPY_TRILL,      /* Happy hello trill: start-up, an update that went well */
    SAGWA_SFX_COUNT
} sagwa_sfx_id_t;

/* Queue a one-shot feline sound to be played by the audio task. */
void sagwa_sfx_trigger(sagwa_sfx_id_t id);

/* Play `id` now and again `times` more, `gap_ms` apart, until she is noticed:
 * a touch, a hand near her, a press, or sagwa_sfx_stop_all(). */
void sagwa_sfx_trigger_repeat(sagwa_sfx_id_t id, int times, uint32_t gap_ms);

/* Stop repeating a call for attention (she has been noticed). */
void sagwa_sfx_noticed(void);

/* Cancel any currently playing one-shot sound or purr (e.g. when PTT is pressed). */
void sagwa_sfx_stop_all(void);

/* Notify that the user is touching/stroking Sagwa; sustains continuous purring. */
void sagwa_sfx_pet_pulse(void);

/* True while continuous purring is active (used to sustain happy/purr pose). */
bool sagwa_sfx_is_purring(void);

/* Quiet hours (night, a dark room): no sounds of her own, only messages. */
void sagwa_sfx_set_quiet(bool quiet);

/* Synchronize feline sounds with autonomous & state-driven rig transitions. */
void sagwa_sfx_on_rig_change(int new_rig, muse_mode_t mode);

/*
 * Pull one 20 ms chunk (320 mono samples @ 16 kHz) of mixed feline audio.
 * Returns true if non-silent samples were written to `out_320`.
 */
bool sagwa_sfx_step(int16_t *out_320);

/* Play a feline sound synchronously in 20 ms chunks on the audio task. */
void sagwa_sfx_play_now(sagwa_sfx_id_t id);

#ifdef __cplusplus
}
#endif
