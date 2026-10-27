/*
 * Sagwa updates herself from her GitHub repository's releases (sagwa_update.h).
 */

#include "sagwa_update.h"

#include "sdkconfig.h"

#if CONFIG_MUSE_ENABLED
#include "sagwa_offline.h"
#include "sagwa_store.h"
#endif

#if CONFIG_SAGWA_AUTO_UPDATE

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"

#include "config_store.h"
#include "muse_state.h"
#include "ota.h"
#include "sagwa_offline.h"
#include "sagwa_sfx.h"
#include "sagwa_store.h"
#include "wifi_mgr.h"

#if CONFIG_MUSE_BOARD_M5STACK_CORES3
#include "cores3_rtc.h"
#endif

static const char *TAG = "link.sagwa_update";

#define RELEASES "https://github.com/" CONFIG_SAGWA_UPDATE_REPO "/releases/latest/download/"
#define CLIPS_PART_SUBTYPE 0x50

#define FIRST_CHECK_S 120          /* after boot, once Wi-Fi is up */
#define VERIFY_CHECK_S 15          /* sooner on the boot after an update: it proves the image */
#define JITTER_S (15 * 60)
#define RETRY_S (20 * 60)          /* GitHub or Wi-Fi out of reach */
#define QUIET_S 120                /* nobody has touched or talked to her for this long */
#define COUNTDOWN_S 30
#define POSTPONE_S (10 * 60)       /* touched during the countdown */
#define MIN_BATTERY_PCT 30
#define MANIFEST_MAX 4096        /* also the weather's JSON */
#define WEATHER_EVERY_S (60 * 60)
#define WEATHER_RETRY_S (15 * 60)
#define STAGED_CLIPS "update/sagwa_clips.bin"
#define HTTP_TIMEOUT_MS 30000
#define ERASE_STEP (64 * 1024)

/* NVS (config_store) keys, at most 15 characters */
#define KEY_CLIPS_SHA "sgw_clips_sha"
#define KEY_FROM "sgw_upd_from"
#define KEY_TO "sgw_upd_to"
#define KEY_NOTES "sgw_upd_notes"
#define KEY_TZ "sgw_tz"          /* the time zone the weather service gave */
#define KEY_GEO "sgw_geo"        /* "lat,lon,place" from IP geolocation */

typedef struct {
    char version[32];
    char clips_sha[65];
    uint32_t clips_size;
    char notes[121];
} manifest_t;

static volatile bool s_healthy;
static bool s_sntp;

/* The clip player's, when it is built (components/muse/avatar/sagwa_clips.c) */
__attribute__((weak)) void sagwa_clips_suspend(void)
{
}

bool sagwa_update_healthy(void)
{
    return s_healthy;
}

static const char *running_version(void)
{
    static char v[sizeof(esp_app_get_description()->version) + 1];
    if (!v[0]) {
        memcpy(v, esp_app_get_description()->version, sizeof(v) - 1);
    }
    return v;
}

