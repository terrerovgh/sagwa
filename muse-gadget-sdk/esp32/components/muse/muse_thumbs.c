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

#include "muse_thumbs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz.h"
#include "rom/tjpgd.h"

#include "muse_link.h"
#include "muse_ui.h"

static const char *TAG = "muse_thumbs";
#define BIG_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#include <zlib.h>
#define BIG_ALLOC(n) malloc(n)
#endif

#define FILE_MAX (1536 * 1024)       /* larger files are skipped */
#define PIXELS_MAX (4096 * 4096)

/* ---- Shrinking: a box filter into the thumbnail as rows arrive ---- */

typedef struct {
    int src_w, src_h, tw, th;
    uint32_t *acc;                   /* r, g, b, count per thumbnail pixel */
} shrink_t;

static bool shrink_begin(shrink_t *s, int w, int h)
{
    s->src_w = w;
    s->src_h = h;
    if (w >= h) {
        s->tw = w < MUSE_THUMB_PX ? w : MUSE_THUMB_PX;
        s->th = (int)((int64_t)h * s->tw / w);
    } else {
        s->th = h < MUSE_THUMB_PX ? h : MUSE_THUMB_PX;
        s->tw = (int)((int64_t)w * s->th / h);
    }
    if (s->tw < 1) s->tw = 1;
    if (s->th < 1) s->th = 1;
    size_t bytes = (size_t)s->tw * s->th * 4 * sizeof(uint32_t);
    s->acc = BIG_ALLOC(bytes);
    if (s->acc) {
        memset(s->acc, 0, bytes);
    }
    return s->acc != NULL;
}

/* One source pixel at (x, y), RGB. */
static inline void shrink_px(shrink_t *s, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    int tx = (int)((int64_t)x * s->tw / s->src_w);
    int ty = (int)((int64_t)y * s->th / s->src_h);
    uint32_t *a = &s->acc[(ty * s->tw + tx) * 4];
    a[0] += r;
    a[1] += g;
    a[2] += b;
    a[3]++;
}

