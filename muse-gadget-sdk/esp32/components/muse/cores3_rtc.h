/*
 * The CoreS3's BM8563 real-time clock (I2C 0x51), kept running by the board's
 * battery: Sagwa knows the time after a restart even with no internet.
 *
 * At start-up the RTC's time (kept in UTC) sets the system clock, if the RTC
 * has a believable time. Each SNTP sync writes the fresh time back.
 */

#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

void cores3_rtc_init(i2c_master_bus_handle_t bus);

/* Writes the system clock (once it has been set) to the RTC. */
void cores3_rtc_save_now(void);

#ifdef __cplusplus
}
#endif
