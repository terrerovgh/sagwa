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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Images Muse shares in the chat, shown as thumbnails under the reply.
 *
 * muse_thumbs_request() queues a file in the VM's workspace (the chat's
 * image_path, e.g. "workspace/your_files/sun.png"); a task fetches it over
 * Link's Noise session in Range requests, decodes it (baseline JPEG, or
 * 8-bit non-interlaced PNG), shrinks it to at most MUSE_THUMB_PX a side and
 * hands it to muse_ui_add_thumb(). Anything else is skipped with a log line.
 */
#define MUSE_THUMB_PX 96

void muse_thumbs_request(const char *path, const char *mime, size_t byte_len);

/*
 * Decodes a PNG or JPEG file held in memory straight to a thumbnail, alpha
 * over black. Returns a malloc'd RGB565 image and its size, or NULL with
 * `why` set. The fetch task uses these; host tests call them directly.
 */
uint16_t *muse_thumbs_decode_png(const uint8_t *file, size_t len, int *out_w, int *out_h,
                                 const char **why);
uint16_t *muse_thumbs_decode_jpeg(const uint8_t *file, size_t len, int *out_w, int *out_h,
                                  const char **why);

#ifdef __cplusplus
}
#endif
