/*
 * What Sagwa can tell you without asking Muse: the time and date (SNTP, kept
 * by the RTC), her sensors (temperature, light) and the weather she fetched
 * last time she was online (cached on the microSD card).
 *
 * Tap her twice, or press to talk while she's offline, and the card shows in
 * her bubble for a few seconds. The update task (main/sagwa_update.c) fetches
 * the weather and passes it in.
 *
 * Also the status of her updates, for the settings: what's running, what's
 * the latest on GitHub, and a way to ask for an update now.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Muse SDK this firmware is built on (the vendored muse-gadget-sdk) */
#define MUSE_SDK_VERSION "2026.10"

typedef struct {
    uint32_t magic;             /* SAGWA_WEATHER_MAGIC when filled in */
    int64_t fetched;            /* time() when fetched */
    char place[40];
    float temp_c;               /* now */
    int code;                   /* WMO weather code, now */
    float max_c[3], min_c[3];   /* today, tomorrow, the day after */
    int day_code[3];
    int32_t utc_offset_s;
} sagwa_weather_t;

#define SAGWA_WEATHER_MAGIC 0x53575731u   /* "SWW1" */

void sagwa_offline_init(void);

/* From the update task, after a fetch: kept in RAM and on the card */
void sagwa_offline_set_weather(const sagwa_weather_t *w);
bool sagwa_offline_weather(sagwa_weather_t *out);

/* WMO weather code in a few words of Spanish (ASCII) */
const char *sagwa_offline_weather_text(int code);

/* The card: time, date, sensors and weather, ASCII for the bubble's font */
void sagwa_offline_card(char *out, size_t cap);

/* Shows the card in her bubble for a few seconds, with a little mrrp */
void sagwa_offline_show(void);

/* ---- Updates, for the settings ---- */

typedef struct {
    char latest[24];        /* latest release on GitHub, "" until checked */
    char state[64];         /* what the updater is doing, or how the last check went */
    int64_t checked;        /* time() of the last check, 0 never */
    bool busy;
} sagwa_update_status_t;

void sagwa_update_status_set(const char *latest, const char *state, bool busy);
void sagwa_update_status_get(sagwa_update_status_t *out);

/* The settings' "check for updates" button: wakes the updater, which checks
 * now and installs right away if there's something newer. */
void sagwa_update_request(void);
/* For the updater: waits up to timeout_ms; true if an update was asked for. */
bool sagwa_update_wait_request(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
