/*
 * The CoreS3's BM8563 real-time clock (cores3_rtc.h).
 */

#include "cores3_rtc.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "cores3_rtc";

#define BM8563_ADDR 0x51
#define REG_CONTROL1 0x00
#define REG_SECONDS 0x02      /* seconds, minutes, hours, day, weekday, month, year */
#define VL_BIT 0x80           /* in seconds: the clock stopped (power lost) */
#define CENTURY_BIT 0x80      /* in month: 19xx when set */
#define SANE_EPOCH 1735689600 /* 2025-01-01: anything earlier was never set */

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int unbcd(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

/* timegm without the TZ dance: the RTC keeps UTC */
static time_t utc_mktime(struct tm *tm)
{
    static const int days_before[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int y = tm->tm_year + 1900;
    long days = (long)(y - 1970) * 365 + ((y - 1969) / 4) - ((y - 1901) / 100) + ((y - 1601) / 400);
    days += days_before[tm->tm_mon] + tm->tm_mday - 1;
    if (tm->tm_mon > 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
    return (time_t)days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

void cores3_rtc_init(i2c_master_bus_handle_t bus)
{
    if (!bus || i2c_master_probe(bus, BM8563_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "BM8563 not found");
        return;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BM8563_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) {
        s_dev = NULL;
        return;
    }
    uint8_t ctl[2] = { REG_CONTROL1, 0x00 };   /* clock running, normal mode */
    i2c_master_transmit(s_dev, ctl, sizeof(ctl), 50);

    uint8_t reg = REG_SECONDS, r[7];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 50) != ESP_OK) return;
    if (r[0] & VL_BIT) {
        ESP_LOGI(TAG, "RTC lost its time; waiting for SNTP");
        return;
    }
    struct tm tm = {
        .tm_sec = unbcd(r[0] & 0x7F),
        .tm_min = unbcd(r[1] & 0x7F),
        .tm_hour = unbcd(r[2] & 0x3F),
        .tm_mday = unbcd(r[3] & 0x3F),
        .tm_mon = unbcd(r[5] & 0x1F) - 1,
        .tm_year = unbcd(r[6]) + ((r[5] & CENTURY_BIT) ? 0 : 100),
    };
    if (tm.tm_mon < 0 || tm.tm_mon > 11) return;
    time_t t = utc_mktime(&tm);
    if (t < SANE_EPOCH) {
        ESP_LOGI(TAG, "RTC time not set yet");
        return;
    }
    if (time(NULL) < SANE_EPOCH) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "clock set from the RTC: %04d-%02d-%02d %02d:%02d UTC", tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min);
    }
}

void cores3_rtc_save_now(void)
{
    time_t now = time(NULL);
    if (!s_dev || now < SANE_EPOCH) return;
    struct tm tm;
    gmtime_r(&now, &tm);
    uint8_t w[8] = {
        REG_SECONDS,
        bcd(tm.tm_sec),
        bcd(tm.tm_min),
        bcd(tm.tm_hour),
        bcd(tm.tm_mday),
        (uint8_t)tm.tm_wday,
        (uint8_t)(bcd(tm.tm_mon + 1) | (tm.tm_year < 100 ? CENTURY_BIT : 0)),
        bcd(tm.tm_year % 100),
    };
    if (i2c_master_transmit(s_dev, w, sizeof(w), 50) == ESP_OK) {
        ESP_LOGI(TAG, "RTC set from SNTP");
    }
}
