# Copyright (c) 2023 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_PAW3950 app PRIVATE behavior_paw3950_calibrate.c)
target_sources_ifdef(CONFIG_PAW3950 app PRIVATE behavior_paw3950_power_mode_toggle.c)
