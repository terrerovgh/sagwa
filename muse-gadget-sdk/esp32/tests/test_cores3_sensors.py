# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CoreS3SensorsAndSettingsTest(unittest.TestCase):
    def test_sensors_read_advertised_and_dispatched_for_cores3(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        self.assertIn("#if CONFIG_MUSE_BOARD_M5STACK_CORES3", noise)
        start = noise.index("#if CONFIG_MUSE_BOARD_M5STACK_CORES3")
        self.assertIn('add_command(commands, "sensors.read"', noise[start:noise.index("#endif", start)])

        app = (ROOT / "main/app.c").read_text()
        start = app.index("static cJSON *on_ws_command(")
        dispatch = app[start:app.index("unsupported command", start)]
        block = dispatch[dispatch.index("#if CONFIG_MUSE_BOARD_M5STACK_CORES3"):]
        self.assertIn('"sensors.read"', block[:block.index("#endif")])
        self.assertIn("cores3_sensors_command()", block[:block.index("#endif")])

    def test_cores3_board_initializes_sensors_on_shared_i2c_bus(self):
        board = (ROOT / "components/muse/boards/board_m5stack_cores3.c").read_text()
        self.assertIn('#include "cores3_sensors.h"', board)
        self.assertIn("cores3_sensors_init(bsp_i2c_get_handle());", board)

    def test_settings_ui_has_ergonomic_compact_layout_and_sensors_page(self):
        ui = (ROOT / "components/muse/muse_settings_ui.c").read_text()
        self.assertIn("build_sensors_page", ui)
        self.assertIn("tick_sensors", ui)
        self.assertIn('"Pet by proximity"', ui)
        self.assertIn('"Wake on motion"', ui)
        self.assertIn('"Auto brightness"', ui)
        self.assertIn("is_compact_panel()", ui)
        self.assertIn("Confirm pairing", ui)


if __name__ == "__main__":
    unittest.main()
