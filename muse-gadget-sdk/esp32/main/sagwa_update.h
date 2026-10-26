/*
 * Sagwa updates herself from her GitHub repository's releases.
 *
 * Every push to main builds and publishes a release (.github/workflows/
 * release.yml) with the firmware, the clip pack and a manifest.json:
 *
 *   {"version": "1.0.12", "fw_sha256": "...", "fw_size": N,
 *    "clips_sha256": "...", "clips_size": N, "notes": "commit message"}
 *
 * A few minutes after boot and then every CONFIG_SAGWA_UPDATE_INTERVAL_MIN,
 * she fetches the latest manifest. When the firmware version is newer than the
 * running one, or the clip pack differs from the one in her partition, she
 * waits until nobody is talking with her, tells you on screen (with a chirp)
 * that she's about to update, counts down (touching her puts it off), then
 * downloads the clips straight into their partition (SHA-256 checked) and the
 * firmware through ota.c (signature checked), and restarts. On the next boot
 * she tells you, with a happy trill, that she's updated, or that it failed.
 *
 * Also starts SNTP with CONFIG_SAGWA_TZ, so her day follows the clock.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the update task (a no-op unless CONFIG_SAGWA_AUTO_UPDATE). */
void sagwa_update_start(void);

/* True once this boot has reached GitHub and read a manifest: the update path
 * works, so a freshly installed image can be kept (app.c's health check). */
bool sagwa_update_healthy(void);

#ifdef __cplusplus
}
#endif
