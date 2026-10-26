/*
 * Sagwa's own storage on the CoreS3's microSD card (sagwa_store.h).
 */

#include "sagwa_store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_MUSE_BOARD_M5STACK_CORES3
#include "bsp/m5stack_core_s3.h"
#include "esp_vfs_fat.h"
#endif

static const char *TAG = "sagwa_store";

static bool s_ready;
static bool s_unformatted;   /* a card is in, but not FAT */

bool sagwa_store_card_unformatted(void)
{
    return s_unformatted;
}

void sagwa_store_init(void)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    esp_err_t err = bsp_sdcard_mount();
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
    mkdir(SAGWA_STORE_ROOT, 0775);
    mkdir(SAGWA_STORE_ROOT "/cache", 0775);
    mkdir(SAGWA_STORE_ROOT "/update", 0775);
    s_ready = true;
    uint64_t total = 0, free_bytes = 0;
    sagwa_store_space(&total, &free_bytes);
    ESP_LOGI(TAG, "microSD mounted: %llu MB free of %llu MB", (unsigned long long)(free_bytes >> 20),
             (unsigned long long)(total >> 20));
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
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        unlink(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) {
        unlink(tmp);
        ESP_LOGW(TAG, "couldn't write %s", path);
    }
    return ok;
}

size_t sagwa_store_read(const char *rel, void *buf, size_t cap)
{
    char path[128];
    if (!sagwa_store_path(rel, path, sizeof(path))) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
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
    bool ok = remove_tree(path);
    mkdir(path, 0775);   /* the folder itself stays, empty */
    ESP_LOGI(TAG, "cleared %s", path);
    return ok;
}

bool sagwa_store_space(uint64_t *total, uint64_t *free_bytes)
{
#if CONFIG_MUSE_BOARD_M5STACK_CORES3
    if (!s_ready) return false;
    return esp_vfs_fat_info(BSP_SD_MOUNT_POINT, total, free_bytes) == ESP_OK;
#else
    (void)total;
    (void)free_bytes;
    return false;
#endif
}