static uint16_t *shrink_end(shrink_t *s, int *out_w, int *out_h)
{
    uint16_t *px = BIG_ALLOC((size_t)s->tw * s->th * sizeof(uint16_t));
    if (px) {
        for (int i = 0; i < s->tw * s->th; i++) {
            uint32_t *a = &s->acc[i * 4];
            uint32_t n = a[3] ? a[3] : 1;
            uint32_t r = a[0] / n, g = a[1] / n, b = a[2] / n;
            px[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
        *out_w = s->tw;
        *out_h = s->th;
    }
    free(s->acc);
    s->acc = NULL;
    return px;
}

/* ---- PNG: 8-bit, non-interlaced; IDAT inflated a dictionary at a time ---- */

typedef struct {
    shrink_t shrink;
    int w, h, ctype, channels;
    size_t stride;                   /* bytes per row, without the filter byte */
    uint8_t *cur, *prev;             /* rows, each with its filter byte first */
    size_t fill;                     /* bytes of `cur` so far */
    int y;
    uint8_t palette[256 * 3];
    bool bad;
} png_t;

static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static void png_row(png_t *p)
{
    uint8_t f = p->cur[0], *row = p->cur + 1;
    const uint8_t *up = p->prev + 1;
    int bpp = p->channels;
    for (size_t i = 0; i < p->stride; i++) {
        int a = i >= (size_t)bpp ? row[i - bpp] : 0;
        int b = up[i];
        int c = i >= (size_t)bpp ? up[i - bpp] : 0;
        switch (f) {
        case 1: row[i] += a; break;
        case 2: row[i] += b; break;
        case 3: row[i] += (a + b) >> 1; break;
        case 4: row[i] += paeth(a, b, c); break;
        default: break;
        }
    }
    if (f > 4) {
        p->bad = true;
    }
    for (int x = 0; x < p->w; x++) {
        const uint8_t *q = row + x * bpp;
        uint8_t r, g, b;
        unsigned alpha = 255;
        switch (p->ctype) {
        case 0: r = g = b = q[0]; break;
        case 2: r = q[0]; g = q[1]; b = q[2]; break;
        case 3: r = p->palette[q[0] * 3]; g = p->palette[q[0] * 3 + 1]; b = p->palette[q[0] * 3 + 2]; break;
        case 4: r = g = b = q[0]; alpha = q[1]; break;
        default: r = q[0]; g = q[1]; b = q[2]; alpha = q[3]; break;
        }
        if (alpha != 255) {
            r = (uint8_t)(r * alpha / 255);
            g = (uint8_t)(g * alpha / 255);
            b = (uint8_t)(b * alpha / 255);
        }
        shrink_px(&p->shrink, x, p->y, r, g, b);
    }
    uint8_t *t = p->prev;
    p->prev = p->cur;
    p->cur = t;
    p->fill = 0;
    p->y++;
}

/* Inflated bytes, cut into rows. */
static void png_feed(png_t *p, const uint8_t *data, size_t len)
{
    while (len && p->y < p->h && !p->bad) {
        size_t want = p->stride + 1 - p->fill;
        size_t n = len < want ? len : want;
        memcpy(p->cur + p->fill, data, n);
        p->fill += n;
        data += n;
        len -= n;
        if (p->fill == p->stride + 1) {
            png_row(p);
        }
    }
}

/* Inflates the zlib stream `z` into png_feed. False if it's corrupt. */
static bool png_inflate(png_t *p, const uint8_t *z, size_t zlen)
{
#ifdef ESP_PLATFORM
    tinfl_decompressor *d = BIG_ALLOC(sizeof(*d));
    uint8_t *dict = BIG_ALLOC(TINFL_LZ_DICT_SIZE);
    bool ok = false;
    if (d && dict) {
        tinfl_init(d);
        size_t in_ofs = 0, dict_ofs = 0;
        for (;;) {
            size_t in_bytes = zlen - in_ofs, out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
            tinfl_status st = tinfl_decompress(d, z + in_ofs, &in_bytes, dict, dict + dict_ofs, &out_bytes,
                                               TINFL_FLAG_PARSE_ZLIB_HEADER);
            in_ofs += in_bytes;
            png_feed(p, dict + dict_ofs, out_bytes);
            dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
            if (st == TINFL_STATUS_DONE) {
                ok = true;
                break;
            }
            if (st < TINFL_STATUS_DONE || (st == TINFL_STATUS_NEEDS_MORE_INPUT && in_ofs >= zlen) || p->bad) {
                break;
            }
        }
    }
    free(d);
    free(dict);
    return ok || p->y == p->h;
#else
    z_stream s = { 0 };
    uint8_t out[16384];
    if (inflateInit(&s) != Z_OK) return false;
    s.next_in = (Bytef *)z;
    s.avail_in = (uInt)zlen;
    int rc;
    do {
        s.next_out = out;
        s.avail_out = sizeof(out);
        rc = inflate(&s, Z_NO_FLUSH);
        png_feed(p, out, sizeof(out) - s.avail_out);
    } while (rc == Z_OK && !p->bad);
    inflateEnd(&s);
    return rc == Z_STREAM_END || p->y == p->h;
#endif
}

uint16_t *muse_thumbs_decode_png(const uint8_t *file, size_t len, int *out_w, int *out_h, const char **why)
{
    static const uint8_t SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    *why = NULL;
    if (len < 33 || memcmp(file, SIG, 8) || memcmp(file + 12, "IHDR", 4)) {
        *why = "not a PNG";
        return NULL;
    }
    png_t *p = calloc(1, sizeof(*p));
    if (!p) {
        *why = "out of memory";
        return NULL;
    }
    p->w = (int)be32(file + 16);
    p->h = (int)be32(file + 20);
    int depth = file[24], interlace = file[28];
    p->ctype = file[25];
    static const int CH[7] = { 1, 0, 3, 1, 2, 0, 4 };
    p->channels = p->ctype <= 6 ? CH[p->ctype] : 0;
    uint16_t *thumb = NULL;
    uint8_t *idat = NULL;
    if (depth != 8 || interlace || !p->channels || p->w <= 0 || p->h <= 0 ||
        (int64_t)p->w * p->h > PIXELS_MAX) {
        *why = "unsupported PNG (8-bit, non-interlaced only)";
        goto out;
    }
    p->stride = (size_t)p->w * p->channels;

    /* Gather the palette and every IDAT's data, in order */
    idat = BIG_ALLOC(len);
    size_t zlen = 0;
    for (size_t at = 8; at + 12 <= len;) {
        uint32_t n = be32(file + at);
        const uint8_t *type = file + at + 4, *data = file + at + 8;
        if (n > len - at - 12) {
            break;
        }
        if (!memcmp(type, "PLTE", 4)) {
            memcpy(p->palette, data, n < sizeof(p->palette) ? n : sizeof(p->palette));
        } else if (!memcmp(type, "IDAT", 4) && idat) {
            memcpy(idat + zlen, data, n);
            zlen += n;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        at += 12 + n;
    }
    p->cur = calloc(1, p->stride + 1);
    p->prev = calloc(1, p->stride + 1);
    if (!idat || !p->cur || !p->prev || !shrink_begin(&p->shrink, p->w, p->h)) {
        *why = "out of memory";
        goto out;
    }
    if (!png_inflate(p, idat, zlen) || p->bad || p->y < p->h) {
        *why = "corrupt PNG";
        free(p->shrink.acc);
        goto out;
    }
    thumb = shrink_end(&p->shrink, out_w, out_h);
    if (!thumb) {
        *why = "out of memory";
    }
out:
    free(idat);
    free(p->cur);
    free(p->prev);
    free(p);
    return thumb;
}

/* ---- JPEG: the ROM's TJpgDec, scaled down 1/2..1/8 on the way ---- */

#ifdef ESP_PLATFORM
typedef struct {
    const uint8_t *file;
    size_t len, pos;
    shrink_t shrink;
} jpeg_t;

static UINT jpeg_in(JDEC *jd, BYTE *buf, UINT n)
{
    jpeg_t *j = jd->device;
    size_t left = j->len - j->pos;
    if (n > left) n = (UINT)left;
    if (buf) memcpy(buf, j->file + j->pos, n);
    j->pos += n;
    return n;
}

static UINT jpeg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_t *j = jd->device;
    const uint8_t *rgb = bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++, rgb += 3) {
            if (x < j->shrink.src_w && y < j->shrink.src_h) {
                shrink_px(&j->shrink, x, y, rgb[0], rgb[1], rgb[2]);
            }
        }
    }
    return 1;
}
#endif

