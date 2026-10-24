/*
 * Sagwa from real animation clips: a pack of JPEG frames in the "sagwa" flash
 * partition (sagwa/tools/pack_clips.py), played as a graph of clips that start
 * and end on shared poses; the IMU wakes her.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "muse_pixel.h"

/* Maps the pack and starts the decoder. False if the board has no pack. */
bool sagwa_clips_init(void);

void sagwa_clips_render(const muse_pose_t *pose);
void sagwa_clips_set_size(int px);
void sagwa_clips_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
int sagwa_clips_get_rig(void);
int sagwa_clips_offset_x(void);
void sagwa_clips_activity(char *out, size_t cap);