/* Like ota.c: "[v]MAJOR.MINOR.PATCH", numerically */
static bool version_newer(const char *cand, const char *cur)
{
    long a[3] = { 0 }, b[3] = { 0 };
    const char *s[2] = { cand, cur };
    long *o[2] = { a, b };
    for (int k = 0; k < 2; k++) {
        const char *p = s[k];
        if (*p == 'v' || *p == 'V') p++;
        for (int i = 0; i < 3 && *p; i++) {
            char *end;
            o[k][i] = strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            if (*p == '.') p++;
            else break;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

/* ---- Talking to her people ---- */

static void say(const char *text)
{
    muse_state_set_caption("%s", text);
}

static void say_clear_later(const char *text, int secs)
{
    say(text);
    vTaskDelay(pdMS_TO_TICKS(secs * 1000));
    char now[MUSE_CAPTION_MAX];
    uint32_t version = 0;
    muse_state_caption(now, sizeof(now), &version);
    if (!strcmp(now, text) && muse_state_mode(NULL) == MUSE_MODE_IDLE) {
        say("");   /* still ours: tidy it away */
    }
}

/* On the boot after an update: whether it took */
static void report_last_update(void)
{
    char to[32] = "", from[32] = "", notes[121] = "";
    if (!config_get_str(KEY_TO, to, sizeof(to))) return;
    config_get_str(KEY_FROM, from, sizeof(from));
    config_get_str(KEY_NOTES, notes, sizeof(notes));
    config_erase_key(KEY_TO);
    config_erase_key(KEY_FROM);
    config_erase_key(KEY_NOTES);

    char msg[MUSE_CAPTION_MAX];
    muse_state_set_asleep(false);
    if (!strcmp(to, running_version())) {
        ESP_LOGI(TAG, "updated %s -> %s", from, to);
        if (strcmp(from, to)) {
            snprintf(msg, sizeof(msg), "Ya me actualice! v%s -> v%s%s%s", from, to, notes[0] ? "\n" : "", notes);
        } else {
            snprintf(msg, sizeof(msg), "Ya me actualice! Animaciones nuevas en v%s%s%s", to, notes[0] ? "\n" : "",
                     notes);
        }
        sagwa_sfx_trigger(SAGWA_SFX_HAPPY_TRILL);
        /* It took: the downloads staged for it can go */
        sagwa_store_clear("update");
        ESP_LOGI(TAG, "cleaned up after the update");
    } else {
        ESP_LOGW(TAG, "update to %s didn't take: running %s", to, running_version());
        snprintf(msg, sizeof(msg), "La actualizacion a v%s fallo. Sigo en v%s.", to, running_version());
        sagwa_sfx_trigger(SAGWA_SFX_STARTLE);
    }
    say_clear_later(msg, 45);
}

/* ---- HTTP ---- */

typedef struct {
    char *buf;
    size_t cap, len;
    bool overflow;
} body_t;

static esp_err_t on_manifest_data(esp_http_client_event_t *evt)
{
    body_t *b = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }
    if (b->len + evt->data_len >= b->cap) {
        b->overflow = true;
        return ESP_OK;
    }
    memcpy(b->buf + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    b->buf[b->len] = '\0';
    return ESP_OK;
}

static esp_http_client_config_t http_config(const char *url, http_event_handle_cb handler, void *user)
{
    return (esp_http_client_config_t){
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "Sagwa-Updater",
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_redirection_count = 5,
        .buffer_size = 4096,      /* GitHub's redirect headers are long */
        .buffer_size_tx = 4096,   /* and so is the URL they send us on to */
        .event_handler = handler,
        .user_data = user,
    };
}

static void copy_str(cJSON *root, const char *key, char *out, size_t cap)
{
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(root, key));
    strlcpy(out, v ? v : "", cap);
}

/* A small GET (JSON) into buf, NUL-terminated */
static bool http_get(const char *url, char *buf, size_t cap)
{
    body_t body = { .buf = buf, .cap = cap };
    buf[0] = '\0';
    esp_http_client_config_t cfg = http_config(url, on_manifest_data, &body);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status != 200 || body.overflow) {
        ESP_LOGW(TAG, "%s: %s, HTTP %d%s", url, esp_err_to_name(err), status, body.overflow ? ", too big" : "");
        return false;
    }
    return true;
}

static char s_buf[MANIFEST_MAX];

static bool fetch_manifest(manifest_t *m)
{
    char *buf = s_buf;
    if (!http_get(RELEASES "manifest.json", buf, sizeof(s_buf))) return false;
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGW(TAG, "manifest: not JSON");
        return false;
    }
    memset(m, 0, sizeof(*m));
    copy_str(root, "version", m->version, sizeof(m->version));
    copy_str(root, "clips_sha256", m->clips_sha, sizeof(m->clips_sha));
    copy_str(root, "notes", m->notes, sizeof(m->notes));
    cJSON *size = cJSON_GetObjectItem(root, "clips_size");
    m->clips_size = cJSON_IsNumber(size) && size->valuedouble > 0 ? (uint32_t)size->valuedouble : 0;
    cJSON_Delete(root);
    /* The notes go on a screen with an ASCII font: keep the first line, printable */
    for (char *p = m->notes; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) *p = ' ';
    }
    if (!m->version[0]) {
        ESP_LOGW(TAG, "manifest: no version");
        return false;
    }
    return true;
}

/* ---- The clip pack ---- */

static void hex(const uint8_t *h, char out[65])
{
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

static const esp_partition_t *clips_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)CLIPS_PART_SUBTYPE, "sagwa");
}

