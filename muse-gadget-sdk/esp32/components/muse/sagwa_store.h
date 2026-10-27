/*
 * Sagwa's own storage on the CoreS3's microSD card, for what she keeps to
 * work without the internet: cached weather, update downloads, and whatever
 * later offline features need.
 *
 *   /sdcard/sagwa/cache/    things she fetched and can show offline
 *   /sdcard/sagwa/update/   downloads staged during an update (cleared after)
 *
 * The card is mounted (FAT, never formatted) on the display's SPI bus at
 * start-up. Without a card every call fails softly and she keeps working
 * from RAM and NVS.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAGWA_STORE_ROOT "/sdcard/sagwa"

/* Mounts the card and makes Sagwa's folders (CoreS3; a no-op elsewhere). */
void sagwa_store_init(void);

bool sagwa_store_ready(void);

/* A card is in but its filesystem isn't FAT (exFAT, NTFS...): format it FAT32 */
bool sagwa_store_card_unformatted(void);

/* SAGWA_STORE_ROOT "/" rel into out; false if there's no card. */
bool sagwa_store_path(const char *rel, char *out, size_t cap);

/* Whole-file write (through a temporary and a rename, so a crash never
 * leaves half a file) and read. read returns the bytes read, 0 if none. */
bool sagwa_store_write(const char *rel, const void *data, size_t len);
size_t sagwa_store_read(const char *rel, void *buf, size_t cap);

/* Exclusive use of the card's SPI bus, which the display shares: hold it
 * around any direct stdio on files under SAGWA_STORE_ROOT (the functions
 * here take it themselves). Recursive; briefly holds up the display. */
void sagwa_store_lock(void);
void sagwa_store_unlock(void);

/* Removes a folder under the root and everything in it. */
bool sagwa_store_clear(const char *rel);

/* Card size and free space in bytes, as of the last mount or clear; false
 * without a card. */
bool sagwa_store_space(uint64_t *total, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
