/*
 * Sagwa updates herself from her GitHub repository's releases (sagwa_update.h).
 */

#include "sagwa_update.h"

#include "sdkconfig.h"

#if CONFIG_SAGWA_AUTO_UPDATE

#include <inttypes.h>
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
#include "sagwa_sfx.h"
#include "wifi_mgr.h"

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
#define MANIFEST_MAX 2048
#define HTTP_TIMEOUT_MS 30000
#define ERASE_STEP (64 * 1024)

/* NVS (config_store) keys, at most 15 characters */
#define KEY_CLIPS_SHA "sgw_clips_sha"
#define KEY_FROM "sgw_upd_from"
#define KEY_TO "sgw_upd_to"
#define KEY_NOTES "sgw_upd_notes"

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

static bool fetch_manifest(manifest_t *m)
{
    static char buf[MANIFEST_MAX];
    body_t body = { .buf = buf, .cap = sizeof(buf) };
    buf[0] = '\0';
    esp_http_client_config_t cfg = http_config(RELEASES "manifest.json", on_manifest_data, &body);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status != 200 || body.overflow) {
        ESP_LOGW(TAG, "manifest: %s, HTTP %d%s", esp_err_to_name(err), status, body.overflow ? ", too big" : "");
        return false;
    }
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
    while (d->erased < d->written + n) {
        if (esp_partition_erase_range(d->part, d->erased, ERASE_STEP) != ESP_OK) {
            d->failed = true;
            return ESP_FAIL;
        }
        d->erased += ERASE_STEP;
    }
    if (esp_partition_write(d->part, d->written, evt->data, n) != ESP_OK ||
        mbedtls_md_update(&d->md, evt->data, n) != 0) {
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

/* Rewrites the clip partition from the release. Afterwards the pack in it is
 * the new one or broken (until the next try): a restart follows either way. */
static bool download_clips(const manifest_t *m)
{
    const esp_partition_t *p = clips_partition();
    if (!p || !m->clips_size || m->clips_size > p->size || (p->size % ERASE_STEP)) {
        ESP_LOGE(TAG, "clips: no room (%" PRIu32 " bytes)", m->clips_size);
        return false;
    }
    sagwa_clips_suspend();
    clips_dl_t d = { .part = p, .want = m->clips_size, .last_pct = -5 };
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
    config_erase_key(KEY_CLIPS_SHA);
    if (err != ESP_OK || status != 200 || d.failed || d.written != d.want || strcmp(got, m->clips_sha)) {
        ESP_LOGE(TAG, "clips: %s, HTTP %d, %" PRIu32 "/%" PRIu32 " bytes, sha %s", esp_err_to_name(err), status,
                 d.written, d.want, got);
        return false;
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

/* Returns seconds until the next check */
static int check_once(void)
{
    manifest_t m;
    if (!fetch_manifest(&m)) return RETRY_S;
    s_healthy = true;

    const char *running = running_version();
    bool fw = version_newer(m.version, running);
    char have[65] = "";
    bool clips = m.clips_sha[0] && m.clips_size && clips_sha_now(m.clips_size, have) && strcmp(have, m.clips_sha);
    ESP_LOGI(TAG, "latest v%s, running v%s: firmware %s, clips %s", m.version, running, fw ? "newer" : "same",
             clips ? "changed" : "same");
    if (!fw && !clips) return CONFIG_SAGWA_UPDATE_INTERVAL_MIN * 60;

    /* Wait for a quiet moment (checking back every 30 s, up to a few hours) */
    int64_t until = esp_timer_get_time() + (int64_t)CONFIG_SAGWA_UPDATE_INTERVAL_MIN * 60 * 1000000;
    while (!free_now()) {
        if (esp_timer_get_time() > until) return 60;
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    if (!countdown(&m, fw, clips)) return POSTPONE_S;

    config_set_str(KEY_FROM, running);
    config_set_str(KEY_TO, fw ? m.version : running);
    config_set_str(KEY_NOTES, m.notes);

    if (clips) {
        say("Actualizando mis animaciones...");
        if (!download_clips(&m)) {
            config_erase_key(KEY_TO);
            sagwa_sfx_trigger(SAGWA_SFX_STARTLE);
            say("No pude bajar las animaciones nuevas. Reinicio y lo intento luego.");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();   /* the pack in the partition may be half written */
        }
    }
    if (fw) {
        say("Descargando la nueva version...");
        s_ota_result = -1;
        ota_start(RELEASES "sagwa-cores3.bin", false, on_ota, NULL);
        while (s_ota_result < 0) vTaskDelay(pdMS_TO_TICKS(500));
        if (s_ota_result != OTA_RESULT_APPLIED) {
            config_erase_key(KEY_TO);
            sagwa_sfx_trigger(SAGWA_SFX_STARTLE);
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

static void start_sntp(void)
{
    if (s_sntp) return;
    s_sntp = true;
    setenv("TZ", CONFIG_SAGWA_TZ, 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP didn't start");
    }
}

static void update_task(void *arg)
{
    (void)arg;
    bool reported = false;
    char to[32];
    int wait = config_get_str(KEY_TO, to, sizeof(to)) ? VERIFY_CHECK_S : FIRST_CHECK_S;
    for (;;) {
        while (!wifi_mgr_is_connected()) vTaskDelay(pdMS_TO_TICKS(5000));
        start_sntp();
        if (!reported) {
            reported = true;
            vTaskDelay(pdMS_TO_TICKS(8000));   /* her start-up trill first */
            report_last_update();
        }
        vTaskDelay(pdMS_TO_TICKS(wait * 1000));
        if (!wifi_mgr_is_connected()) continue;
        wait = check_once();
        if (wait >= 600) wait += (int)(esp_random() % (2 * JITTER_S)) - JITTER_S;
    }
}

void sagwa_update_start(void)
{
    /* Internal RAM for the stack: TLS, and flash writes, which can't run from PSRAM stacks */
    if (xTaskCreate(update_task, "sagwa_update", 10240, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "could not start the update task");
    }
}

#else

void sagwa_update_start(void)
{
}

bool sagwa_update_healthy(void)
{
    return false;
}

#endif