uint16_t *muse_thumbs_decode_jpeg(const uint8_t *file, size_t len, int *out_w, int *out_h, const char **why)
{
#ifdef ESP_PLATFORM
    *why = NULL;
    jpeg_t j = { .file = file, .len = len };
    void *pool = malloc(3100);
    if (!pool) {
        *why = "out of memory";
        return NULL;
    }
    uint16_t *thumb = NULL;
    JDEC jd;
    JRESULT rc = jd_prepare(&jd, jpeg_in, pool, 3100, &j);
    if (rc == JDR_OK) {
        /* Shrink in the decoder while that still leaves twice the thumbnail */
        uint8_t scale = 0;
        while (scale < 3 && (int)(jd.width >> (scale + 1)) >= 2 * MUSE_THUMB_PX &&
               (int)(jd.height >> (scale + 1)) >= 2 * MUSE_THUMB_PX) {
            scale++;
        }
        int w = (int)(jd.width >> scale), h = (int)(jd.height >> scale);
        if (!shrink_begin(&j.shrink, w, h)) {
            *why = "out of memory";
        } else if ((rc = jd_decomp(&jd, jpeg_out, scale)) == JDR_OK) {
            thumb = shrink_end(&j.shrink, out_w, out_h);
        } else {
            free(j.shrink.acc);
        }
    }
    if (!thumb && !*why) {
        *why = rc == JDR_FMT3 ? "progressive JPEG" : "not a valid JPEG";
    }
    free(pool);
    return thumb;
#else
    (void)file;
    (void)len;
    (void)out_w;
    (void)out_h;
    *why = "no JPEG decoder on the host";
    return NULL;
#endif
}

/* ---- Fetching from the VM, a range at a time, on our own task ---- */

#ifdef ESP_PLATFORM
#define RANGE_BYTES (32 * 1024)      /* well inside a Noise frame */
#define RANGE_TIMEOUT_MS 15000
#define QUEUE_LEN 4

typedef struct {
    char path[200];
    char mime[32];
    size_t len;
} job_t;

static QueueHandle_t s_jobs;

static struct {
    SemaphoreHandle_t done;
    uint32_t gen;
    uint8_t *buf;
    size_t cap, got;
    int status;
    bool ok;
} s_req;
static portMUX_TYPE s_req_lock = portMUX_INITIALIZER_UNLOCKED;

static void on_frame(void *ctx, int status, const uint8_t *data, size_t len, bool end)
{
    portENTER_CRITICAL(&s_req_lock);
    bool mine = (uint32_t)(uintptr_t)ctx == s_req.gen && s_req.buf;
    if (mine) {
        if (status) s_req.status = status;
        if (len && (s_req.status == 200 || s_req.status == 206)) {
            size_t n = len <= s_req.cap - s_req.got ? len : s_req.cap - s_req.got;
            memcpy(s_req.buf + s_req.got, data, n);
            s_req.got += n;
        }
        s_req.ok = end && status >= 0;
    }
    portEXIT_CRITICAL(&s_req_lock);
    if (mine && (end || status < 0)) {
        xSemaphoreGive(s_req.done);
    }
}

