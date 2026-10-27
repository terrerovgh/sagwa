/*
 * Sagwa's own storage on the CoreS3's microSD card (sagwa_store.h).
 *
 * The card shares SPI3 with the display, whose LVGL flushes go out as DMA
 * that finishes in an interrupt. An SD transaction started while one of
 * those is in flight trips the SPI driver (spi_hal_setup_trans asserts), so
 * every card access holds the LVGL lock (no new flush can start) and waits
 * for the last flush to land first: sagwa_store_lock().
 */

#include "sagwa_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_MUSE_BOARD_M5STACK_CORES3
#include "bsp/m5stack_core_s3.h"
#include "esp_vfs_fat.h"
#include "lvgl.h"
#include "lvgl_private.h"   /* lv_display_t.flushing: is a flush's DMA still going */
#endif

static const char *TAG = "sagwa_store";

static bool s_ready;
static bool s_unformatted;   /* a card is in, but not FAT */
static uint64_t s_total, s_free;

bool sagwa_store_card_unformatted(void)
{
    return s_unformatted;
}

void sagwa_store_lock(void)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    bsp_display_lock(0);   /* recursive: fine from the LVGL task itself */
    lv_display_t *d = lv_display_get_default();
    for (int i = 0; d && d->flushing && i < 200; i++) {
        vTaskDelay(1);   /* the flush's DMA ends in an interrupt */
    }
#endif
}

void sagwa_store_unlock(void)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    bsp_display_unlock();
#endif
}

static void refresh_space(void)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    uint64_t total = 0, free_bytes = 0;
    sagwa_store_lock();
    esp_err_t err = esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free_bytes);
    sagwa_store_unlock();
    if (err == ESP_OK) {
        s_total = total;
        s_free = free_bytes;
    }
#endif
}

void sagwa_store_init(void)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    sagwa_store_lock();
    esp_err_t err = bsp_sdcard_mount();
    if (err == ESP_OK) {
        mkdir(SAGWA_STORE_ROOT, 0775);
        mkdir(SAGWA_STORE_ROOT "/cache", 0775);
        mkdir(SAGWA_STORE_ROOT "/update", 0775);
    }
    sagwa_store_unlock();
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            /* The card answered; its filesystem didn't mount */
            ESP_LOGW(TAG, "microSD found but it has no FAT filesystem (exFAT and NTFS aren't supported): "
                          "format it FAT32 to use it");
            s_unformatted = true;
        } else {
            ESP_LOGI(TAG, "no microSD card (%s): offline data stays in memory", esp_err_to_name(err));
        }
        return;
    }
    s_ready = true;
    refresh_space();
    ESP_LOGI(TAG, "microSD mounted: %llu MB free of %llu MB", (unsigned long long)(s_free >> 20),
             (unsigned long long)(s_total >> 20));
#endif
}

bool sagwa_store_ready(void)
{
    return s_ready;
}

bool sagwa_store_path(const char *rel, char *out, size_t cap)
{
    if (!s_ready || !rel) return false;
    int n = snprintf(out, cap, "%s/%s", SAGWA_STORE_ROOT, rel);
    return n > 0 && (size_t)n < cap;
}

bool sagwa_store_write(const char *rel, const void *data, size_t len)
{
    char path[128], tmp[136];
    if (!sagwa_store_path(rel, path, sizeof(path))) return false;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    sagwa_store_lock();
    FILE *f = fopen(tmp, "wb");
    bool ok = f != NULL;
    if (f) {
        ok = fwrite(data, 1, len, f) == len;
        ok = fclose(f) == 0 && ok;
    }
    if (ok) {
        unlink(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) unlink(tmp);
    sagwa_store_unlock();
    if (!ok) ESP_LOGW(TAG, "couldn't write %s", path);
    return ok;
}

size_t sagwa_store_read(const char *rel, void *buf, size_t cap)
{
    char path[128];
    if (!sagwa_store_path(rel, path, sizeof(path))) return 0;
    sagwa_store_lock();
    FILE *f = fopen(path, "rb");
    size_t n = 0;
    if (f) {
        n = fread(buf, 1, cap, f);
        fclose(f);
    }
    sagwa_store_unlock();
    return n;
}

static bool remove_tree(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return unlink(path) == 0;
    bool ok = true;
    struct dirent *e;
    char child[256];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if ((size_t)snprintf(child, sizeof(child), "%.127s/%.120s", path, e->d_name) >= sizeof(child)) continue;
        ok = remove_tree(child) && ok;
    }
    closedir(d);
    return rmdir(path) == 0 && ok;
}

bool sagwa_store_clear(const char *rel)
{
    char path[128];
    if (!sagwa_store_path(rel, path, sizeof(path))) return false;
    sagwa_store_lock();
    bool ok = remove_tree(path);
    mkdir(path, 0775);   /* the folder itself stays, empty */
    sagwa_store_unlock();
    refresh_space();
    ESP_LOGI(TAG, "cleared %s", path);
    return ok;
}

bool sagwa_store_space(uint64_t *total, uint64_t *free_bytes)
{
    /* As of the last mount or clear: reading the FAT holds the display's bus,
     * so the settings never do it themselves */
    if (!s_ready) return false;
    *total = s_total;
    *free_bytes = s_free;
    return true;
}
