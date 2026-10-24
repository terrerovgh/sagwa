/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sagwa_stats.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

static const char *TAG = "sagwa_stats";

#define NVS_NS "sagwa"
#define NVS_KEY "stats1"
#define SAVE_EVERY_US (10LL * 60 * 1000000)   /* flash wear: a few writes an hour */

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sagwa_stats_t s_session, s_total;
static float s_frac[SAGWA_T_COUNT];   /* time not yet a whole second */
static bool s_loaded, s_dirty;
static int64_t s_saved_us;

static void load(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    s_saved_us = esp_timer_get_time();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    sagwa_stats_t t;
    size_t len = sizeof(t);
    if (nvs_get_blob(h, NVS_KEY, &t, &len) == ESP_OK && len == sizeof(t)) {
        s_total = t;
    }
    nvs_close(h);
}

void sagwa_stats_save(void)
{
    sagwa_stats_t t;
    portENTER_CRITICAL(&s_lock);
    t = s_total;
    bool dirty = s_dirty;
    s_dirty = false;
    portEXIT_CRITICAL(&s_lock);
    s_saved_us = esp_timer_get_time();
    if (!dirty) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(h, NVS_KEY, &t, sizeof(t)) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGW(TAG, "couldn't save Sagwa's activity");
    }
    nvs_close(h);
}

void sagwa_stats_add_time(sagwa_time_t what, float secs)
{
    if (what >= SAGWA_T_COUNT || secs <= 0.0f) {
        return;
    }
    load();
    portENTER_CRITICAL(&s_lock);
    s_frac[what] += secs;
    uint32_t whole = (uint32_t)s_frac[what];
    if (whole) {
        s_frac[what] -= (float)whole;
        s_session.secs[what] += whole;
        s_total.secs[what] += whole;
        s_dirty = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (esp_timer_get_time() - s_saved_us > SAVE_EVERY_US) {
        sagwa_stats_save();
    }
}

void sagwa_stats_count(sagwa_count_t what)
{
    if (what >= SAGWA_N_COUNT) {
        return;
    }
    load();
    portENTER_CRITICAL(&s_lock);
    s_session.count[what]++;
    s_total.count[what]++;
    s_dirty = true;
    portEXIT_CRITICAL(&s_lock);
}

void sagwa_stats_get(sagwa_stats_t *session, sagwa_stats_t *total)
{
    load();
    portENTER_CRITICAL(&s_lock);
    if (session) *session = s_session;
    if (total) *total = s_total;
    portEXIT_CRITICAL(&s_lock);
}
