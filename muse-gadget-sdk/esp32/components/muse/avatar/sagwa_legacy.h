/* Sagwa's earlier renderer: the poses warped in place. Used when the board has no clip pack. */
#pragma once

#include <stdint.h>

#include "muse_pixel.h"

void sagwa_legacy_render(const muse_pose_t *pose);
void sagwa_legacy_set_size(int px);
void sagwa_legacy_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
int sagwa_legacy_get_rig(void);
uint32_t sagwa_legacy_accent(muse_mode_t mode);
