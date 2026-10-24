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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What Sagwa has been up to, for the settings' Sagwa page: time in each
 * activity and how often she did things, since power-on and in total (kept
 * in NVS, saved every few minutes and on power-off).
 */
typedef enum {
    SAGWA_T_SLEEP,      /* curled up asleep */
    SAGWA_T_REST,       /* lying in a loaf */
    SAGWA_T_SIT,
    SAGWA_T_STAND,      /* standing or walking */
    SAGWA_T_PLAY,
    SAGWA_T_WORK,       /* the agent busy: thinking or answering */
    SAGWA_T_COUNT
} sagwa_time_t;

typedef enum {
    SAGWA_N_NAP,        /* fell asleep */
    SAGWA_N_STRETCH,
    SAGWA_N_PLAY,       /* played, out of boredom */
    SAGWA_N_WORK,       /* agent turns */
    SAGWA_N_WOKEN,      /* woken by the device moving */
    SAGWA_N_GROOM,
    SAGWA_N_WALK,
    SAGWA_N_MEOW,       /* called out on her own */
    SAGWA_N_PURR,       /* petted */
    SAGWA_N_COUNT
} sagwa_count_t;

typedef struct {
    uint32_t secs[SAGWA_T_COUNT];
    uint32_t count[SAGWA_N_COUNT];
} sagwa_stats_t;

void sagwa_stats_add_time(sagwa_time_t what, float secs);
void sagwa_stats_count(sagwa_count_t what);

/* Since power-on and all time; either may be NULL. */
void sagwa_stats_get(sagwa_stats_t *session, sagwa_stats_t *total);

/* Writes the totals to NVS now (also done every few minutes by add_time). */
void sagwa_stats_save(void);

#ifdef __cplusplus
}
#endif
