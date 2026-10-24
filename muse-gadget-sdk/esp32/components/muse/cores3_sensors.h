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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M5Stack CoreS3 onboard sensors on internal I2C0 (SDA=GPIO12, SCL=GPIO11):
 * - LTR-553ALS-WA (0x23): ambient light (lux) and infrared proximity (0..2047)
 * - BMI270 (0x69 / 0x68): 6-axis IMU (3-axis accel in g, 3-axis gyro in deg/s,
 *   tilt angle in degrees, and board-compensated temperature in Celsius).
 *
 * Feeds both the Home Link `sensors.read` command for Muse and the live
 * CoreS3 Touch Settings "Sensors" page + Sagwa proximity petting / motion wake.
 */

typedef struct {
    bool als_valid;
    float illuminance_lux;
    int proximity;          /* 0..2047 counts */

    bool imu_valid;
    float temperature_c;
    float accel_x;          /* g */
    float accel_y;          /* g */
    float accel_z;          /* g */
    float gyro_x;           /* deg/s */
    float gyro_y;           /* deg/s */
    float gyro_z;           /* deg/s */
    float tilt_deg;         /* 0..180 deg from horizontal */
} cores3_sensors_snapshot_t;

void cores3_sensors_init(i2c_master_bus_handle_t bus);
void cores3_sensors_read(cores3_sensors_snapshot_t *out);
cJSON *cores3_sensors_command(void);

#ifdef __cplusplus
}
#endif
