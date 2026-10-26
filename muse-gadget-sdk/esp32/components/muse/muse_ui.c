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

#include "muse_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "src/draw/lv_image_decoder_private.h"   /* custom decoder */
#include "src/misc/lv_area_private.h"            /* lv_area_intersect, for the ring */

#include "muse_ble.h"
#include "muse_board.h"
#include "muse_input.h"
#include "muse_chat.h"
#include "muse_console.h"
#include "muse_link.h"
#include "muse_mem.h"
#include "muse_menu.h"
#include "muse_pixel.h"
#include "muse_settings.h"
#include "muse_settings_ui.h"
#include "muse_state.h"
#include "muse_text.h"
#include "muse_wifi.h"
#include "sagwa_offline.h"
#include "sagwa_sfx.h"
#if CONFIG_MUSE_VIDEO
#include "muse_video.h"
#endif
#if CONFIG_MUSE_WATCHER_CAMERA
#include "boards/watcher_camera.h"
#endif

static const char *TAG = "muse_ui";

#define METER_SEGS 14
#define METER_SEG_PX 9
#define METER_GAP_PX 4
#define RING_RANGE 1000
#define CAPTION_W 256           /* 16 columns of unscii_16, the width reply captions wrap to */
#define CAPTION_LINE_SPACE 2
#define ART_BLANK_ROWS 3        /* Muse's art never reaches the grid's bottom rows */
#define MINI_CELL_PX 2          /* Muse's grid cells over a reply that's read */
#define ANSWER_MS 360           /* Sagwa gliding smoothly to top-right corner and back */
#define SPEAKER_PX 64
#define SPEAKER_GROW_PX 8       /* how much the speaker button swells while held */
#define SPEAKER_HOLD_MS 400     /* LVGL's long press */

#define COLOR_DIM 0x8b84a8
#define COLOR_CAPTION 0xf4f0ea
#define COLOR_ACTIVITY 0xb4a998   /* warm grey, quieter than the reply */
#define COLOR_RING_BG 0x140f22
#define COLOR_METER_OFF 0x1d1733
#define COLOR_ACCENT 0xa77dff
#define COLOR_DOT_OFF 0x3a3358
#define COLOR_LIT 0xf2efff
#define SETTINGS_TICK_S 0.25f

/* Text on 128 px screens, as in the button menu (muse_menu.c). */
#if LV_FONT_MONTSERRAT_12
#define FONT_COMPACT (&lv_font_montserrat_12)
#else
#define FONT_COMPACT (&lv_font_montserrat_14)
#endif

/*
 * Two layouts: the full one (round 466 px or similar) with a progress ring,
 * level meter and settings tile, and a compact one for tiny screens (128 px)
 * where Muse fills the screen and text overlays it.
 */
static int s_w, s_h;
static bool s_small;
static bool s_tall;         /* compact, with room above and below Muse (StickS3) */
static int s_canvas_px;     /* Muse's size on screen */
static int s_dy;            /* full layout: offset from a 466 px tall screen */
static lv_indev_t *s_indev;
static lv_obj_t *s_tv;
/* True while the face tile is the visible tile. Updated in the LVGL task;
 * read from any task (single word, benign if briefly stale). */
static volatile bool s_face_active = true;
static lv_obj_t *s_face;

static void tile_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    s_face_active = (lv_tileview_get_tile_active(tv) == s_face);
}

/* From any task: the face tile is the currently visible tile. */
bool muse_ui_face_active(void)
{
    return s_face_active;
}
static lv_obj_t *s_settings;
static lv_obj_t *s_dots[2];
static lv_obj_t *s_wifi_icon;
static lv_obj_t *s_ble_icon;
static lv_obj_t *s_cover;
static lv_obj_t *s_pair;
static lv_obj_t *s_pair_code;
static lv_obj_t *s_pair_title;
static lv_obj_t *s_pair_hint;
static lv_obj_t *s_canvas;
static lv_obj_t *s_mic_icon;
static lv_obj_t *s_ptt_hint, *s_spk_hint, *s_pwr_hint;
#if LV_USE_SNAPSHOT
static volatile bool s_bench_demo;
void muse_ui_bench_demo(bool enabled)
{
    s_bench_demo = enabled;   /* display-only: never changes BLE identity/setup */
}
#endif
static lv_obj_t *s_ring;
static lv_obj_t *s_bar;     /* compact layout's stand-in for the ring */
static lv_obj_t *s_state_lbl;
static lv_obj_t *s_name_lbl;    /* this gadget's own name, to tell it from the next one */
static lv_obj_t *s_power_lbl;
static lv_obj_t *s_caption_lbl;
static lv_obj_t *s_reply_lbl;   /* full layout: the whole reply, inside s_reply_box */
static lv_obj_t *s_reply_box;   /* scrolls vertically through the reply and its images */
static lv_obj_t *s_thumb_row;   /* the reply's image thumbnails, under its text */
static uint32_t s_reply_version;
static bool s_reply_has;        /* there's reply text or a thumbnail to show */
static float s_reply_until;     /* keep the reply up until then, once Muse is done */
static float s_user_scroll_until;   /* the reader is scrolling: don't follow the speech */
static int s_follow_y = -1;     /* last scroll position set to follow the speech */
#define REPLY_LINGER_S 60.0f    /* a finished reply stays this long after the last touch */
#define REPLY_HOLD_S 8.0f       /* after a touch, how long the reader keeps the scroll */
static lv_obj_t *s_meter[METER_SEGS];
static lv_obj_t *s_speaker;
static lv_obj_t *s_speaker_icon;
static lv_obj_t *s_aux_icon;
static lv_obj_t *s_image;   /* display.draw_url, over the face */
#if CONFIG_MUSE_WATCHER_CAMERA
static lv_obj_t *s_camera_hint;
#endif
static lv_image_dsc_t s_image_dsc;  /* its data is set once the image is shown */
/* The download writes the pixels without the display lock, so a big JPEG isn't
 * held up by each frame; this guards the buffer and what changed in it. */
static SemaphoreHandle_t s_image_mutex;
static uint16_t *s_image_buf;
static lv_area_t s_image_area;
static bool s_image_dirty;
static bool s_ready;

static float s_level;
static uint32_t s_caption_version;
static float s_next_settings_tick;
static bool s_dark;
static int s_brightness = -1;       /* last applied */
static int s_preview_brightness = -1;
static int s_shown_page = -1;
static int s_shown_speaker = -1;
static muse_mode_t s_last_mode = MUSE_MODE_COUNT;
static muse_mode_t s_last_epaper_pose = MUSE_MODE_COUNT;

/*
 * While Muse is thinking or speaking it shrinks to make room for the reply:
 * a little when the reply is heard, over a few lines to follow along, and
 * to the top when it's only read, over a page.
 */
typedef struct {
    int px, x, y;             /* Sagwa's size and centre offsets */
    int cols, lines;          /* the reply's page */
    int w, h, top;            /* and where it goes */
    lv_text_align_t align;
    lv_obj_t *hides[4];       /* what it covers */
} answer_layout_t;

enum { ANSWER_HEARD, ANSWER_READ };
static answer_layout_t s_answers[2];
static int s_answer = -1;       /* the layout showing, or -1 */
static int s_page_for = -1;     /* the layout the reply's page is sized for */
static int s_big_y;             /* Sagwa's centre Y at full size */
static int s_muse_x;            /* current X offset from centre */
static int s_muse_y;            /* current Y offset from centre */
static int s_from_px, s_from_x, s_from_y, s_to_px, s_to_x, s_to_y;
static int s_last_rig = -1;

/*
 * Muse reaches LVGL as an image whose pixels are made on demand: a decoder
 * scales the 64 px grid up one strip of rows at a time, so the full-size frame
 * never sits in RAM and LVGL never runs its (slow) image transform.
 *
 * LVGL splits each refresh into a tile per draw unit and draws them at once,
 * each with its own open decoder, so each one takes its own strip: with one
 * shared strip, a tile blended rows the other had just scaled into it.
 */
#define STRIP_ROWS 16
#define STRIPS LV_DRAW_SW_DRAW_UNIT_CNT

#define MUSE_PIXEL_MAX_PX 512   /* muse_pixel_set_size's cap */
#define DIRTY_RECTS_MAX 8       /* with the meter and labels, well inside LVGL's 32 areas */
#define DIRTY_MERGE_PX 2048     /* pixels that cost about one walk of the widget tree */

static lv_image_dsc_t s_muse_src;
static lv_draw_buf_t *s_strips[STRIPS];
static bool s_strip_busy[STRIPS];