/* GET one range of /fs/raw/<path> into buf. Returns bytes, or -1. */
static int get_range(const char *url, uint8_t *buf, size_t at, size_t len)
{
    char range[48];
    snprintf(range, sizeof(range), "bytes=%u-%u", (unsigned)at, (unsigned)(at + len - 1));
    const char *headers[] = { "Range", range, NULL };
    xSemaphoreTake(s_req.done, 0);
    portENTER_CRITICAL(&s_req_lock);
    uint32_t gen = ++s_req.gen;
    s_req.buf = buf;
    s_req.cap = len;
    s_req.got = 0;
    s_req.status = 0;
    s_req.ok = false;
    portEXIT_CRITICAL(&s_req_lock);

    int64_t id = muse_link_req_open("GET", url, headers, true, on_frame, (void *)(uintptr_t)gen);
    bool done = id && xSemaphoreTake(s_req.done, pdMS_TO_TICKS(RANGE_TIMEOUT_MS)) == pdTRUE;

    portENTER_CRITICAL(&s_req_lock);
    s_req.gen++;
    s_req.buf = NULL;
    int got = (int)s_req.got, status = s_req.status;
    bool ok = s_req.ok;
    portEXIT_CRITICAL(&s_req_lock);
    if (id && !done) {
        muse_link_req_cancel(id);
    }
    if (!done || !ok || (status != 200 && status != 206)) {
        ESP_LOGW(TAG, "%s at %u: status %d, %d bytes%s", url, (unsigned)at, status, got, done ? "" : ", timed out");
        return -1;
    }
    return got;
}

static char *request_path(const char *path)
{
    while (*path == '/') path++;
    char *out = malloc(strlen("/fs/raw/") + strlen(path) * 3 + 1);
    if (!out) return NULL;
    char *p = out + sprintf(out, "/fs/raw/");
    for (const unsigned char *c = (const unsigned char *)path; *c; c++) {
        if (isalnum(*c) || strchr("-._~/", *c)) {
            *p++ = (char)*c;
        } else {
            p += sprintf(p, "%%%02X", *c);
        }
    }
    *p = '\0';
    return out;
}

static void fetch_one(const job_t *job)
{
    if (!job->len || job->len > FILE_MAX) {
        ESP_LOGW(TAG, "%s: %u bytes, skipped", job->path, (unsigned)job->len);
        return;
    }
    char *url = request_path(job->path);
    uint8_t *file = BIG_ALLOC(job->len);
    if (!url || !file) {
        ESP_LOGW(TAG, "%s: out of memory", job->path);
        free(url);
        free(file);
        return;
    }
    size_t have = 0;
    while (have < job->len) {
        size_t n = job->len - have < RANGE_BYTES ? job->len - have : RANGE_BYTES;
        int got = get_range(url, file + have, have, n);
        if (got <= 0) break;
        have += (size_t)got;
        if ((size_t)got < n) break;
    }
    free(url);
    int w = 0, h = 0;
    const char *why = "incomplete download";
    uint16_t *thumb = NULL;
    if (have == job->len) {
        bool png = have >= 8 && file[0] == 0x89 && file[1] == 'P';
        thumb = png ? muse_thumbs_decode_png(file, have, &w, &h, &why)
                    : muse_thumbs_decode_jpeg(file, have, &w, &h, &why);
    }
    free(file);
    if (!thumb) {
        ESP_LOGW(TAG, "%s (%s): %s", job->path, job->mime, why);
        return;
    }
    ESP_LOGI(TAG, "%s: %u bytes -> %dx%d thumbnail", job->path, (unsigned)have, w, h);
    muse_ui_add_thumb(thumb, w, h);
}

static void thumbs_task(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
            fetch_one(&job);
        }
    }
}

void muse_thumbs_request(const char *path, const char *mime, size_t byte_len)
{
    if (!path || !path[0]) {
        return;
    }
    if (!s_jobs) {
        s_req.done = xSemaphoreCreateBinary();
        s_jobs = xQueueCreate(QUEUE_LEN, sizeof(job_t));
        if (!s_req.done || !s_jobs ||
            xTaskCreatePinnedToCoreWithCaps(thumbs_task, "muse_thumbs", 6144, NULL, 3, NULL, 1,
                                            MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGW(TAG, "no task for thumbnails");
            return;
        }
    }
    job_t job = { .len = byte_len };
    if (!strncmp(path, "sandbox://", 10)) {
        path += 10;
    }
    strlcpy(job.path, path, sizeof(job.path));
    strlcpy(job.mime, mime ? mime : "", sizeof(job.mime));
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "%s: queue full, skipped", job.path);
    }
}
#endif
