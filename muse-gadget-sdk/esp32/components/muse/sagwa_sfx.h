/*
 * Realistic Feline Sound Engine for Sagwa (M5Stack CoreS3)
 *
 * Replaces human TTS voice and robotic sine-wave chirps with organic feline
 * vocalizations derived from sagwa/sounds/ (meow-original.ogg & purr-original.ogg):
 * - Continuous touch-reactive purring while petting the screen
 * - Attention meows when Sagwa calls for attention
 * - Friendly concise reply meows when text answers appear
 * - Attentive feline trills & soft activity murmurs ("mrrp")
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
    SAGWA_SFX_MEOW_ATTENTION,   /* Full warm meow when calling attention or greeting */
    SAGWA_SFX_MEOW_REPLY,       /* Friendly melodic "mrr-ow!" when showing text reply */
    SAGWA_SFX_TRILL_LISTEN,     /* Rising "brrr-up!" activation trill after push-to-talk */
    SAGWA_SFX_MURMUR_ACTIVITY,  /* Soft throat "mrrp" when stretching, prowling, or thinking */
} sagwa_sfx_id_t;

/* Queue a one-shot feline sound to be played by the audio task. */
void sagwa_sfx_trigger(sagwa_sfx_id_t id);

/* Cancel any currently playing one-shot sound or purr (e.g. when PTT is pressed). */
void sagwa_sfx_stop_all(void);

/* Notify that the user is touching/stroking Sagwa; sustains continuous purring. */
void sagwa_sfx_pet_pulse(void);

/* True while continuous purring is active (used to sustain happy/purr pose). */
bool sagwa_sfx_is_purring(void);

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
