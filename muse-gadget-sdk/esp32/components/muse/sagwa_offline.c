/*
 * What Sagwa can tell you without asking Muse (sagwa_offline.h).
 */

#include "sagwa_offline.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#include "muse_state.h"
#include "muse_wifi.h"
#include "sagwa_sfx.h"
#include "sagwa_store.h"

#if CONFIG_MUSE_BOARD_M5STACK_CORES3
#include "cores3_sensors.h"
#endif

static const char *TAG = "sagwa_offline";

#define WEATHER_FILE "cache/weather.bin"
#define CARD_SECS 12
#define STALE_S (36 * 3600)   /* older weather than this is too old to show */

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sagwa_weather_t s_weather;
static sagwa_update_status_t s_update;
static char s_card[MUSE_CAPTION_MAX];
static esp_timer_handle_t s_clear_timer;

static StaticEventGroup_t s_events_buf;
static EventGroupHandle_t s_events;
#define EV_UPDATE_NOW BIT0

static EventGroupHandle_t events(void)
{
    static portMUX_TYPE once = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&once);
    if (!s_events) s_events = xEventGroupCreateStatic(&s_events_buf);
    taskEXIT_CRITICAL(&once);
    return s_events;
}

static void clear_card(void *arg)
{
    (void)arg;
    char now[MUSE_CAPTION_MAX];
    uint32_t version = 0;
    muse_state_caption(now, sizeof(now), &version);
    if (!strcmp(now, s_card) && muse_state_mode(NULL) == MUSE_MODE_IDLE) {
        muse_state_set_caption("%s", "");
    }
}

void sagwa_offline_init(void)
{
    events();
    sagwa_weather_t w;
    if (sagwa_store_read(WEATHER_FILE, &w, sizeof(w)) == sizeof(w) && w.magic == SAGWA_WEATHER_MAGIC) {
        taskENTER_CRITICAL(&s_lock);
        s_weather = w;
        taskEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "weather for %s from the card", w.place);
    }
    const esp_timer_create_args_t args = { .callback = clear_card, .name = "sagwa_card" };
    esp_timer_create(&args, &s_clear_timer);
}

void sagwa_offline_set_weather(const sagwa_weather_t *w)
{
    taskENTER_CRITICAL(&s_lock);
    s_weather = *w;
    s_weather.magic = SAGWA_WEATHER_MAGIC;
    sagwa_weather_t copy = s_weather;
    taskEXIT_CRITICAL(&s_lock);
    sagwa_store_write(WEATHER_FILE, &copy, sizeof(copy));
}

bool sagwa_offline_weather(sagwa_weather_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_weather;
    taskEXIT_CRITICAL(&s_lock);
    return out->magic == SAGWA_WEATHER_MAGIC;
}

const char *sagwa_offline_weather_text(int code)
{
    if (code == 0) return "despejado";
    if (code <= 2) return "algo nublado";
    if (code == 3) return "nublado";
    if (code <= 48) return "niebla";
    if (code <= 57) return "llovizna";
    if (code <= 67) return "lluvia";
    if (code <= 77) return "nieve";
    if (code <= 82) return "chubascos";
    if (code <= 86) return "chubascos de nieve";
    return "tormenta";
}

void sagwa_offline_card(char *out, size_t cap)
{
    static const char *const DAYS[] = { "Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado" };
    static const char *const MONTHS[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                                          "agosto", "septiembre", "octubre", "noviembre", "diciembre" };
    size_t n = 0;
    out[0] = '\0';
#define ADD(...) (n += (n < cap) ? (size_t)snprintf(out + n, cap - n, __VA_ARGS__) : 0)
    time_t now = time(NULL);
    if (now > 1735689600) {
        struct tm tm;
        localtime_r(&now, &tm);
        ADD("%02d:%02d  %s %d de %s\n", tm.tm_hour, tm.tm_min, DAYS[tm.tm_wday], tm.tm_mday, MONTHS[tm.tm_mon]);
    } else {
        ADD("Aun no se la hora (sin internet desde que encendi)\n");
    }
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    cores3_sensors_snapshot_t s;
    cores3_sensors_read(&s);
    if (s.imu_valid || s.als_valid) {
        ADD("Aqui: ");
        if (s.imu_valid) ADD("%.0f C", (double)s.temperature_c);
        if (s.imu_valid && s.als_valid) ADD(", ");
        if (s.als_valid) ADD("%.0f lux", (double)s.illuminance_lux);
        ADD("\n");
    }
#endif
    sagwa_weather_t w;
    if (sagwa_offline_weather(&w) && now > 1735689600 && now - w.fetched < STALE_S) {
        int ago_h = (int)((now - w.fetched) / 3600);
        ADD("%s: %.0f C, %s", w.place[0] ? w.place : "Fuera", (double)w.temp_c, sagwa_offline_weather_text(w.code));
        if (ago_h >= 1) ADD(" (hace %d h)", ago_h);
        ADD("\nHoy %.0f/%.0f C, manana %.0f/%.0f C %s", (double)w.max_c[0], (double)w.min_c[0], (double)w.max_c[1],
            (double)w.min_c[1], sagwa_offline_weather_text(w.day_code[1]));
    } else {
        ADD("Sin datos del clima todavia");
    }
    if (!muse_wifi_connected()) {
        ADD("\nSin conexion");
    }
#undef ADD
}

void sagwa_offline_show(void)
{
    sagwa_offline_card(s_card, sizeof(s_card));
    muse_state_set_caption("%s", s_card);
    sagwa_sfx_trigger(SAGWA_SFX_MURMUR_ACTIVITY);
    if (s_clear_timer) {
        esp_timer_stop(s_clear_timer);
        esp_timer_start_once(s_clear_timer, CARD_SECS * 1000000LL);
    }
}

/* ---- Updates ---- */

void sagwa_update_status_set(const char *latest, const char *state, bool busy)
{
    taskENTER_CRITICAL(&s_lock);
    if (latest) {
        strlcpy(s_update.latest, latest, sizeof(s_update.latest));
        s_update.checked = time(NULL);
    }
    if (state) strlcpy(s_update.state, state, sizeof(s_update.state));
    s_update.busy = busy;
    taskEXIT_CRITICAL(&s_lock);
}

void sagwa_update_status_get(sagwa_update_status_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_update;
    taskEXIT_CRITICAL(&s_lock);
}

void sagwa_update_request(void)
{
    xEventGroupSetBits(events(), EV_UPDATE_NOW);
}

bool sagwa_update_wait_request(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(events(), EV_UPDATE_NOW, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (b & EV_UPDATE_NOW) != 0;
}