/* SHA-256 of the first `size` bytes of the clip partition, remembered */
static bool clips_sha_now(uint32_t size, char out[65])
{
    char key_val[80];
    if (config_get_str(KEY_CLIPS_SHA, key_val, sizeof(key_val))) {
        /* "<size>:<sha>" */
        char *colon = strchr(key_val, ':');
        if (colon && strtoul(key_val, NULL, 10) == size && strlen(colon + 1) == 64) {
            strlcpy(out, colon + 1, 65);
            return true;
        }
    }
    const esp_partition_t *p = clips_partition();
    if (!p || size == 0 || size > p->size) return false;
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    uint8_t *chunk = malloc(4096);
    bool ok = chunk && md && mbedtls_md_setup(&ctx, md, 0) == 0 && mbedtls_md_starts(&ctx) == 0;
    for (uint32_t off = 0; ok && off < size; off += 4096) {
        uint32_t n = size - off < 4096 ? size - off : 4096;
        ok = esp_partition_read(p, off, chunk, n) == ESP_OK && mbedtls_md_update(&ctx, chunk, n) == 0;
        if ((off & 0xFFFFF) == 0) vTaskDelay(1);
    }
    uint8_t h[32];
    ok = ok && mbedtls_md_finish(&ctx, h) == 0;
    mbedtls_md_free(&ctx);
    free(chunk);
    if (!ok) return false;
    hex(h, out);
    snprintf(key_val, sizeof(key_val), "%" PRIu32 ":%s", size, out);
    config_set_str(KEY_CLIPS_SHA, key_val);
    return true;
}

typedef struct {
    const esp_partition_t *part;
    FILE *f;                  /* staging on the microSD instead of the partition */
    uint32_t want, written, erased;
    mbedtls_md_context_t md;
    bool failed;
    int last_pct;
} clips_dl_t;

static esp_err_t on_clips_data(esp_http_client_event_t *evt)
{
    clips_dl_t *d = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || d->failed || esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }
    uint32_t n = (uint32_t)evt->data_len;
    if (d->written + n > d->want) {
        ESP_LOGE(TAG, "clips: more than the %" PRIu32 " bytes promised", d->want);
        d->failed = true;
        return ESP_FAIL;
    }
    if (d->f) {
        sagwa_store_lock();
        size_t put = fwrite(evt->data, 1, n, d->f);
        sagwa_store_unlock();
        if (put != n) {
            d->failed = true;
            return ESP_FAIL;
        }
    } else {
        while (d->erased < d->written + n) {
            if (esp_partition_erase_range(d->part, d->erased, ERASE_STEP) != ESP_OK) {
                d->failed = true;
                return ESP_FAIL;
            }
            d->erased += ERASE_STEP;
        }
        if (esp_partition_write(d->part, d->written, evt->data, n) != ESP_OK) {
            d->failed = true;
            return ESP_FAIL;
        }
    }
    if (mbedtls_md_update(&d->md, evt->data, n) != 0) {
        d->failed = true;
        return ESP_FAIL;
    }
    d->written += n;
    int pct = (int)((uint64_t)d->written * 100 / d->want);
    if (pct / 5 != d->last_pct / 5) {
        d->last_pct = pct;
        muse_state_set_caption("Actualizando mis animaciones... %d%%", pct);
    }
    return ESP_OK;
}

/* Copies the staged, checked pack from the card into the partition */
static bool install_staged_clips(const esp_partition_t *p, const char *path, uint32_t size)
{
    sagwa_store_lock();
    FILE *f = fopen(path, "rb");
    sagwa_store_unlock();
    uint8_t *chunk = malloc(4096);
    bool ok = f && chunk && esp_partition_erase_range(p, 0, (size + ERASE_STEP - 1) / ERASE_STEP * ERASE_STEP) == ESP_OK;
    int last_pct = -5;
    for (uint32_t off = 0; ok && off < size; off += 4096) {
        uint32_t n = size - off < 4096 ? size - off : 4096;
        sagwa_store_lock();
        ok = fread(chunk, 1, n, f) == n;
        sagwa_store_unlock();
        ok = ok && esp_partition_write(p, off, chunk, n) == ESP_OK;   /* internal flash: not the shared bus */
        int pct = (int)((uint64_t)off * 100 / size);
        if (pct / 10 != last_pct / 10) {
            last_pct = pct;
            muse_state_set_caption("Instalando mis animaciones... %d%%", pct);
        }
    }
    if (f) {
        sagwa_store_lock();
        fclose(f);
        sagwa_store_unlock();
    }
    free(chunk);
    return ok;
}

