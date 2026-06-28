/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/drivers/sensor.h>
#include "pixart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Helper macros used to convert sensor values. */
#define PAW3950_SVALUE_TO_CPI(svalue) ((uint32_t)(svalue).val1)

/** @brief Sensor specific attributes of PAW3950. */
enum paw3950_attribute {

	/** Sensor CPI for both X and Y axes. */
	PAW3950_ATTR_CPI,

	/** Sensor power mode. */
	PAW3950_ATTR_POWER_MODE,

	/** Sensor calibration. */
	PAW3950_ATTR_CALIBRATE,

};

#ifdef __cplusplus
}
#endif