static lv_result_t muse_dec_info(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc, lv_image_header_t *header)
{
    (void)dec;
    if (dsc->src != &s_muse_src) {
        return LV_RESULT_INVALID;
    }
    *header = s_muse_src.header;
    return LV_RESULT_OK;
}

/* LVGL holds its decoder lock around open and close. */
static lv_result_t muse_dec_open(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
    (void)dec;
    for (int i = 0; i < STRIPS; i++) {
        if (!s_strip_busy[i]) {
            s_strip_busy[i] = true;
            dsc->user_data = s_strips[i];
            return LV_RESULT_OK;   /* the rest of the work is in get_area */
        }
    }
    return LV_RESULT_INVALID;
}

static lv_result_t muse_dec_get_area(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc,
                                     const lv_area_t *full, lv_area_t *area)
{
    (void)dec;
    int32_t y1 = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y1 > full->y2) {
        return LV_RESULT_INVALID;
    }
    int32_t y2 = LV_MIN(y1 + STRIP_ROWS - 1, full->y2);
    int32_t w = lv_area_get_width(full);
    lv_draw_buf_t *buf = lv_draw_buf_reshape(dsc->user_data, LV_COLOR_FORMAT_RGB565, w, y2 - y1 + 1, LV_STRIDE_AUTO);
    if (!buf) {
        return LV_RESULT_INVALID;
    }
    muse_pixel_scale((uint16_t *)buf->data, buf->header.stride / sizeof(uint16_t), full->x1, full->x2, y1, y2);
    area->x1 = full->x1;
    area->x2 = full->x2;
    area->y1 = y1;
    area->y2 = y2;
    dsc->decoded = buf;
    return LV_RESULT_OK;
}

static void muse_dec_close(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
    (void)dec;
    for (int i = 0; i < STRIPS; i++) {
        if (s_strips[i] == dsc->user_data) {
            s_strip_busy[i] = false;   /* kept for the next open */
        }
    }
}

static void muse_image_init(void)
{
    s_muse_src.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_muse_src.header.cf = LV_COLOR_FORMAT_RGB565;
    s_muse_src.header.w = s_canvas_px;
    s_muse_src.header.h = s_canvas_px;
    s_muse_src.header.stride = s_canvas_px * sizeof(uint16_t);
    s_muse_src.data = (const uint8_t *)&s_muse_src;   /* LVGL skips sources without data */
    muse_pixel_set_size(s_canvas_px);

    for (int i = 0; i < STRIPS; i++) {
        s_strips[i] = lv_draw_buf_create(s_canvas_px, STRIP_ROWS, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        assert(s_strips[i]);
    }
    lv_image_decoder_t *dec = lv_image_decoder_create();
    assert(dec);
    lv_image_decoder_set_info_cb(dec, muse_dec_info);
    lv_image_decoder_set_open_cb(dec, muse_dec_open);
    lv_image_decoder_set_get_area_cb(dec, muse_dec_get_area);
    lv_image_decoder_set_close_cb(dec, muse_dec_close);
    dec->name = "muse";
}

static void invalidate_muse(void)
{
    lv_obj_invalidate(s_canvas);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);

/* A microphone from primitives: LVGL's symbol font has none. */
static lv_obj_t *make_mic(lv_obj_t *parent, int size)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    int stroke = size / 10 > 1 ? size / 10 : 1;

    lv_obj_t *head = lv_obj_create(box);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, size * 3 / 8, size * 9 / 16);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *cradle = lv_arc_create(box);
    lv_obj_set_size(cradle, size * 5 / 8 + stroke, size * 5 / 8 + stroke);
    lv_arc_set_bg_angles(cradle, 0, 180);
    lv_obj_remove_style(cradle, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(cradle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(cradle, 0, 0);
    lv_obj_set_style_arc_opa(cradle, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(cradle, stroke, LV_PART_MAIN);
    lv_obj_align(cradle, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *stem = lv_obj_create(box);
    lv_obj_remove_style_all(stem);
    lv_obj_set_size(stem, stroke, size * 7 / 8 - size * 5 / 8);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_align(stem, LV_ALIGN_TOP_MID, 0, size * 5 / 8);

    lv_obj_t *base = lv_obj_create(box);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, size * 3 / 8, stroke);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_align(base, LV_ALIGN_TOP_MID, 0, size * 7 / 8);
    return box;
}

static void color_mic(lv_obj_t *icon, uint32_t color)
{
    if (!icon) return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(icon); i++) {
        lv_obj_t *part = lv_obj_get_child(icon, i);
        lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
        lv_obj_set_style_arc_color(part, lv_color_hex(color), LV_PART_MAIN);
    }
}

static void set_mic_color(uint32_t color)
{
    color_mic(s_mic_icon, color);   /* legacy face icon only */
}

static void on_touch_mic(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        muse_input_touch_talk(true);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        muse_input_touch_talk(false);
        break;
    default:
        break;
    }
}