/* Puts the release's clip pack in the partition. With a microSD card it is
 * downloaded and checked there first (she keeps playing meanwhile) and the
 * partition is only touched once it's good; without one it streams straight
 * in. *touched says whether the partition changed (a restart must follow). */
static bool download_clips(const manifest_t *m, bool *touched)
{
    *touched = false;
    const esp_partition_t *p = clips_partition();
    if (!p || !m->clips_size || m->clips_size > p->size || (p->size % ERASE_STEP)) {
        ESP_LOGE(TAG, "clips: no room (%" PRIu32 " bytes)", m->clips_size);
        return false;
    }
    char staged[128];
    bool stage = sagwa_store_path(STAGED_CLIPS, staged, sizeof(staged));
    clips_dl_t d = { .part = p, .want = m->clips_size, .last_pct = -5 };
    if (stage) {
        sagwa_store_lock();
        d.f = fopen(staged, "wb");
        sagwa_store_unlock();
        stage = d.f != NULL;
    }
    if (!stage) {
        sagwa_clips_suspend();
        *touched = true;
    }
    mbedtls_md_init(&d.md);
    if (mbedtls_md_setup(&d.md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
        mbedtls_md_starts(&d.md) != 0) {
        mbedtls_md_free(&d.md);
        return false;
    }
    esp_http_client_config_t cfg = http_config(RELEASES "sagwa_clips.bin", on_clips_data, &d);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = c ? esp_http_client_perform(c) : ESP_FAIL;
    int status = c ? esp_http_client_get_status_code(c) : 0;
    if (c) esp_http_client_cleanup(c);
    uint8_t h[32];
    char got[65] = "";
    if (mbedtls_md_finish(&d.md, h) == 0) hex(h, got);
    mbedtls_md_free(&d.md);
    if (d.f) {
        sagwa_store_lock();
        fclose(d.f);
        sagwa_store_unlock();
    }
    if (err != ESP_OK || status != 200 || d.failed || d.written != d.want || strcmp(got, m->clips_sha)) {
        ESP_LOGE(TAG, "clips: %s, HTTP %d, %" PRIu32 "/%" PRIu32 " bytes, sha %s", esp_err_to_name(err), status,
                 d.written, d.want, got);
        if (stage) sagwa_store_clear("update");
        if (*touched) config_erase_key(KEY_CLIPS_SHA);
        return false;
    }
    if (stage) {
        ESP_LOGI(TAG, "clips: checked on the card, installing");
        sagwa_clips_suspend();
        *touched = true;
        config_erase_key(KEY_CLIPS_SHA);
        if (!install_staged_clips(p, staged, m->clips_size)) {
            ESP_LOGE(TAG, "clips: couldn't copy them from the card");
            return false;
        }
    }
    char key_val[80];
    snprintf(key_val, sizeof(key_val), "%" PRIu32 ":%s", m->clips_size, got);
    config_set_str(KEY_CLIPS_SHA, key_val);
    ESP_LOGI(TAG, "clips: %" PRIu32 " bytes written, sha ok", d.written);
    return true;
}

/* ---- The firmware ---- */

static volatile int s_ota_result = -1;

static void on_ota(const ota_event_t *ev, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "firmware: %s (%s)", ev->result == OTA_RESULT_APPLIED ? "applied" : ev->result == OTA_RESULT_SKIPPED
                                                                                  ? "skipped" : "failed",
             ev->detail ? ev->detail : "");
    if (ev->result == OTA_RESULT_APPLIED) {
        say("Listo! Reiniciando...");
    }
    s_ota_result = (int)ev->result;   /* on success ota.c restarts right after */
}

/* ---- Choosing the moment ---- */

/* Free to update: nobody talking with her or touching her for a while, and
 * enough battery if she's on it */
static bool free_now(void)
{
    muse_power_t pw = muse_state_power();
    if (muse_state_on_battery() && pw.battery_pct >= 0 && pw.battery_pct < MIN_BATTERY_PCT) {
        return false;
    }
    return muse_state_mode(NULL) == MUSE_MODE_IDLE && muse_state_idle_secs() >= QUIET_S;
}

