// Sagwa for the M5Stack CoreS3.
//
// With a clip pack in the "sagwa" flash partition (sagwa/tools/pack_clips.py),
// Sagwa is played from real animation clips with body physics
// (sagwa_clips.c). Without one, the drawn poses are warped in place as before
// (sagwa_legacy.c).

#include <stddef.h>

#include "muse_pixel.h"
#include "sagwa_clips.h"
#include "sagwa_legacy.h"

static int s_clips = -1;   /* -1 until checked, then 0 or 1 */

static bool clips(void)
{
    if (s_clips < 0) {
        s_clips = sagwa_clips_init() ? 1 : 0;
    }
    return s_clips == 1;
}

uint32_t muse_pixel_accent(muse_mode_t mode)
{
    return sagwa_legacy_accent(mode);
}

void muse_pixel_render(const muse_pose_t *pose)
{
    if (clips()) {
        sagwa_clips_render(pose);
    } else {
        sagwa_legacy_render(pose);
    }
}

void muse_pixel_set_size(int px)
{
    sagwa_clips_set_size(px);
    sagwa_legacy_set_size(px);
}

void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    if (s_clips == 1) {
        sagwa_clips_scale(dst, stride_px, x0, x1, y0, y1);
    } else {
        sagwa_legacy_scale(dst, stride_px, x0, x1, y0, y1);
    }
}

int muse_pixel_get_rig(void)
{
    return s_clips == 1 ? sagwa_clips_get_rig() : sagwa_legacy_get_rig();
}

int muse_pixel_offset_x(void)
{
    return s_clips == 1 ? sagwa_clips_offset_x() : 0;
}

void muse_pixel_activity(char *out, size_t cap)
{
    if (s_clips == 1) {
        sagwa_clips_activity(out, cap);
    } else if (cap) {
        out[0] = '\0';
    }
}