/* Icons beside the physical buttons, in place of an instruction caption. */
static void build_button_icons(lv_obj_t *face)
{
    if (muse_board->rim_controls) return;   /* dedicated rim hints below */
    const muse_button_hint_t *t = &muse_board->talk_hint, *a = &muse_board->aux_hint;
    s_mic_icon = make_mic(face, s_tall ? 24 : s_small ? 12 : 26);
    lv_obj_align(s_mic_icon, t->align, t->x, t->y);
    set_mic_color(COLOR_DIM);
    if (muse_board->touch_talk) {
        lv_obj_add_flag(s_mic_icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(s_mic_icon, 12);
        for (uint32_t i = 0; i < lv_obj_get_child_count(s_mic_icon); i++) {
            lv_obj_remove_flag(lv_obj_get_child(s_mic_icon, i), LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_add_event_cb(s_mic_icon, on_touch_mic, LV_EVENT_ALL, NULL);
    }

    /* Without touch the aux button opens the menu rather than sleeping. A board
     * that leaves aux_hint out has no button to put an icon beside. */
    if (a->align == LV_ALIGN_DEFAULT) {
        return;
    }
    s_aux_icon = make_label(face, s_small ? &lv_font_montserrat_14 : &lv_font_montserrat_28, COLOR_DIM);
    lv_label_set_text(s_aux_icon, muse_board->touch ? LV_SYMBOL_POWER : LV_SYMBOL_LIST);
    lv_obj_align(s_aux_icon, a->align, a->x, a->y);
}

static void on_canvas_clicked(lv_event_t *e)
{
    (void)e;
    /* Two taps: what she knows without the internet (time, sensors, weather) */
    static int64_t last_us;
    int64_t now = esp_timer_get_time();
    if (now - last_us < 450000) {
        last_us = 0;
        muse_state_poke();
        sagwa_sfx_noticed();
        sagwa_offline_show();
        return;
    }
    last_us = now;
    muse_state_make_happy();
    muse_state_poke();
    sagwa_sfx_pet_pulse();
}

static const lv_font_t *font_pick(const lv_font_t *full, const lv_font_t *compact)
{
    return s_small ? compact : full;
}

#if CONFIG_MUSE_CJK_FONT
LV_FONT_DECLARE(muse_font_cjk_16)
#endif

/* High-legibility Montserrat-20 for concise Sagwa replies, with CJK fallback. */
static const lv_font_t *caption_font(void)
{
#if CONFIG_MUSE_CJK_FONT
    static lv_font_t font;
    if (!font.get_glyph_dsc) {
        font = lv_font_montserrat_20;
        font.fallback = &muse_font_cjk_16;
    }
    return &font;
#else
    return &lv_font_montserrat_20;
#endif
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    return l;
}

static void show_speaker(bool on)
{
    /* Off is the one that stands out, like a lit flashlight button. */
    lv_obj_set_style_bg_color(s_speaker, lv_color_hex(on ? COLOR_METER_OFF : COLOR_LIT), 0);
    lv_obj_set_style_text_color(s_speaker_icon, lv_color_hex(on ? COLOR_DIM : 0x000000), 0);
    lv_label_set_text(s_speaker_icon, on ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE);
    s_shown_speaker = on;
}

static void set_speaker_size(void *obj, int32_t px)
{
    lv_obj_set_size(obj, px, px);
}

/* Swells over the long press, so the toggle lands as it reaches full size. */
static void speaker_grow(bool grow)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_speaker);
    lv_anim_set_exec_cb(&a, set_speaker_size);
    lv_anim_set_values(&a, lv_obj_get_width(s_speaker), SPEAKER_PX + (grow ? SPEAKER_GROW_PX : 0));
    lv_anim_set_duration(&a, grow ? SPEAKER_HOLD_MS : 150);
    lv_anim_start(&a);
}

/* Touch and hold to toggle, like the lock screen's flashlight; a tap only says so. */
static void on_speaker_event(lv_event_t *e)
{
    bool on = muse_settings_speaker_on();
    bool idle = muse_state_mode(NULL) == MUSE_MODE_IDLE;   /* don't cover a reply's captions */
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        speaker_grow(true);
        break;
    case LV_EVENT_LONG_PRESSED:
        muse_settings_set_speaker_on(!on);
        show_speaker(!on);
        if (idle) {
            muse_state_set_caption(on ? "SPEAKER OFF" : "SPEAKER ON");
        }
        break;
    case LV_EVENT_SHORT_CLICKED:
        if (idle) {
            muse_state_set_caption(on ? "HOLD TO MUTE" : "HOLD TO UNMUTE");
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        speaker_grow(false);
        break;
    default:
        break;
    }
}

/* Upper left by the status icons, where a thumb finds it without covering the reply. */
static void build_speaker(lv_obj_t *face, int x, int y)
{
    s_speaker = lv_obj_create(face);
    lv_obj_remove_style_all(s_speaker);
    lv_obj_set_size(s_speaker, SPEAKER_PX, SPEAKER_PX);
    lv_obj_set_style_radius(s_speaker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_speaker, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_speaker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(s_speaker, 12);   /* a fingertip is bigger than the circle */
    static const lv_event_code_t EVENTS[] = { LV_EVENT_PRESSED, LV_EVENT_LONG_PRESSED, LV_EVENT_SHORT_CLICKED,
                                              LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST };
    for (size_t i = 0; i < sizeof(EVENTS) / sizeof(EVENTS[0]); i++) {
        lv_obj_add_event_cb(s_speaker, on_speaker_event, EVENTS[i], NULL);
    }
    s_speaker_icon = make_label(s_speaker, &lv_font_montserrat_28, COLOR_DIM);
    lv_obj_center(s_speaker_icon);
    lv_obj_align(s_speaker, LV_ALIGN_CENTER, x, y);
    show_speaker(muse_settings_speaker_on());
}

static void set_canvas_px(int px)
{
    if ((int)s_muse_src.header.w == px) {
        return;
    }
    s_muse_src.header.w = px;
    s_muse_src.header.h = px;
    s_muse_src.header.stride = px * sizeof(uint16_t);
    muse_pixel_set_size(px);
    lv_image_set_src(s_canvas, &s_muse_src);   /* picks up the new size */
}

/* The avatar's own sideways position (Sagwa walks about), only at full size */
__attribute__((weak)) int muse_pixel_offset_x(void)
{
    return 0;
}

static int s_canvas_off;

__attribute__((weak)) void muse_pixel_activity(char *out, size_t cap)
{
    if (cap) out[0] = '\0';
}

/* What she's doing, small under her feet (full size only) */
static lv_obj_t *s_activity_lbl;
static char s_activity_text[48];

static void set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

/* Fades `obj` in over `ms` */
static void fade_in(lv_obj_t *obj, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void update_activity(void)
{
    if (!s_activity_lbl) {
        return;
    }
    char now[sizeof(s_activity_text)];
    muse_pixel_activity(now, sizeof(now));
    if (strcmp(now, s_activity_text) != 0) {
        strlcpy(s_activity_text, now, sizeof(s_activity_text));
        lv_label_set_text(s_activity_lbl, now);
        fade_in(s_activity_lbl, 350);
    }
    bool show = now[0] && s_answer < 0 && !lv_anim_get(s_canvas, NULL);
    lv_obj_set_flag(s_activity_lbl, LV_OBJ_FLAG_HIDDEN, !show);
    if (show) {
        lv_obj_align(s_activity_lbl, LV_ALIGN_BOTTOM_MID, s_muse_x + s_canvas_off, -4);
    }
}

static int canvas_off(void)
{
    return s_answer < 0 ? s_canvas_off : 0;
}

static void move_muse_t(void *obj, int32_t t)
{
    (void)obj;
    set_canvas_px(s_from_px + (s_to_px - s_from_px) * t / 256);
    s_muse_x = s_from_x + (s_to_x - s_from_x) * t / 256;
    s_muse_y = s_from_y + (s_to_y - s_from_y) * t / 256;
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, s_muse_x + canvas_off(), s_muse_y);
}

/* Eases Sagwa to `px` centred at `(x, y)` (e.g. top-right corner or center). */
static void move_muse(int px, int x, int y)
{
    if (px == s_to_px && x == s_to_x && y == s_to_y && s_muse_x == x && s_muse_y == y) {
        return;
    }
    s_from_px = s_muse_src.header.w;
    s_from_x = s_muse_x;
    s_from_y = s_muse_y;
    s_to_px = px;
    s_to_x = x;
    s_to_y = y;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_canvas);
    lv_anim_set_exec_cb(&a, move_muse_t);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, ANSWER_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* Switches to answer layout `which` (top-right corner + text), or back to center for -1. */
static void set_answer(int which)
{
    const answer_layout_t *l = which >= 0 ? &s_answers[which] : NULL;
    s_answer = which;
    if (l) {
        lv_obj_set_size(s_reply_box, l->w, l->h);
        lv_obj_align(s_reply_box, LV_ALIGN_CENTER, 0, l->top + l->h / 2);
        lv_obj_set_style_text_align(s_reply_lbl, l->align, 0);
    }
    move_muse(l ? l->px : s_canvas_px, l ? l->x : 0, l ? l->y : s_big_y);
}

static void add_hides(answer_layout_t *l, lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    for (size_t i = 0; i < sizeof(l->hides) / sizeof(l->hides[0]); i++) {
        if (!l->hides[i]) {
            l->hides[i] = obj;
            return;
        }
    }
}

#define THUMBS_MAX 6

typedef struct {
    uint16_t *px;
    int w, h;
    lv_image_dsc_t dsc;
    lv_obj_t *img;
} thumb_t;

static thumb_t s_thumbs[THUMBS_MAX];
static int s_nthumbs;
static thumb_t s_thumbs_in[THUMBS_MAX];   /* handed over, not yet shown */
static int s_nthumbs_in;
static portMUX_TYPE s_thumbs_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_thumbs_new;   /* a thumbnail arrived: keep the reply up for it */

void muse_ui_add_thumb(uint16_t *px, int w, int h)
{
    bool kept = false;
    portENTER_CRITICAL(&s_thumbs_lock);
    if (s_nthumbs_in < THUMBS_MAX) {
        s_thumbs_in[s_nthumbs_in++] = (thumb_t){ .px = px, .w = w, .h = h };
        kept = true;
    }
    portEXIT_CRITICAL(&s_thumbs_lock);
    if (!kept) {
        heap_caps_free(px);
    }
}

static void muse_ui_clear_thumbs(void)
{
    for (int i = 0; i < s_nthumbs; i++) {
        lv_obj_delete(s_thumbs[i].img);
        heap_caps_free(s_thumbs[i].px);
    }
    s_nthumbs = 0;
    if (s_thumb_row) {
        lv_obj_add_flag(s_thumb_row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* LVGL task: show what muse_ui_add_thumb handed over, oldest dropped past THUMBS_MAX */
static void take_thumbs(void)
{
    thumb_t in[THUMBS_MAX];
    int n;
    portENTER_CRITICAL(&s_thumbs_lock);
    n = s_nthumbs_in;
    memcpy(in, s_thumbs_in, sizeof(thumb_t) * n);
    s_nthumbs_in = 0;
    portEXIT_CRITICAL(&s_thumbs_lock);
    for (int i = 0; i < n; i++) {
        if (s_nthumbs == THUMBS_MAX) {
            lv_obj_delete(s_thumbs[0].img);
            heap_caps_free(s_thumbs[0].px);
            memmove(&s_thumbs[0], &s_thumbs[1], sizeof(thumb_t) * (THUMBS_MAX - 1));
            s_nthumbs--;
        }
        thumb_t *t = &s_thumbs[s_nthumbs++];
        *t = in[i];
        t->dsc = (lv_image_dsc_t){ 0 };
        t->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        t->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        t->dsc.header.w = t->w;
        t->dsc.header.h = t->h;
        t->dsc.header.stride = t->w * 2;
        t->dsc.data = (const uint8_t *)t->px;
        t->dsc.data_size = t->w * t->h * 2;
        t->img = lv_image_create(s_thumb_row);
        lv_image_set_src(t->img, &t->dsc);
        lv_obj_set_style_radius(t->img, 8, 0);
        lv_obj_set_style_clip_corner(t->img, true, 0);
        lv_obj_set_style_border_width(t->img, 1, 0);
        lv_obj_set_style_border_color(t->img, lv_color_hex(0x3a352f), 0);
        lv_obj_add_flag(t->img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(s_thumb_row, LV_OBJ_FLAG_HIDDEN);
        s_thumbs_new = true;
    }
}

static float ui_now(void)
{
    return (float)(esp_timer_get_time() / 1000) / 1000.0f;
}

/* A finger on the reply: the reader leads, and the reply stays up a while. */
static void on_reply_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL && !lv_indev_active()) {
        return;   /* our own follow scroll */
    }
    float now = ui_now();
    s_user_scroll_until = now + REPLY_HOLD_S;
    if (s_reply_until < now + REPLY_LINGER_S) {
        s_reply_until = now + REPLY_LINGER_S;
    }
}

/*
 * Sagwa minimalist answer layout:
 * When reply text appears, Sagwa glides smoothly to the top-right corner (86x86)
 * while the concise reply text appears cleanly below in warm white Montserrat-20.
 */
static void build_answer(lv_obj_t *face, int ring_in)
{
    (void)ring_in;
    int spk_r = (SPEAKER_PX + SPEAKER_GROW_PX) / 2;
    int spk_x = -s_w / 2 + 8 + spk_r, spk_y = -s_h / 2 + 8 + spk_r;
    const lv_font_t *font = caption_font();
    int cap_w = muse_board->rim_controls ? 280 : s_w;
    (void)cap_w;
    add_hides(&s_answers[0], NULL);

    int corner_px = 86;
    int corner_x  = s_w / 2 - corner_px / 2 - 6;    /* +111 on 320x240 -> x: 228..314 */
    int corner_y  = -s_h / 2 + corner_px / 2 + 6;   /* -71 on 320x240  -> y: 6..92 */
    int text_w    = s_w - 32;                       /* 288 px wide (16 px margins) */
    int text_top  = -s_h / 2 + corner_px + 10;      /* y = 96 on 320x240 */
    int text_h    = s_h / 2 - 10 - text_top;        /* 134 px tall (4 lines of 20px font) */

    for (int k = 0; k < 2; k++) {
        answer_layout_t *l = &s_answers[k];
        l->px = corner_px;
        l->x  = corner_x;
        l->y  = corner_y;
        l->cols = 26;
        l->lines = 4;
        l->w = text_w;
        l->h = text_h;
        l->top = text_top;
        l->align = LV_TEXT_ALIGN_LEFT;
    }

    ESP_LOGI(TAG, "Sagwa reply layout: %d cols x %d lines, corner (%d, %d) @ %dpx",
             s_answers[0].cols, s_answers[0].lines, corner_x, corner_y, corner_px);

    /* The reply scrolls up and down under a finger; the page swipe stays sideways. */
    s_reply_box = lv_obj_create(face);
    lv_obj_remove_style_all(s_reply_box);
    lv_obj_set_size(s_reply_box, text_w, text_h);
    lv_obj_set_scroll_dir(s_reply_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_reply_box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(s_reply_box, lv_color_hex(COLOR_DIM), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_reply_box, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_reply_box, 3, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_reply_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_reply_box, 10, 0);
    lv_obj_set_style_pad_bottom(s_reply_box, 8, 0);
    lv_obj_add_event_cb(s_reply_box, on_reply_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_reply_box, on_reply_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_reply_box, on_reply_touch, LV_EVENT_SCROLL, NULL);
    lv_obj_add_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);

    s_reply_lbl = make_label(s_reply_box, font, COLOR_CAPTION);
    lv_obj_set_width(s_reply_lbl, lv_pct(100));
    lv_obj_set_style_text_line_space(s_reply_lbl, 4, 0);
    lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_remove_flag(s_reply_lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reply_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_thumb_row = lv_obj_create(s_reply_box);
    lv_obj_remove_style_all(s_thumb_row);
    lv_obj_set_size(s_thumb_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_thumb_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(s_thumb_row, 8, 0);
    lv_obj_set_style_pad_row(s_thumb_row, 8, 0);
    lv_obj_remove_flag(s_thumb_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_thumb_row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_thumb_row, LV_OBJ_FLAG_HIDDEN);

    if (muse_board->touch && !muse_board->rim_controls) {
        build_speaker(face, spk_x, spk_y);
        lv_obj_add_flag(s_speaker, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * A full circle's arc masks every pixel of every row it's asked to draw, hole
 * and corners included, which costs a full-screen redraw (a swipe) several ms.
 * So the ring is drawn a slab of rows at a time, each only as wide as the ring
 * is there: where a slab crosses the hole, a left piece and a right one.
 */
#define RING_SLAB_ROWS 52

static void on_ring_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    lv_area_t c;
    lv_obj_get_coords(s_ring, &c);
    int32_t cx = (c.x1 + c.x2) / 2;
    int32_t cy = (c.y1 + c.y2) / 2;
    int32_t out = lv_area_get_width(&c) / 2 + 1;                                     /* with antialiasing */
    int32_t hole = out - 1 - lv_obj_get_style_arc_width(s_ring, LV_PART_MAIN) - 2;   /* without */
    lv_event_stop_processing(e);
    for (int32_t y = LV_MAX(clip.y1, cy - out); y <= LV_MIN(clip.y2, cy + out); y += RING_SLAB_ROWS) {
        int32_t y2 = LV_MIN(y + RING_SLAB_ROWS - 1, clip.y2);
        int32_t near = y <= cy && cy <= y2 ? 0 : LV_MIN(LV_ABS(y - cy), LV_ABS(y2 - cy));
        int32_t far = LV_MAX(LV_ABS(y - cy), LV_ABS(y2 - cy));
        int32_t ow = near < out ? (int32_t)ceilf(sqrtf((float)(out * out - near * near))) : 0;
        int32_t iw = far < hole ? (int32_t)sqrtf((float)(hole * hole - far * far)) : 0;
        lv_area_t pieces[2] = {
            { cx - ow, y, iw ? cx - iw : cx + ow, y2 },
            { cx + iw, y, cx + ow, y2 },
        };
        for (int i = 0; i < (iw ? 2 : 1); i++) {
            if (lv_area_intersect(&layer->_clip_area, &clip, &pieces[i])) {
                lv_obj_event_base(NULL, e);   /* the arc's own drawing */
            }
        }
    }
    layer->_clip_area = clip;
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *face = scr;
    if (muse_board->touch) {
        /* Swipe left from Muse for settings. */
        s_tv = lv_tileview_create(scr);
        lv_obj_set_style_bg_color(s_tv, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_tv, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
        s_face = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
        /* It never scrolls, but LVGL would size its scrollbars from all its
         * children every time it draws any part of it. */
        lv_obj_set_scrollbar_mode(s_face, LV_SCROLLBAR_MODE_OFF);
        /* Match the avatar's black canvas; the tile's theme background otherwise
         * shows as a different-coloured square around the character. */
        lv_obj_set_style_bg_color(s_face, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_face, LV_OPA_COVER, 0);
        s_settings = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_LEFT);
        lv_obj_add_event_cb(s_tv, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
        face = s_face;
    }

    if (!s_small && muse_board->no_ring) {
        /* A square panel: progress along the bottom edge, as the compact
         * layout has it, rather than a circle around Muse. */
        s_bar = lv_obj_create(face);
        lv_obj_remove_style_all(s_bar);
        lv_obj_set_size(s_bar, 0, 6);
        lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
        lv_obj_align(s_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    } else if (!s_small) {
        /* Progress ring around the bezel. */
        int d = (s_w < s_h ? s_w : s_h) - 8;
        s_ring = lv_arc_create(face);
        lv_obj_set_size(s_ring, d, d);
        lv_obj_center(s_ring);
        lv_arc_set_bg_angles(s_ring, 0, 360);
        lv_arc_set_rotation(s_ring, 270);
        lv_arc_set_range(s_ring, 0, RING_RANGE);
        lv_arc_set_value(s_ring, 0);
        lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(s_ring, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_ring, lv_color_hex(COLOR_RING_BG), LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_ring, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(s_ring, false, LV_PART_INDICATOR);
        lv_obj_add_event_cb(s_ring, on_ring_draw, LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS, NULL);
    }

    /*
     * Full layout, from the bottom up (offsets from the centre): two caption
     * lines as low as the ring leaves them room, the level meter, then Muse,
     * whose blank bottom rows can tuck in behind the meter.
     */
    int ring_in = (s_w < s_h ? s_w : s_h) / 2 - 10;   /* the ring's inner edge */
    int cap_h = 2 * lv_font_get_line_height(&lv_font_unscii_16) + CAPTION_LINE_SPACE;
    int cap_bottom = 179;                              /* a 466 px circle's; fine for rectangles */
    if (muse_board->round) {
        cap_bottom = (int)sqrtf((float)(ring_in * ring_in - CAPTION_W * CAPTION_W / 4)) - 3;
    }
    int cap_top = cap_bottom - cap_h;
    int meter_y = cap_top - 6 - METER_SEG_PX / 2;
    s_big_y = 2;
    if (muse_board->avatar_px == s_h) {
        s_big_y = -10;   /* a canvas the screen's height: room under her feet for what she's doing */
    }

    /* The character (Sagwa). */
    muse_image_init();
    s_canvas = lv_image_create(face);
    lv_image_set_src(s_canvas, &s_muse_src);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, s_big_y);
    s_muse_x = 0;
    s_muse_y = s_big_y;
    s_to_px = s_canvas_px;
    s_to_x = 0;
    s_to_y = s_big_y;
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, on_canvas_clicked, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_canvas, on_canvas_clicked, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_canvas, on_canvas_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(face, on_canvas_clicked, LV_EVENT_CLICKED, NULL);

    s_activity_lbl = make_label(face, &lv_font_montserrat_14, COLOR_ACTIVITY);
    lv_obj_set_style_text_letter_space(s_activity_lbl, 1, 0);
    lv_obj_add_flag(s_activity_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_ring) {
        lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_bar) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
    build_button_icons(face);
    if (s_mic_icon) lv_obj_add_flag(s_mic_icon, LV_OBJ_FLAG_HIDDEN);
    if (s_aux_icon) lv_obj_add_flag(s_aux_icon, LV_OBJ_FLAG_HIDDEN);

    /* Status line: kept hidden on Sagwa's pure black face screen. */
    lv_obj_t *status = lv_obj_create(face);
    lv_obj_remove_style_all(status);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, s_small ? 4 : 8, 0);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, muse_board->rim_controls ? 40 : s_small ? 1 : 20 + s_dy);
    s_wifi_icon = make_label(status, &lv_font_montserrat_14, COLOR_DIM);
    s_ble_icon = make_label(status, &lv_font_montserrat_14, COLOR_DIM);
    s_power_lbl = make_label(status, &lv_font_unscii_8, COLOR_DIM);
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);

    s_state_lbl = make_label(face, s_small ? &lv_font_unscii_8 : &lv_font_unscii_16, 0xffffff);
    lv_obj_set_style_text_letter_space(s_state_lbl, s_small ? 1 : 2, 0);
    lv_obj_align(s_state_lbl, LV_ALIGN_TOP_MID, 0, muse_board->rim_controls ? 64 : s_small ? 22 : 40 + s_dy);
    lv_obj_add_flag(s_state_lbl, LV_OBJ_FLAG_HIDDEN);

    s_name_lbl = make_label(face, s_small ? &lv_font_unscii_8 : &lv_font_unscii_16, COLOR_DIM);
    lv_obj_align(s_name_lbl, LV_ALIGN_TOP_MID, 0, muse_board->rim_controls ? 88 : s_small ? 32 : 60 + s_dy);
    lv_obj_add_flag(s_name_lbl, LV_OBJ_FLAG_HIDDEN);

    s_caption_lbl = make_label(face, font_pick(caption_font(), &lv_font_unscii_8), COLOR_CAPTION);
    lv_obj_add_flag(s_caption_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Chunky level meter (kept hidden on Sagwa's minimalist face). */
    int span = METER_SEGS * (METER_SEG_PX + METER_GAP_PX) - METER_GAP_PX;
    for (int i = 0; i < METER_SEGS; i++) {
        lv_obj_t *seg = lv_obj_create(face);
        lv_obj_remove_style_all(seg);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(seg, METER_SEG_PX, METER_SEG_PX);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(COLOR_METER_OFF), 0);
        int x = -span / 2 + i * (METER_SEG_PX + METER_GAP_PX) + METER_SEG_PX / 2;
        lv_obj_align(seg, LV_ALIGN_CENTER, x, meter_y);
        lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
        s_meter[i] = seg;
    }

    build_answer(face, ring_in);
}


static void on_cover_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        /* Wake on touch; keep the (now clear) cover until release so the tap
         * doesn't land on whatever is underneath. */
        muse_state_set_asleep(false);
        lv_obj_set_style_bg_opa(s_cover, LV_OPA_TRANSP, 0);
    } else if (!muse_state_asleep()) {
        lv_obj_add_flag(s_cover, LV_OBJ_FLAG_HIDDEN);
    }
}

/* With the display lock held. */
static void image_hide_locked(void)
{
    xSemaphoreTake(s_image_mutex, portMAX_DELAY);
    if (s_image_buf) {
        lv_obj_add_flag(s_image, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(s_image, NULL);
        heap_caps_free(s_image_buf);
        s_image_buf = NULL;
        s_image_dsc.data = NULL;
        s_image_dirty = false;
    }
    xSemaphoreGive(s_image_mutex);
}

/* Each frame: shows a new image and redraws what the download changed. */
static void image_sync(void)
{
    xSemaphoreTake(s_image_mutex, portMAX_DELAY);
    if (s_image_buf && !s_image_dsc.data) {
        s_image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_image_dsc.header.w = s_w;
        s_image_dsc.header.h = s_h;
        s_image_dsc.header.stride = s_w * sizeof(uint16_t);
        s_image_dsc.data_size = (size_t)s_w * s_h * sizeof(uint16_t);
        s_image_dsc.data = (const uint8_t *)s_image_buf;
        muse_menu_close();
        lv_image_set_src(s_image, &s_image_dsc);
        lv_obj_remove_flag(s_image, LV_OBJ_FLAG_HIDDEN);
        muse_state_set_asleep(false);
    }
    if (s_image_dirty) {
        lv_obj_invalidate_area(s_image, &s_image_area);
        s_image_dirty = false;
    }
    xSemaphoreGive(s_image_mutex);
}

static void on_image_clicked(lv_event_t *e)
{
    (void)e;
#if CONFIG_MUSE_WATCHER_CAMERA
    if (watcher_camera_preview_active()) {
        watcher_camera_preview_toggle();
        return;
    }
#endif
    image_hide_locked();
}

#if CONFIG_MUSE_WATCHER_CAMERA
static void on_camera_hint_clicked(lv_event_t *e)
{
    (void)e;
    if (watcher_camera_preview_active()) watcher_camera_preview_toggle();
}
#endif

static void on_pair_clicked(lv_event_t *e)
{
    (void)e;
    if (muse_link_state() == MUSE_LINK_CONFIRM) {
        if (!muse_link_talk_press()) {
            muse_input_touch_talk(true);
            muse_input_touch_talk(false);
        }
    }
    muse_state_poke();
}

static void on_any_press(lv_event_t *e)
{
    (void)e;
    if (muse_link_state() == MUSE_LINK_CONFIRM) {
        if (!muse_link_talk_press()) {
            muse_input_touch_talk(true);
            muse_input_touch_talk(false);
        }
    }
    muse_state_poke();
}

static lv_obj_t *make_control_icon(lv_obj_t *scr, const muse_button_hint_t *pos,
                                   const char *symbol, uint32_t color)
{
    lv_obj_t *icon = make_label(scr, &lv_font_montserrat_28, color);
    lv_obj_set_size(icon, 36, 36);
    lv_obj_set_style_bg_color(icon, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icon, 6, 0);
    lv_label_set_long_mode(icon, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(icon, symbol);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(icon, pos->align, pos->x, pos->y);
    return icon;
}

/* Icons only beside the physical controls, including before pairing. */
static void update_control_hints(muse_mode_t mode)
{
    if (!s_ptt_hint) return;
    /* Don't obscure settings rows/keyboards with floating physical icons. */
    bool settings = s_tv && lv_tileview_get_tile_active(s_tv) == s_settings;
    lv_obj_set_flag(s_ptt_hint, LV_OBJ_FLAG_HIDDEN, settings);
    lv_obj_set_flag(s_spk_hint, LV_OBJ_FLAG_HIDDEN, settings);
    lv_obj_set_flag(s_pwr_hint, LV_OBJ_FLAG_HIDDEN, settings);
    static int last_mode = -1;
    if ((int)mode != last_mode) {
        color_mic(s_ptt_hint, mode == MUSE_MODE_LISTENING ? 0x64d4ff : 0x80bfff);
        last_mode = (int)mode;
    }
    bool speaker_on = muse_settings_speaker_on();
    const char *speaker = speaker_on ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_MUTE;
    if (strcmp(speaker, lv_label_get_text(s_spk_hint))) {
        lv_label_set_text(s_spk_hint, speaker);
        lv_obj_set_style_text_color(s_spk_hint, lv_color_hex(speaker_on ? 0xffd060 : 0x8b7845), 0);
    }
}

static void build_overlays(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* Page dots. */
    for (int i = 0; i < 2 && s_tv; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COLOR_DOT_OFF), 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, i ? 8 : -8, -14);
        s_dots[i] = d;
    }

    /* A downloaded image: over everything on the screen (and in snapshots),
     * under the pairing code and sleep cover on the top layer. */
    s_image = lv_image_create(scr);
    lv_obj_set_pos(s_image, 0, 0);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_image, on_image_clicked, LV_EVENT_CLICKED, NULL);
#if CONFIG_MUSE_WATCHER_CAMERA
    s_camera_hint = lv_btn_create(scr);
    lv_obj_set_size(s_camera_hint, 244, 46);
    lv_obj_align(s_camera_hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(s_camera_hint, lv_color_hex(0x201a35), 0);
    lv_obj_set_style_bg_opa(s_camera_hint, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_camera_hint, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_camera_hint, 2, 0);
    lv_obj_set_style_radius(s_camera_hint, 18, 0);
    lv_obj_add_flag(s_camera_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *hint_text = lv_label_create(s_camera_hint);
    lv_label_set_text(hint_text, "TAP TO TAKE PHOTO");
    lv_obj_center(hint_text);
    lv_obj_add_event_cb(s_camera_hint, on_camera_hint_clicked, LV_EVENT_CLICKED, NULL);
#endif

    if (muse_board->rim_controls) {
        /* Dedicated ownership: legacy chrome/reply hides must never claim it. */
        s_ptt_hint = make_mic(scr, 26);
        lv_obj_align(s_ptt_hint, muse_board->talk_hint.align, muse_board->talk_hint.x, muse_board->talk_hint.y);
        s_spk_hint = make_control_icon(scr, &muse_board->speaker_hint, LV_SYMBOL_VOLUME_MID, 0xffd060);
        s_pwr_hint = make_control_icon(scr, &muse_board->power_hint, LV_SYMBOL_POWER, 0xff8080);
        update_control_hints(MUSE_MODE_BOOT);
    }

    /* BLE pairing code, or the Muse app's ask for the talk button: a centred
     * column in one typeface, the code large. The small card grows with the hint. */
    s_pair = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pair);
    lv_obj_set_size(s_pair, s_small ? s_w - 8 : 300, s_small ? LV_SIZE_CONTENT : 150);
    lv_obj_center(s_pair);
    lv_obj_set_flex_flow(s_pair, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pair, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(s_pair, s_small ? 10 : 18, 0);
    lv_obj_set_style_pad_hor(s_pair, s_small ? 6 : 16, 0);
    lv_obj_set_style_pad_row(s_pair, s_small ? 4 : 10, 0);
    lv_obj_set_style_radius(s_pair, s_small ? 10 : 24, 0);
    lv_obj_set_style_bg_opa(s_pair, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_pair, lv_color_hex(0x1a1530), 0);
    lv_obj_set_style_border_color(s_pair, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_pair, 2, 0);
    lv_obj_remove_flag(s_pair, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pair, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_pair, on_pair_clicked, LV_EVENT_CLICKED, NULL);
    s_pair_title = make_label(s_pair, font_pick(&lv_font_montserrat_20, FONT_COMPACT), COLOR_LIT);
    lv_label_set_text(s_pair_title, "Pairing code");
    s_pair_code = make_label(s_pair, font_pick(&lv_font_montserrat_28, &lv_font_montserrat_20), COLOR_ACCENT);
    lv_obj_set_style_text_letter_space(s_pair_code, s_small ? 2 : 6, 0);
    s_pair_hint = make_label(s_pair, font_pick(&lv_font_montserrat_14, FONT_COMPACT), COLOR_DIM);
    lv_label_set_text(s_pair_hint, s_small ? "Enter on phone" : "Enter it on your phone");
    /* Wraps: "bottom right button" is wider than the AIPI's card. */
    lv_obj_set_width(s_pair_hint, lv_pct(100));
    lv_label_set_long_mode(s_pair_hint, LV_LABEL_LONG_MODE_WRAP);

    /* Sleep cover: swallows the waking touch. */
    s_cover = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_cover);
    lv_obj_set_size(s_cover, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_cover, lv_color_black(), 0);
    lv_obj_add_flag(s_cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_cover, on_cover_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_cover, on_cover_event, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_cover, on_cover_event, LV_EVENT_PRESS_LOST, NULL);

    if (s_indev) {
        lv_indev_add_event_cb(s_indev, on_any_press, LV_EVENT_PRESSED, NULL);
    }
}

static void apply_brightness(int pct)
{
    if (pct != s_brightness) {
        muse_board->set_brightness(pct);
        s_brightness = pct;
    }
}

bool muse_ui_dark(void)
{
    return s_dark;
}

/* Returns true while asleep (skip rendering). */
static bool update_sleep(void)
{
    bool asleep = muse_state_asleep();
    if (asleep && !s_dark) {
        muse_menu_close();
        apply_brightness(0);
        if (muse_board->panel_sleep) {
            muse_board->panel_sleep(true);
        }
        lv_obj_set_style_bg_opa(s_cover, LV_OPA_COVER, 0);
        lv_obj_remove_flag(s_cover, LV_OBJ_FLAG_HIDDEN);
        s_dark = true;
    } else if (!asleep && s_dark) {
        s_dark = false;
        if (muse_board->panel_sleep) {
            muse_board->panel_sleep(false);
        }
        /* Woken by a button: drop the cover now. Woken by touch: on release. */
        if (lv_obj_get_style_bg_opa(s_cover, 0) == LV_OPA_COVER) {
            lv_obj_add_flag(s_cover, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!s_dark) {
        int target = muse_settings_brightness();
        if (s_preview_brightness >= 0) {
            if (s_preview_brightness == target) {
                s_preview_brightness = -1;
            } else {
                target = s_preview_brightness;
            }
        }
        apply_brightness(target);
    }
    return s_dark;
}

static void update_chrome(float now)
{
    if (now < s_next_settings_tick) {
        return;
    }
    s_next_settings_tick = now + SETTINGS_TICK_S;

    if (s_tv) {
        int page = lv_tileview_get_tile_active(s_tv) == s_settings;
        bool subpage = muse_settings_ui_in_subpage();
        bool swipe = !page || !subpage;
        if (swipe != lv_obj_has_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE)) {
            lv_obj_set_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE, swipe);
        }
        int shown = page * 2 + subpage;
        if (shown != s_shown_page) {
            for (int i = 0; i < 2; i++) {
                lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(i == page ? COLOR_ACCENT : COLOR_DOT_OFF), 0);
                /* Hide dots on Sagwa's pure black face screen (page == 0) */
                lv_obj_set_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN, !page || subpage);
            }
            s_shown_page = shown;
        }
        muse_settings_ui_tick(lv_obj_get_scroll_x(s_tv) > 0);
    }

    muse_ble_status_t b;
    muse_ble_status(&b);

    if (muse_board->epaper) {
        bool paired = muse_link_hatch_linked() || muse_settings_hatch_token_len() > 0;
        bool mic = paired && (muse_board->audio_init || muse_board->mic_init);
        lv_obj_set_flag(s_mic_icon, LV_OBJ_FLAG_HIDDEN, !mic);
        lv_obj_set_flag(s_speaker, LV_OBJ_FLAG_HIDDEN, !paired || !muse_board->audio_init);
    }

    /* The same card asks for the talk button when the Muse app pairs. */
    bool confirm = !b.passkey && muse_link_state() == MUSE_LINK_CONFIRM;
    if (b.passkey || confirm) {
        char code[24], hint[40];
        if (confirm) {
            if (muse_board->touch_talk || !muse_board->audio_init) {
                strlcpy(code, "Tap screen", sizeof(code));
                strlcpy(hint, "Tap to confirm pairing", sizeof(hint));
            } else if (muse_board->touch) {
                strlcpy(code, "Tap or press", sizeof(code));
                snprintf(hint, sizeof(hint), "Tap card or press %s button", muse_board->talk_button);
            } else {
                strlcpy(code, s_small ? "Press" : "Press button", sizeof(code));
                snprintf(hint, sizeof(hint), s_small ? "%s button" : "Press the %s button", muse_board->talk_button);
            }
        } else {
            snprintf(code, sizeof(code), "%06lu", (unsigned long)b.passkey);
            strlcpy(hint, s_small ? "Enter on phone" : "Enter it on your phone", sizeof(hint));
        }
        const char *title = confirm ? (s_small ? "Muse app" : "Pair with Muse app") : "Pairing code";
        if (strcmp(code, lv_label_get_text(s_pair_code)) != 0) {
            lv_label_set_text(s_pair_code, code);
            lv_label_set_text(s_pair_title, title);
            lv_label_set_text(s_pair_hint, hint);
        }
    }
    lv_obj_set_flag(s_pair, LV_OBJ_FLAG_HIDDEN, !b.passkey && !confirm);
}

static bool is_internal_status_caption(const char *s, muse_mode_t mode)
{
    if (!s || !s[0]) return true;
    if (!strncmp(s, "LISTENING", 9) || !strncmp(s, "RECORDING", 9)) return true;
    if (!strcmp(s, "SENDING VOICE NOTE") || !strcmp(s, "SENDING SAVED NOTE")) return true;
    if (!strcmp(s, "NOTE SENT - WAITING FOR MUSE")) return true;
    if (!strcmp(s, "SAVED, WILL TRY AGAIN") || !strcmp(s, "SAVED, SENDS WHEN ONLINE")) return true;
    if (!strcmp(s, "SAVED NOTE SENT")) return true;
    /* While thinking, keep Sagwa centered until the actual reply arrives */
    if (s[0] == '"' && mode == MUSE_MODE_THINKING) return true;
    return false;
}

static void update_status(muse_mode_t mode, float now)
{
    (void)now;
    static char caption[MUSE_CAPTION_MAX];
    bool fresh = muse_state_caption(caption, sizeof(caption), &s_caption_version);
    bool has_text = !is_internal_status_caption(caption, mode);
    int answer = -1;
    if (s_reply_lbl) {
        int layout = muse_board->audio_init && muse_settings_speaker_on() ? ANSWER_HEARD : ANSWER_READ;
        if (layout != s_page_for) {
            muse_state_set_page(s_answers[layout].cols, s_answers[layout].lines);
            s_page_for = layout;
        }
        if (has_text) {
            answer = layout;
        }
    }
    if (!s_reply_box) {
        return;
    }
    /*
     * The whole reply when there is one, scrollable, with its thumbnails; a
     * status or error caption otherwise. A finished reply stays up to be read.
     */
    static EXT_RAM_BSS_ATTR char reply[MUSE_REPLY_MAX];
    take_thumbs();
    bool new_reply = muse_state_reply(reply, sizeof(reply), &s_reply_version);
    if (s_thumbs_new) {
        s_thumbs_new = false;
        s_reply_until = now + REPLY_LINGER_S;
    }
    if (new_reply) {
        if (!reply[0]) {
            muse_ui_clear_thumbs();
        }
        s_reply_until = now + REPLY_LINGER_S;
        if (reply[0] && !s_reply_has) {
            s_follow_y = -1;
            lv_obj_scroll_to_y(s_reply_box, 0, LV_ANIM_OFF);
        }
    }
    bool thumbs = !lv_obj_has_flag(s_thumb_row, LV_OBJ_FLAG_HIDDEN);
    s_reply_has = reply[0] || thumbs;
    bool busy = mode == MUSE_MODE_THINKING || mode == MUSE_MODE_SPEAKING;
    if (busy && s_reply_has) {
        s_reply_until = now + REPLY_LINGER_S;
    }
    bool show_reply = s_reply_has && mode != MUSE_MODE_LISTENING && (busy || now < s_reply_until);
    bool show_caption = !show_reply && has_text;
    if ((show_reply || show_caption) && answer < 0) {
        answer = ANSWER_READ;
    }
    if (!show_reply && !show_caption) {
        answer = -1;
    }
    if (answer != s_answer) {
        set_answer(answer);
        fresh = new_reply = true;
    }
    if (show_reply && new_reply) {
        lv_label_set_text(s_reply_lbl, reply);
    } else if (show_caption && fresh) {
        lv_label_set_text(s_reply_lbl, caption);
        lv_obj_add_flag(s_thumb_row, LV_OBJ_FLAG_HIDDEN);
    }
    bool was_hidden = lv_obj_has_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN, !show_reply && !show_caption);
    if (was_hidden && (show_reply || show_caption)) {
        fade_in(s_reply_box, 280);
    }
    lv_obj_add_flag(s_caption_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Follow the speech down the reply until the reader takes over (not on
     * e-paper, where every scroll is a refresh: the reader scrolls there) */
    if (show_reply && reply[0] && now >= s_user_scroll_until && !muse_board->epaper) {
        lv_obj_update_layout(s_reply_box);
        int max_y = lv_obj_get_scroll_bottom(s_reply_box) + lv_obj_get_scroll_y(s_reply_box);
        if (max_y > 0) {
            /* The reply is ASCII (muse_text_to_ascii): bytes are letters */
            uint32_t at = (uint32_t)(muse_state_reply_pos() * (float)strlen(reply));
            lv_point_t p;
            lv_label_get_letter_pos(s_reply_lbl, at, &p);
            int target = p.y - lv_obj_get_height(s_reply_box) / 3;
            target = target < 0 ? 0 : target > max_y ? max_y : target;
            if (s_follow_y < 0 || abs(target - s_follow_y) >= 12) {
                lv_obj_scroll_to_y(s_reply_box, target, LV_ANIM_ON);
                s_follow_y = target;
            }
        }
    }
}

static volatile bool s_snapshot;
#if LV_USE_SNAPSHOT
static volatile int s_bench_page = -1;
void muse_ui_bench_page(bool settings)
{
    s_bench_page = settings ? 1 : 0;   /* applied in the LVGL task */
}

void muse_ui_bench_sagwa(void)
{
    s_bench_page = 2;
}
#endif

/* Streams the screen over the USB cable as base64 RGB565 (bench testing; needs
 * LV_USE_SNAPSHOT, which devices/sdkconfig.muse-bench turns on). */
static void send_snapshot(void)
{
#if LV_USE_SNAPSHOT
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!buf) {
        return;
    }
    enum { RAW = 144 };   /* lines must fit the driver's 256-byte TX ring */
    char hdr[48];
    int n = snprintf(hdr, sizeof(hdr), "\nSNAP BEGIN %d %d %d\n", (int)buf->header.w, (int)buf->header.h, RAW);
    muse_console_write(hdr, n);
    static unsigned char b64[4 * RAW / 3 + 4];
    for (uint32_t y = 0; y < buf->header.h; y++) {
        const unsigned char *p = buf->data + y * buf->header.stride;
        size_t left = buf->header.w * 2;
        while (left) {
            size_t chunk = left > RAW ? RAW : left, olen;
            mbedtls_base64_encode(b64, sizeof(b64), &olen, p, chunk);
            b64[olen++] = '\n';
            muse_console_write(b64, olen);
            p += chunk;
            left -= chunk;
        }
    }
    muse_console_write("SNAP END\n", 9);
    lv_draw_buf_destroy(buf);
#else
    muse_console_write("\nSNAP OFF\n", 10);   /* so snap.py can say why */
#endif
}

void muse_ui_request_snapshot(void)
{
    s_snapshot = true;
}

static void frame_tick(lv_timer_t *timer)
{
#if LV_USE_SNAPSHOT
    if (s_bench_page >= 0) {
        bool settings = s_bench_page != 0;
        bool sagwa = s_bench_page == 2;
        s_bench_page = -1;
        if (s_tv) lv_tileview_set_tile(s_tv, settings ? s_settings : s_face, LV_ANIM_OFF);
        if (sagwa) muse_settings_ui_bench_sagwa();
    }
#endif
    update_control_hints(muse_state_mode(NULL));
    if (s_snapshot) {
        s_snapshot = false;
        send_snapshot();
    }
    (void)timer;
    image_sync();
    float mode_t;
    muse_mode_t mode = muse_state_mode(&mode_t);
    float now = (float)esp_timer_get_time() / 1e6f;

    if (mode != s_last_mode) {
        if (mode == MUSE_MODE_LISTENING) {
            image_hide_locked();
            muse_ui_show_face();
        }
        s_last_mode = mode;
    }
    if (update_sleep()) {
        return;
    }
#if CONFIG_MUSE_VIDEO
    if (muse_video_tick()) {
        muse_menu_close();
        return;   /* video covers everything */
    }
#endif
    update_chrome(now);
    if (muse_menu_tick(now)) {
        image_hide_locked();
        return;   /* the menu covers the face */
    }
    if (s_image_dsc.data) {
        return;   /* the image covers the face */
    }
    if (s_tv && lv_obj_get_scroll_x(s_tv) != 0) {
        /* Off screen, or sliding to or from settings: hold still so the
         * slide gets the whole frame time. */
        return;
    }

    /* Fast attack, slow release keeps the mouth and meter lively but readable. */
    float level = muse_state_level();
    s_level += (level - s_level) * (level > s_level ? 0.6f : 0.2f);

    float happy = muse_state_happiness();
    if (sagwa_sfx_is_purring() && happy < 0.85f) {
        happy = 0.85f;
    }
    muse_pose_t pose = {
        .mode = mode,
        .t = now,
        .mode_t = mode_t,
        .level = s_level,
        .happy = happy,
    };
    if (!muse_board->epaper || mode != s_last_epaper_pose) {
        muse_pixel_render(&pose);
        int off = muse_pixel_offset_x();
        if (off != s_canvas_off) {
            s_canvas_off = off;
            if (s_answer < 0 && !lv_anim_get(s_canvas, NULL)) {
                lv_obj_align(s_canvas, LV_ALIGN_CENTER, s_muse_x + off, s_muse_y);
            }
        }
        update_activity();
        int cur_rig = muse_pixel_get_rig();
        if (cur_rig != s_last_rig) {
            s_last_rig = cur_rig;
            sagwa_sfx_on_rig_change(cur_rig, mode);
        }
        invalidate_muse();
        s_last_epaper_pose = mode;
    }

    update_status(mode, now);
}

esp_err_t muse_ui_start(void)
{
    s_w = muse_board->width;
    s_h = muse_board->height;
    s_small = s_h < 200 || s_w < 200;
    s_tall = s_small && s_h >= s_w + 64;
    /* Full-resolution 168x168 Sagwa canvas on 320x240 M5Stack CoreS3 */
    s_canvas_px = s_small ? s_h * 3 / 4 : 168;
    if (muse_board->avatar_px > 0) {
        s_canvas_px = muse_board->avatar_px;
    }
    if (s_canvas_px > s_w) {
        s_canvas_px = s_w;
    }
    s_dy = (s_h - 466) / 2;
    if (!s_small && muse_board->round && s_dy < 0) {
        s_canvas_px = (MUSE_PX_W * 5 + 2 * s_dy) / MUSE_PX_W * MUSE_PX_W;
        s_dy = 0;
    }

    lv_display_t *disp = muse_board->display_start(&s_indev);
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return ESP_FAIL;
    }

    s_image_mutex = xSemaphoreCreateMutex();
    muse_board->display_lock(-1);
    build_screen();
    if (s_settings) {
        muse_settings_ui_build(s_settings);
    } else {
        muse_menu_build(lv_screen_active(), s_w, s_h);
    }
    build_overlays();
    lv_timer_create(frame_tick, muse_board->frame_ms, NULL);
    s_ready = true;
    muse_board->display_unlock();

    ESP_LOGI(TAG, "UI up: %dx%d, %d px Muse, %d ms frames", s_w, s_h, s_canvas_px, muse_board->frame_ms);
    return ESP_OK;
}

void muse_ui_show_face(void)
{
    if (!s_tv) {
        return;
    }
    lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    lv_tileview_set_tile(s_tv, s_face, muse_board->epaper ? LV_ANIM_OFF : LV_ANIM_ON);
}

void muse_ui_set_swipe_enabled(bool enabled)
{
    if (!s_tv) {
        return;
    }
    lv_obj_set_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE, enabled);
    s_shown_page = -1;
    s_next_settings_tick = 0;
}

void muse_ui_preview_brightness(int pct)
{
    s_preview_brightness = pct;
    apply_brightness(pct);
}

bool muse_ui_image_size(int *w, int *h)
{
    if (!s_ready || !heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) {
        return false;
    }
    *w = s_w;
    *h = s_h;
    return true;
}

int muse_ui_image_bits(void)
{
    return muse_board->epaper ? 2 : 16;
}

/* Floyd-Steinberg to four grays, in place: each pixel becomes black, 85, 170
 * or white, so a panel that rounds every pixel to its nearest gray (the
 * Sticky's) shows shading instead of bands. err holds 2 * (w + 2) values. */
static void dither_gray4_565(uint16_t *px, int w, int h, int16_t *err)
{
    static const uint16_t grays[4] = { 0x0000, 0x52AA, 0xAD55, 0xFFFF };
    int16_t *cur = err, *next = err + w + 2;
    memset(err, 0, 2 * (size_t)(w + 2) * sizeof(*err));
    for (int y = 0; y < h; y++) {
        memset(next, 0, (size_t)(w + 2) * sizeof(*next));
        for (int x = 0; x < w; x++) {
            uint16_t p = px[(size_t)y * w + x];
            int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
            int v = ((77 * (r * 255 / 31) + 150 * (g * 255 / 63) + 29 * (b * 255 / 31)) >> 8) + cur[x + 1];
            int level = (v * 3 + 127) / 255;
            level = level < 0 ? 0 : level > 3 ? 3 : level;
            int e = v - level * 85;
            cur[x + 2] += (int16_t)(e * 7 / 16);
            next[x] += (int16_t)(e * 3 / 16);
            next[x + 1] += (int16_t)(e * 5 / 16);
            next[x + 2] += (int16_t)(e / 16);
            px[(size_t)y * w + x] = grays[level];
        }
        int16_t *t = cur;
        cur = next;
        next = t;
    }
}

void muse_ui_image_done(void)
{
    if (!s_ready || muse_ui_image_bits() != 2) {
        return;   /* a colour panel shows each rectangle as it comes */
    }
    int16_t *err = malloc(2 * (size_t)(s_w + 2) * sizeof(int16_t));
    if (!err) {
        return;   /* shown undithered: banded, but whole */
    }
    xSemaphoreTake(s_image_mutex, portMAX_DELAY);
    if (s_image_buf) {
        dither_gray4_565(s_image_buf, s_w, s_h, err);
        s_image_area = (lv_area_t){ 0, 0, s_w - 1, s_h - 1 };
        s_image_dirty = true;
    }
    xSemaphoreGive(s_image_mutex);
    free(err);
}

bool muse_ui_image_draw(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!s_ready || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > s_w || y + h > s_h) {
        return false;
    }
    xSemaphoreTake(s_image_mutex, portMAX_DELAY);
    if (!s_image_buf) {
        /* The first rectangle of an image: a black screen to draw onto. */
        s_image_buf = heap_caps_calloc((size_t)s_w * s_h, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_image_buf) {
            xSemaphoreGive(s_image_mutex);
            return false;
        }
        /* Now, not when it's shown: asleep, the display may be paused (muse_input.c). */
        muse_state_set_asleep(false);
    }
    const uint8_t *src = (const uint8_t *)pixels;
    for (int row = 0; row < h; row++) {
        uint16_t *dst = s_image_buf + (size_t)(y + row) * s_w + x;
        for (int i = 0; i < w; i++, src += 2) {
            dst[i] = (uint16_t)(src[0] << 8 | src[1]);
        }
    }
    lv_area_t *a = &s_image_area;
    if (!s_image_dirty) {
        *a = (lv_area_t){ x, y, x + w - 1, y + h - 1 };
        s_image_dirty = true;
    } else {
        a->x1 = LV_MIN(a->x1, x);
        a->y1 = LV_MIN(a->y1, y);
        a->x2 = LV_MAX(a->x2, x + w - 1);
        a->y2 = LV_MAX(a->y2, y + h - 1);
    }
    xSemaphoreGive(s_image_mutex);
    return true;
}

void muse_ui_image_hide(void)
{
    if (!s_ready) {
        return;
    }
    muse_board->display_lock(-1);
    image_hide_locked();
#if CONFIG_MUSE_WATCHER_CAMERA
    if (s_camera_hint) lv_obj_add_flag(s_camera_hint, LV_OBJ_FLAG_HIDDEN);
#endif
    muse_board->display_unlock();
}

#if CONFIG_MUSE_WATCHER_CAMERA
void muse_ui_camera_hint(bool visible)
{
    if (!s_ready || !s_camera_hint) return;
    muse_board->display_lock(-1);
    if (visible) lv_obj_remove_flag(s_camera_hint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_camera_hint, LV_OBJ_FLAG_HIDDEN);
    muse_board->display_unlock();
}
#endif