/* Tells you she's about to update and counts down; false if you touched her */
static bool countdown(const manifest_t *m, bool fw, bool clips)
{
    muse_state_set_asleep(false);   /* wakes the screen (and pokes) */
    sagwa_sfx_trigger(SAGWA_SFX_NOTIFY);
    int64_t t0 = esp_timer_get_time();
    char msg[MUSE_CAPTION_MAX];
    for (int left = COUNTDOWN_S; left > 0; left--) {
        snprintf(msg, sizeof(msg), "Hay una version nueva de mi: v%s%s%s%s\nMe actualizo en %d s. Tocame para esperar.",
                 m->version, fw && clips ? " (con animaciones nuevas)" : !fw ? " (animaciones nuevas)" : "",
                 m->notes[0] ? "\n" : "", m->notes, left);
        say(msg);
        vTaskDelay(pdMS_TO_TICKS(1000));
        float since = (float)(esp_timer_get_time() - t0) / 1e6f;
        if (muse_state_idle_secs() + 1.5f < since || muse_state_mode(NULL) != MUSE_MODE_IDLE) {
            ESP_LOGI(TAG, "touched during the countdown: later");
            say_clear_later("Vale, me actualizo mas tarde.", 8);
            return false;
        }
    }
    return true;
}

/* ---- The check ---- */

/* Returns seconds until the next check, or -1 to check again now as asked
 * for from the settings. manual: asked for from the settings,
 * so no waiting for a quiet moment and no countdown to put it off. */
static int check_once(bool manual)
{
    manifest_t m;
    sagwa_update_status_set(NULL, "Checking...", true);
    if (!fetch_manifest(&m)) {
        sagwa_update_status_set(NULL, "Couldn't reach GitHub", false);
        return RETRY_S;
    }
    s_healthy = true;
    ESP_LOGI(TAG, "stack left after the manifest: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));

    const char *running = running_version();
    bool fw = version_newer(m.version, running);
    char have[65] = "";
    bool clips = m.clips_sha[0] && m.clips_size && clips_sha_now(m.clips_size, have) && strcmp(have, m.clips_sha);
    ESP_LOGI(TAG, "latest v%s, running v%s: firmware %s, clips %s%s", m.version, running, fw ? "newer" : "same",
             clips ? "changed" : "same", manual ? " (asked for)" : "");
    if (!fw && !clips) {
        sagwa_update_status_set(m.version, "Up to date", false);
        if (manual) say_clear_later("Ya tengo la ultima version.", 6);
        return CONFIG_SAGWA_UPDATE_INTERVAL_MIN * 60;
    }
    char state[64];
    snprintf(state, sizeof(state), "v%s available", m.version);
    sagwa_update_status_set(m.version, state, true);

    if (manual) {
        muse_state_set_asleep(false);
        sagwa_sfx_trigger(SAGWA_SFX_NOTIFY);
        char msg[MUSE_CAPTION_MAX];
        snprintf(msg, sizeof(msg), "Me actualizo a v%s ahora mismo.%s%s", m.version, m.notes[0] ? "\n" : "", m.notes);
        say(msg);
        vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
        /* Wait for a quiet moment (checking back every 30 s, up to a few hours) */
        int64_t until = esp_timer_get_time() + (int64_t)CONFIG_SAGWA_UPDATE_INTERVAL_MIN * 60 * 1000000;
        while (!free_now()) {
            if (esp_timer_get_time() > until) return 60;
            if (sagwa_update_wait_request(30000)) return -1;   /* asked for meanwhile: go now */
        }
        if (!countdown(&m, fw, clips)) {
            sagwa_update_status_set(NULL, "Put off 10 min", false);
            return POSTPONE_S;
        }
    }
    snprintf(state, sizeof(state), "Installing v%s...", m.version);
    sagwa_update_status_set(NULL, state, true);

    config_set_str(KEY_FROM, running);
    config_set_str(KEY_TO, fw ? m.version : running);
    config_set_str(KEY_NOTES, m.notes);

    if (clips) {
        say("Actualizando mis animaciones...");
        bool touched;
        if (!download_clips(&m, &touched)) {
            config_erase_key(KEY_TO);
            sagwa_sfx_trigger(SAGWA_SFX_STARTLE);
            sagwa_update_status_set(NULL, "Clip download failed", false);
            if (!touched) {
                say_clear_later("No pude bajar las animaciones nuevas. Lo intento mas tarde.", 15);
                return RETRY_S;
            }
            say("No pude bajar las animaciones nuevas. Reinicio y lo intento luego.");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();   /* the pack in the partition may be half written */
        }
    }
    if (fw) {
        say("Descargando la nueva version...");
        s_ota_result = -1;
        /* Here on this task: by now there's no internal RAM for ota_start's own */
        ota_run(RELEASES "sagwa-cores3.bin", false, on_ota, NULL);
        ESP_LOGI(TAG, "stack left after the download: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (s_ota_result != OTA_RESULT_APPLIED) {
            config_erase_key(KEY_TO);
            sagwa_sfx_trigger(SAGWA_SFX_STARTLE);
            sagwa_update_status_set(NULL, "Install failed", false);
            if (clips) {
                say("La nueva version no se pudo instalar. Reinicio con las animaciones nuevas.");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
            }
            say_clear_later("La nueva version no se pudo instalar. Lo intento mas tarde.", 20);
            return RETRY_S;
        }
        vTaskDelay(portMAX_DELAY);   /* ota.c restarts */
    }
    say("Listo! Reiniciando...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return 0;
}

/* ---- The clock ---- */

static void apply_tz(void)
{
    char tz[24] = "";
    /* A zone set in the build wins; otherwise the one the weather service gave */
    if (strcmp(CONFIG_SAGWA_TZ, "UTC0") || !config_get_str(KEY_TZ, tz, sizeof(tz))) {
        strlcpy(tz, CONFIG_SAGWA_TZ, sizeof(tz));
    }
    setenv("TZ", tz, 1);
    tzset();
}

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "clock synced");
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    cores3_rtc_save_now();   /* so she knows the time after a restart offline */
#endif
}

