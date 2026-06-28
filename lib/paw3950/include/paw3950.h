/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PAW3950_LIB_H
#define PAW3950_LIB_H

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* Register count used for reading a single motion burst */
#define PAW3950_BURST_SIZE 6

/* Position of X/Y delta in motion burst data */
#define PAW3950_DX_POS 2
#define PAW3950_DY_POS 4

/* Universal lift cut-off settings (Lift_Config, bank 0x18 reg 0x7A).
 * The 0.7mm setting is the default after chip initialization. */
#define PAW3950_LIFT_CONFIG_07MM 0x00
#define PAW3950_LIFT_CONFIG_10MM 0x85
#define PAW3950_LIFT_CONFIG_20MM 0x8F

enum paw3950_mode {
    PAW3950_MODE_HIGH_PERFORMANCE = 0x00,
    PAW3950_MODE_LOW_POWER = 0x01,
    PAW3950_MODE_OFFICE = 0x02,
};

int paw3950_lib_power_up_reset(const struct spi_dt_spec *spi);
int paw3950_lib_verify_product_id(const struct spi_dt_spec *spi);
int paw3950_lib_power_up_init_regs(const struct spi_dt_spec *spi);
int paw3950_lib_clear_motion_pin_state(const struct spi_dt_spec *spi);
int paw3950_lib_motion_burst_read(const struct spi_dt_spec *spi, uint8_t *buf, size_t burst_size);
int paw3950_lib_set_cpi(const struct spi_dt_spec *spi, uint32_t cpi);
int paw3950_lib_set_axis(const struct spi_dt_spec *spi, bool swap_xy, bool inv_x, bool inv_y);
int paw3950_lib_set_performance(const struct spi_dt_spec *spi, bool enable);
int paw3950_lib_set_mode(const struct spi_dt_spec *spi, enum paw3950_mode mode);
int paw3950_lib_set_lift_config(const struct spi_dt_spec *spi, uint8_t cutoff);
int paw3950_lib_set_ripple_control(const struct spi_dt_spec *spi, bool enable);
int paw3950_lib_shutdown(const struct spi_dt_spec *spi);

// weak linked reference logger
extern void paw3950_lib_log_err(const char *fmt, ...);
extern void paw3950_lib_log_inf(const char *fmt, ...);

/******************************************************************************
 Put below custom loggers implementations in your Zephyr application or module
 ******************************************************************************

// Custom ERROR loggers for PAW3950 static library
void paw3950_lib_log_err(const char *fmt, ...) {
#if CONFIG_PAW3950_LOG_LEVEL >= 0
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    LOG_ERR("PAW3950_LIB: %s", buf);
    va_end(args);
#endif
}

// Custom INFO loggers for PAW3950 static library
void paw3950_lib_log_inf(const char *fmt, ...) {
#if CONFIG_PAW3950_LOG_LEVEL >= 3
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    LOG_INF("PAW3950_LIB: %s", buf);
    va_end(args);
#endif
}

*/

#endif // PAW3950_LIB_H
