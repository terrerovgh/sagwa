/*
 * How Sagwa announces a message: which kind of cat sound it deserves.
 *
 * - CHAT:   a reply to something you said, or a message that follows on from
 *           a conversation a few minutes old. A soft low "mrrow".
 * - URGENT: something important ("urgente", "alarma", "recordatorio", "!!",
 *           SHOUTING...). A loud insistent double meow, repeated until she is
 *           noticed.
 * - NOTIFY: any other message. A short bright chirp.
 *
 * Pure C with no ESP-IDF dependencies, so it is tested on the host
 * (tests/test_sagwa_notify.py).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAGWA_NOTIFY_NOTIFY = 0,
    SAGWA_NOTIFY_CHAT,
    SAGWA_NOTIFY_URGENT,
} sagwa_notify_kind_t;

/* A message within this long of the last exchange continues the conversation */
#define SAGWA_NOTIFY_CHAT_WINDOW_US (10LL * 60 * 1000000)

/*
 * user_turn: the message answers something the user just said.
 * text: the message so far (UTF-8; may be NULL or empty).
 * now_us, last_exchange_us: monotonic times; last_exchange_us <= 0 if none yet.
 */
sagwa_notify_kind_t sagwa_notify_classify(bool user_turn, const char *text, int64_t now_us,
                                          int64_t last_exchange_us);

const char *sagwa_notify_kind_name(sagwa_notify_kind_t kind);

/* Body language for an announcement, done by the clip renderer when it runs
 * (a weak no-op otherwise). */
void sagwa_clips_cue(sagwa_notify_kind_t kind);

#ifdef __cplusplus
}
#endif