static void start_sntp(void)
{
    if (s_sntp) return;
    s_sntp = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_time_sync;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP didn't start");
    }
}

/* ---- The weather, kept for when she's offline ---- */

/* Where she is, roughly, from her IP address; remembered */
static bool locate(float *lat, float *lon, char *place, size_t cap)
{
    static bool fresh;   /* looked up once per boot, in case she moved */
    char geo[96];
    if (fresh && config_get_str(KEY_GEO, geo, sizeof(geo))) {
        char *a = strchr(geo, ','), *b = a ? strchr(a + 1, ',') : NULL;
        if (a && b) {
            *lat = strtof(geo, NULL);
            *lon = strtof(a + 1, NULL);
            strlcpy(place, b + 1, cap);
            return true;
        }
    }
    /* Plain HTTP: ip-api's free tier has no TLS, and a rough place isn't a secret */
    if (!http_get("http://ip-api.com/json/?fields=status,lat,lon,city", s_buf, sizeof(s_buf))) return false;
    cJSON *root = cJSON_Parse(s_buf);
    cJSON *la = cJSON_GetObjectItem(root, "lat"), *lo = cJSON_GetObjectItem(root, "lon");
    const char *city = cJSON_GetStringValue(cJSON_GetObjectItem(root, "city"));
    bool ok = cJSON_IsNumber(la) && cJSON_IsNumber(lo);
    if (ok) {
        *lat = (float)la->valuedouble;
        *lon = (float)lo->valuedouble;
        strlcpy(place, city ? city : "", cap);
        for (char *p = place; *p; p++) {
            if ((unsigned char)*p > 0x7e) *p = '?';   /* the bubble's font is ASCII */
        }
        snprintf(geo, sizeof(geo), "%.3f,%.3f,%s", (double)*lat, (double)*lon, place);
        config_set_str(KEY_GEO, geo);
        fresh = true;
    }
    cJSON_Delete(root);
    return ok;
}

