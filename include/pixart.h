#pragma once

/**
 * @file pixart.h
 *
 * @brief Common header file for all optical motion sensor by PIXART
 */

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/* device data structure */
struct pixart_data {
    const struct device          *dev;
    bool                         sw_smart_flag; // for paw3950 smart algorithm

    struct gpio_callback         irq_gpio_cb; // motion pin irq callback
    struct k_work                trigger_work; // realtrigger job

    struct k_work_delayable      init_work; // the work structure for delayable init steps
    uint8_t                      async_init_step;
    uint8_t                      init_retry_count; // current retry count
    uint8_t                      init_retry_attempts; // remaining retry attempts

    bool                         ready; // whether init is finished successfully
    bool                         error_triggered;
    int                          err; // error code during async init

    bool                         data_ready;
    uint8_t                      data_index;

    int64_t last_smp_time, last_rpt_time;
    int64_t dx, dy;

    uint8_t last_squal;
};

// device config data structure
struct pixart_config {
    uint8_t id;
	struct spi_dt_spec spi;
    struct gpio_dt_spec irq_gpio;
    uint16_t cpi;
    bool swap_xy;
    bool inv_x;
    bool inv_y;
    uint8_t evt_type;
    uint8_t x_input_code;
    uint8_t y_input_code;
    bool force_awake;
    bool ripple_control;
    uint8_t init_retry_count;
    uint16_t init_retry_interval;
    uint8_t power_mode;
    uint8_t lift_cutoff;
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