static bool fetch_weather(void)
{
    float lat, lon;
    sagwa_weather_t w = { 0 };
    if (!locate(&lat, &lon, w.place, sizeof(w.place))) return false;
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&timezone=auto&forecast_days=3",
             (double)lat, (double)lon);
    if (!http_get(url, s_buf, sizeof(s_buf))) return false;
    cJSON *root = cJSON_Parse(s_buf);
    cJSON *cur = cJSON_GetObjectItem(root, "current"), *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *t = cJSON_GetObjectItem(cur, "temperature_2m"), *code = cJSON_GetObjectItem(cur, "weather_code");
    cJSON *off = cJSON_GetObjectItem(root, "utc_offset_seconds");
    bool ok = cJSON_IsNumber(t) && cJSON_IsNumber(code) && daily;
    if (ok) {
        w.fetched = time(NULL);
        w.temp_c = (float)t->valuedouble;
        w.code = code->valueint;
        cJSON *mx = cJSON_GetObjectItem(daily, "temperature_2m_max"), *mn = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *dc = cJSON_GetObjectItem(daily, "weather_code");
        for (int i = 0; i < 3; i++) {
            cJSON *a = cJSON_GetArrayItem(mx, i), *b = cJSON_GetArrayItem(mn, i), *c = cJSON_GetArrayItem(dc, i);
            w.max_c[i] = cJSON_IsNumber(a) ? (float)a->valuedouble : NAN;
            w.min_c[i] = cJSON_IsNumber(b) ? (float)b->valuedouble : NAN;
            w.day_code[i] = cJSON_IsNumber(c) ? c->valueint : 0;
        }
        if (cJSON_IsNumber(off)) {
            /* Her day follows the local clock: POSIX TZ from the offset (west is positive) */
            int o = off->valueint;
            w.utc_offset_s = o;
            int h = abs(o) / 3600, mnt = abs(o) % 3600 / 60;
            char tz[24];
            if (mnt) snprintf(tz, sizeof(tz), "SGW%s%d:%02d", o > 0 ? "-" : "", h, mnt);
            else snprintf(tz, sizeof(tz), "SGW%s%d", o > 0 ? "-" : "", h);
            char was[24] = "";
            config_get_str(KEY_TZ, was, sizeof(was));
            if (strcmp(was, tz)) {
                config_set_str(KEY_TZ, tz);
                apply_tz();
                ESP_LOGI(TAG, "time zone from the weather service: %s", tz);
            }
        }
        sagwa_offline_set_weather(&w);
        ESP_LOGI(TAG, "weather for %s: %.1f C, %s", w.place, (double)w.temp_c, sagwa_offline_weather_text(w.code));
    }
    cJSON_Delete(root);
    return ok;
}

/* ---- The task ---- */

static void update_task(void *arg)
{
    (void)arg;
    apply_tz();   /* before the first look at the clock, even offline */
    bool reported = false;
    char to[32];
    int64_t now = esp_timer_get_time();
    int64_t next_check = now + (int64_t)(config_get_str(KEY_TO, to, sizeof(to)) ? VERIFY_CHECK_S : FIRST_CHECK_S) * 1000000;
    int64_t next_weather = now + 30LL * 1000000;
    if (!config_get_str(KEY_TO, to, sizeof(to))) {
        sagwa_store_clear("update");   /* nothing pending: anything left there is stale */
    }
    for (;;) {
        if (!wifi_mgr_is_connected()) {
            if (sagwa_update_wait_request(5000)) {
                sagwa_update_status_set(NULL, "No internet", false);
            }
            continue;
        }
        start_sntp();
        if (!reported) {
            reported = true;
            vTaskDelay(pdMS_TO_TICKS(8000));   /* her start-up trill first */
            report_last_update();
        }
        now = esp_timer_get_time();
        int64_t next = next_check < next_weather ? next_check : next_weather;
        bool manual = next > now && sagwa_update_wait_request((uint32_t)((next - now) / 1000));
        if (!wifi_mgr_is_connected()) {
            if (manual) sagwa_update_status_set(NULL, "No internet", false);
            continue;
        }
        now = esp_timer_get_time();
        if (manual || now >= next_check) {
            int wait = check_once(manual);
            if (wait < 0) wait = check_once(true);
            if (wait >= 600) wait += (int)(esp_random() % (2 * JITTER_S)) - JITTER_S;
            next_check = esp_timer_get_time() + (int64_t)wait * 1000000;
        }
        if (esp_timer_get_time() >= next_weather) {
            bool ok = fetch_weather();
            next_weather = esp_timer_get_time() + (int64_t)(ok ? WEATHER_EVERY_S : WEATHER_RETRY_S) * 1000000;
        }
    }
}

void sagwa_update_start(void)
{
    /* Internal RAM for the stack, taken at boot while there is some: TLS, and
     * the firmware download (ota_run) with its flash writes, which can't run
     * from a PSRAM stack */
    if (xTaskCreate(update_task, "sagwa_update", 14336, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start the update task");
    }
    /* The microSD (on the display's SPI bus, already up) takes internal RAM
     * too, so it's mounted after the stack above has its block */
    sagwa_store_init();
    sagwa_offline_init();
}

#else

void sagwa_update_start(void)
{
#if CONFIG_MUSE_ENABLED
    sagwa_store_init();
    sagwa_offline_init();
#endif
}

bool sagwa_update_healthy(void)
{
    return false;
}

#endif
