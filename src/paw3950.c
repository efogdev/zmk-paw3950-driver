/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pixart_paw3950

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/input/input.h>
#include <zephyr/pm/device.h>
#include <zmk/keymap.h>
#include <zmk/events/activity_state_changed.h>
#include "paw3950.h"

#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
#include <zmk_adaptive_feedback/adaptive_feedback.h>
#endif

#if IS_ENABLED(CONFIG_SHELL)
#include <zephyr/shell/shell.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(paw3950, CONFIG_PAW3950_LOG_LEVEL);

//////// PAW3950 static library header
#include "paw3950/include/paw3950.h"

// The library's public PAW3950_BURST_SIZE (6) omits SQUAL. Read one extra byte
// so the surface-quality value at index 6 is available for diagnostics.
#define PAW3950_SQUAL_POS 6
#define PAW3950_MOTION_BURST_LEN (PAW3950_SQUAL_POS + 1)

// Custom ERROR loggers for PAW3950 static library
void paw3950_lib_log_err(const char *fmt, ...) {
#if CONFIG_PAW3950_LOG_LEVEL >= LOG_LEVEL_ERROR
    va_list args;
    va_start(args, fmt);
    char buf[96];
    vsnprintf(buf, sizeof(buf), fmt, args);
    LOG_ERR("PAW3950_LIB: %s", buf);
    va_end(args);
#endif
}

// Custom INFO loggers for PAW3950 static library
void paw3950_lib_log_inf(const char *fmt, ...) {
#if CONFIG_PAW3950_LOG_LEVEL >= LOG_LEVEL_INFO
    va_list args;
    va_start(args, fmt);
    char buf[96];
    vsnprintf(buf, sizeof(buf), fmt, args);
    LOG_INF("PAW3950_LIB: %s", buf);
    va_end(args);
#endif
}


//////// Sensor initialization steps definition //////////
// init is done in non-blocking manner (i.e., async), a //
// delayable work is defined for this purpose           //
enum async_init_step {
    ASYNC_INIT_STEP_POWER_UP,         // power up reset
    ASYNC_INIT_STEP_FW_LOAD_START,    // load power-up initialzation settings, clear motion registers,
    ASYNC_INIT_STEP_CONFIGURE,        // set cpi and donwshift time (run, rest1, rest2)
    ASYNC_INIT_STEP_COUNT             // end flag
};

/* Timings (in ms) needed in between steps to allow each step finishes succussfully. */
// - Since MCU is not involved in the sensor init process, i is allowed to do other tasks.
//   Thus, k_sleep or delayed schedule can be used.
static const int32_t async_init_delay[ASYNC_INIT_STEP_COUNT] = {
    [ASYNC_INIT_STEP_POWER_UP] = 50 + CONFIG_PAW3950_INIT_POWER_UP_EXTRA_DELAY_MS,
    [ASYNC_INIT_STEP_FW_LOAD_START] = 10,
    [ASYNC_INIT_STEP_CONFIGURE] = 4,
};

static int paw3950_async_init_power_up(const struct device *dev);
static int paw3950_async_init_fw_load(const struct device *dev);
static int paw3950_async_init_configure(const struct device *dev);

#if IS_ENABLED(CONFIG_SHELL)
static void paw3950_squal_accum_sample(uint8_t id, uint8_t raw);
#endif

static int (*const async_init_fn[ASYNC_INIT_STEP_COUNT])(const struct device *dev) = {
    [ASYNC_INIT_STEP_POWER_UP] = paw3950_async_init_power_up,
    [ASYNC_INIT_STEP_FW_LOAD_START] = paw3950_async_init_fw_load,
    [ASYNC_INIT_STEP_CONFIGURE] = paw3950_async_init_configure,
};

//////// Function definitions //////////

static int paw3950_async_init_fw_load(const struct device *dev) {
    int err = 0;
    const struct pixart_config *config = dev->config;

    // verify product id before upload power-up initialization settings
    err = paw3950_lib_verify_product_id(&config->spi);
    if (err) {
        LOG_ERR("Cannot exec paw3950_lib_verify_product_id");
        return -EIO;
    }
    LOG_INF("product id verified");

    err = paw3950_lib_power_up_init_regs(&config->spi);
    if (err) {
        LOG_ERR("Cannot exec paw3950_lib_power_up_init_regs");
        return err;
    }
    LOG_INF("power up init regs done");
    
    err = paw3950_lib_clear_motion_pin_state(&config->spi);
    if (err) {
        LOG_ERR("Cannot exec paw3950_lib_clear_motion_pin_state");
        return err;
    }
    LOG_INF("clear motion pin state");

    return err;
}

static int paw3950_set_performance(const struct device *dev, const bool enabled) {
    const struct pixart_config *config = dev->config;
    int err = 0;

    if (config->force_awake) {
        err = paw3950_lib_set_performance(&config->spi, enabled);
        if (err) {
            LOG_ERR("Cannot exec paw3950_lib_set_performance");
            return err;
        }
        LOG_INF("%s performance mode", enabled ? "enable" : "disable");
    }

    return err;
}

static int paw3950_set_interrupt(const struct device *dev, const bool en) {
    const struct pixart_config *config = dev->config;
    const int ret = gpio_pin_interrupt_configure_dt(&config->irq_gpio, en ? GPIO_INT_LEVEL_ACTIVE : GPIO_INT_DISABLE);
    if (ret < 0) {
        LOG_ERR("can't set interrupt");
    }
    return ret;
}

static int paw3950_async_init_power_up(const struct device *dev) {
    int err = 0;
    const struct pixart_config *config = dev->config;

    err = paw3950_lib_power_up_reset(&config->spi);
    if (err) {
        LOG_ERR("Cannot exec paw3950_lib_power_up_reset");
        return -EIO;
    }
    LOG_INF("power up reset done");

    k_usleep(10000);

    err = paw3950_lib_power_up_reset(&config->spi);
    if (err) {
        LOG_ERR("Cannot exec paw3950_lib_power_up_reset");
        return -EIO;
    }
    LOG_INF("second power up reset done");

    return err;
}

static int paw3950_async_init_configure(const struct device *dev) {
    int err = 0;
    const struct pixart_config *config = dev->config;

    err = paw3950_set_performance(dev, true);
    if (err < 0) {
        LOG_ERR("can't set performance");
        return err;
    }

    err = paw3950_lib_set_cpi(&config->spi, config->cpi);
    if (err < 0) {
        LOG_ERR("can't set cpi");
        return err;
    }
    LOG_INF("set cpi done");

    bool swap_xy = config->swap_xy;
    bool inv_x = config->inv_x;
    bool inv_y = config->inv_y;
#if IS_ENABLED(CONFIG_PAW3950_SWAP_XY)
    swap_xy = true;
#endif
#if IS_ENABLED(CONFIG_PAW3950_INVERT_X)
    inv_x = true;
#endif
#if IS_ENABLED(CONFIG_PAW3950_INVERT_Y)
    inv_y = true;
#endif
    err = paw3950_lib_set_axis(&config->spi, swap_xy, inv_x, inv_y);
    if (err < 0) {
        LOG_ERR("can't set asix");
        return err;
    }
    LOG_INF("set asix done");

    err = paw3950_lib_set_mode(&config->spi, (enum paw3950_mode)config->power_mode);
    if (err < 0) {
        LOG_ERR("can't set power mode");
        return err;
    }

    err = paw3950_lib_set_lift_config(&config->spi, config->lift_cutoff);
    if (err < 0) {
        LOG_ERR("can't set lift config");
        return err;
    }
    LOG_INF("set lift config done");

    err = paw3950_set_performance(dev, true);
    if (err < 0) {
        LOG_ERR("can't set performance");
        return err;
    }

    if (config->ripple_control) {
        err = paw3950_lib_set_ripple_control(&config->spi, true);
        if (err < 0) {
            LOG_ERR("can't set ripple control");
            return err;
        }
        LOG_INF("ripple control enabled");
    }

    return err;
}

static void paw3950_async_init(struct k_work *work) {
    struct k_work_delayable *work_delayable = (struct k_work_delayable *)work;
    struct pixart_data *data = CONTAINER_OF(work_delayable, struct pixart_data, init_work);
    const struct device *dev = data->dev;
    const struct pixart_config *config = dev->config;

    LOG_INF("PAW3950 async init step %d", data->async_init_step);

    data->err = async_init_fn[data->async_init_step](dev);
    if (data->err) {
        LOG_ERR("PAW3950 initialization failed in step %d", data->async_init_step);
        if (data->init_retry_attempts > 0) {
            data->init_retry_attempts--;
            data->init_retry_count++;
            LOG_WRN("PAW3950#%d retrying initialization (attempt %d/%d)",
                    config->id, data->init_retry_count, config->init_retry_count);

#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
            // ReSharper disable once CppRedundantBooleanExpressionArgument
            if (data->init_retry_count >= CONFIG_PAW3950_INIT_FAILURE_THRESHOLD && CONFIG_PAW3950_INIT_FAILURE_THRESHOLD > 0 && !data->error_triggered) {
                zaf_error_trigger(config->id);
                data->error_triggered = true;
            }
#endif

            data->async_init_step = ASYNC_INIT_STEP_POWER_UP;
            k_work_schedule(&data->init_work, K_MSEC(config->init_retry_interval));
        } else {
#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
            if (!data->error_triggered) {
                zaf_error_trigger(config->id);
                data->error_triggered = true;
            }
#endif

            LOG_ERR("PAW3950#%d initialization failed after %d attempts", config->id, config->init_retry_count);
        }
    } else {
        data->async_init_step++;

        if (data->async_init_step == ASYNC_INIT_STEP_COUNT) {
            data->ready = true; // sensor is ready to work

#if IS_ENABLED(CONFIG_ZMK_ADAPTIVE_FEEDBACK)
            zaf_error_clear(config->id);
#endif

            LOG_INF("PAW3950 initialized");
            if (data->init_retry_count > 0) {
                LOG_INF("PAW3950 initialization succeeded after %d retries", data->init_retry_count);
            }
            paw3950_set_interrupt(dev, true);
        } else {
            k_work_schedule(&data->init_work, K_MSEC(async_init_delay[data->async_init_step]));
        }
    }
}

static int paw3950_collect_data(const struct device *dev) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;
    uint8_t buf[PAW3950_MOTION_BURST_LEN];

    if (unlikely(!data->ready)) {
        LOG_WRN("Device is not initialized yet");
        return -EBUSY;
    }

    if (!data->data_ready) {
        if (++data->data_index >= CONFIG_PAW3950_IGNORE_FIRST_N) {
            data->data_ready = true;
        }
        return 0;
    }

    const int err = paw3950_lib_motion_burst_read(&config->spi, buf, PAW3950_MOTION_BURST_LEN);
    if (err) {
        return err;
    }

    const int16_t x = ((int16_t)sys_get_le16(&buf[PAW3950_DX_POS]));
    const int16_t y = ((int16_t)sys_get_le16(&buf[PAW3950_DY_POS]));

    data->last_squal = buf[PAW3950_SQUAL_POS];
#if IS_ENABLED(CONFIG_SHELL)
    paw3950_squal_accum_sample(config->id, data->last_squal);
#endif

    if (x || y) {
        LOG_DBG("x/y: %d/%d", x, y);
    }

    data->dx += x;
    data->dy += y;
    return 0;
}

static void paw3950_emit_pending(const struct device *dev) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;

    const int16_t rx = (int16_t)CLAMP(data->dx, INT16_MIN, INT16_MAX);
    const int16_t ry = (int16_t)CLAMP(data->dy, INT16_MIN, INT16_MAX);
    const bool have_x = rx != 0;
    const bool have_y = ry != 0;

    if (!have_x && !have_y) {
        return;
    }

#if CONFIG_PAW3950_REPORT_INTERVAL_MIN > 0
    const int64_t now = k_uptime_get();
    if (now - data->last_rpt_time < CONFIG_PAW3950_REPORT_INTERVAL_MIN) {
        return;
    }
    data->last_rpt_time = now;
#endif

    data->dx = 0;
    data->dy = 0;
    if (have_x) {
        input_report(dev, config->evt_type, config->x_input_code, rx, !have_y, K_NO_WAIT);
    }
    if (have_y) {
        input_report(dev, config->evt_type, config->y_input_code, ry, true, K_NO_WAIT);
    }
}

static int paw3950_report_data(const struct device *dev) {
    const int err = paw3950_collect_data(dev);
    if (err) {
        return err;
    }
    paw3950_emit_pending(dev);
    return 0;
}

static void paw3950_gpio_callback(const struct device *gpiob, struct gpio_callback *cb,
                                  uint32_t pins) {
    struct pixart_data *data = CONTAINER_OF(cb, struct pixart_data, irq_gpio_cb);
    const struct device *dev = data->dev;
    paw3950_set_interrupt(dev, false);
    k_work_submit(&data->trigger_work);
}

static void paw3950_work_callback(struct k_work *work) {
    const struct pixart_data *data = CONTAINER_OF(work, struct pixart_data, trigger_work);
    const struct device *dev = data->dev;

    paw3950_report_data(dev);

    paw3950_set_interrupt(dev, true);
}

static int paw3950_init_irq(const struct device *dev) {
    int err = 0;
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;

    // check readiness of irq gpio pin
    if (!device_is_ready(config->irq_gpio.port)) {
        LOG_ERR("IRQ GPIO device not ready");
        return -ENODEV;
    }

    // init the irq pin
    err = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
    if (err) {
        LOG_ERR("Cannot configure IRQ GPIO");
        return err;
    }

    // setup and add the irq callback associated
    gpio_init_callback(&data->irq_gpio_cb, paw3950_gpio_callback, BIT(config->irq_gpio.pin));

    err = gpio_add_callback(config->irq_gpio.port, &data->irq_gpio_cb);
    if (err) {
        LOG_ERR("Cannot add IRQ GPIO callback");
    }

    return err;
}

static int paw3950_init(const struct device *dev) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;
    int err = 0;

    if (!spi_is_ready_dt(&config->spi)) {
		LOG_ERR("%s is not ready", config->spi.bus->name);
		return -ENODEV;
	}

    // check readiness of cs gpio pin and init it to inactive
    const struct gpio_dt_spec cs_gpio = config->spi.config.cs.gpio;
    if (!device_is_ready(cs_gpio.port)) {
        LOG_ERR("SPI CS device not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Cannot configure SPI CS GPIO");
        return err;
    }

    // init device pointer
    data->dev = dev;

    data->init_retry_count = 0;
    data->init_retry_attempts = config->init_retry_count;

    // init trigger handler work
    k_work_init(&data->trigger_work, paw3950_work_callback);

    // init irq routine
    err = paw3950_init_irq(dev);
    if (err) {
        return err;
    }

    // Setup delayable and non-blocking init jobs, including following steps:
    // 1. power reset
    // 2. clear motion registers
    // 3. srom firmware download and checking
    // 4. eable rest mode
    // 5. set cpi and downshift time and sample rate
    // The sensor is ready to work (i.e., data->ready=true after the above steps are finished)
    k_work_init_delayable(&data->init_work, paw3950_async_init);

	data->last_rpt_time = 0;
	data->last_smp_time = 0;
	data->dx = data->dy = 0;

    k_work_schedule(&data->init_work, K_MSEC(async_init_delay[data->async_init_step]));

#if IS_ENABLED(CONFIG_PM_DEVICE)
    if (pm_device_wakeup_is_capable(dev)) {
        pm_device_wakeup_enable(dev, true);
    }
#endif

    return err;
}

static int paw3950_attr_set(const struct device *dev, const enum sensor_channel chan,
                            const enum sensor_attribute attr, const struct sensor_value *val) {
    int err = 0;
    const struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;

    if (unlikely(chan != SENSOR_CHAN_ALL)) {
        return -ENOTSUP;
    }

    if (unlikely(!data->ready)) {
        LOG_DBG("Device is not initialized yet");
        return -EBUSY;
    }

    switch ((uint32_t)attr) {
    case PAW3950_ATTR_CPI:
        err = paw3950_lib_set_cpi(&config->spi, PAW3950_SVALUE_TO_CPI(*val));
        break;
    case PAW3950_ATTR_POWER_MODE:
        err = paw3950_lib_set_mode(&config->spi, (enum paw3950_mode)val->val1);
        break;
    case PAW3950_ATTR_CALIBRATE:
        // doesn't work so commented for now
        // err = paw3950_lib_calibrate(&config->spi, CONFIG_PAW3950_CALIBRATION_TIMEOUT_MS);
        break;

    default:
        LOG_ERR("Unknown attribute");
        err = -ENOTSUP;
    }

    return err;
}

static const struct sensor_driver_api paw3950_driver_api = {
    .attr_set = paw3950_attr_set,
};

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int paw3950_pm_action(const struct device *dev, const enum pm_device_action action) {
    struct pixart_data *data = dev->data;
    const struct pixart_config *config = dev->config;

    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND: {
        if (pm_device_wakeup_is_enabled(dev)) {
            return 0;
        }
        const int ret = paw3950_set_interrupt(dev, false);
        if (ret < 0) {
            return ret;
        }
        data->ready = false;
        return paw3950_lib_shutdown(&config->spi);
    }
    case PM_DEVICE_ACTION_RESUME:
        data->async_init_step = ASYNC_INIT_STEP_POWER_UP;
        data->init_retry_count = 0;
        data->init_retry_attempts = config->init_retry_count;
        return k_work_schedule(&data->init_work,
                               K_MSEC(async_init_delay[ASYNC_INIT_STEP_POWER_UP])) < 0 ? -EIO : 0;
    default:
        return -ENOTSUP;
    }
}
#endif // IS_ENABLED(CONFIG_PM_DEVICE)

#define PAW3950_SPI_MODE (SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_MSB |     \
                          SPI_OP_MODE_MASTER | SPI_HOLD_ON_CS | SPI_LOCK_ON)

#define PAW3950_DEFINE(n)                                                                          \
    static struct pixart_data data##n;                                                             \
    static const struct pixart_config config##n = {                                                \
		.id = n,                                                                                   \
        .spi = SPI_DT_SPEC_INST_GET(n, PAW3950_SPI_MODE, 0),                                       \
        .irq_gpio = GPIO_DT_SPEC_INST_GET(n, irq_gpios),                                           \
        .cpi = DT_PROP(DT_DRV_INST(n), cpi),                                                       \
        .swap_xy = DT_PROP(DT_DRV_INST(n), swap_xy),                                               \
        .inv_x = DT_PROP(DT_DRV_INST(n), invert_x),                                                \
        .inv_y = DT_PROP(DT_DRV_INST(n), invert_y),                                                \
        .evt_type = DT_PROP(DT_DRV_INST(n), evt_type),                                             \
        .x_input_code = DT_PROP(DT_DRV_INST(n), x_input_code),                                     \
        .y_input_code = DT_PROP(DT_DRV_INST(n), y_input_code),                                     \
        .force_awake = DT_PROP(DT_DRV_INST(n), force_awake),                                       \
        .ripple_control = DT_PROP(DT_DRV_INST(n), ripple_control),                                 \
        .init_retry_count = DT_PROP(DT_DRV_INST(n), init_retry_count),                             \
        .init_retry_interval = DT_PROP(DT_DRV_INST(n), init_retry_interval),                       \
        .power_mode = DT_PROP(DT_DRV_INST(n), power_mode),                                         \
        .lift_cutoff = DT_PROP(DT_DRV_INST(n), liftoff_dist),                                      \
    };                                                                                             \
    PM_DEVICE_DT_INST_DEFINE(n, paw3950_pm_action);                                                \
    DEVICE_DT_INST_DEFINE(n, paw3950_init, PM_DEVICE_DT_INST_GET(n), &data##n, &config##n,         \
                          POST_KERNEL, CONFIG_INPUT_PAW3950_INIT_PRIORITY, &paw3950_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PAW3950_DEFINE)


#define GET_PAW3950_DEV(node_id) DEVICE_DT_GET(node_id),

static const struct device *paw3950_devs[] = {
    DT_FOREACH_STATUS_OKAY(pixart_paw3950, GET_PAW3950_DEV)
};

static int on_activity_state(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *state_ev = as_zmk_activity_state_changed(eh);

    if (!state_ev) {
        LOG_WRN("NO EVENT, leaving early");
        return 0;
    }

    for (size_t i = 0; i < ARRAY_SIZE(paw3950_devs); i++) {
        paw3950_set_performance(paw3950_devs[i], state_ev->state == ZMK_ACTIVITY_ACTIVE ? 1 : 0);
    }

    return 0;
}

ZMK_LISTENER(zmk_paw3950_idle_sleeper, on_activity_state);
ZMK_SUBSCRIPTION(zmk_paw3950_idle_sleeper, zmk_activity_state_changed);

#if IS_ENABLED(CONFIG_SHELL)
#define PAW3950_SQUAL_MAX_FEATURES (0xB6u << 2)

struct paw3950_squal_accum {
    bool active;
    uint8_t min, max;
    uint32_t sum;
    uint32_t count;
};
static struct paw3950_squal_accum squal_accum[ARRAY_SIZE(paw3950_devs)];
static struct k_spinlock squal_accum_lock;

static void paw3950_squal_accum_sample(const uint8_t id, const uint8_t raw) {
    if (id >= ARRAY_SIZE(squal_accum)) {
        return;
    }
    struct paw3950_squal_accum *a = &squal_accum[id];
    if (!a->active) {
        return;
    }
    const k_spinlock_key_t key = k_spin_lock(&squal_accum_lock);
    if (a->active) {
        if (a->count == 0 || raw < a->min) {
            a->min = raw;
        }
        if (a->count == 0 || raw > a->max) {
            a->max = raw;
        }
        a->sum += raw;
        a->count++;
    }
    k_spin_unlock(&squal_accum_lock, key);
}

static int cmd_sensor_surface(const struct shell *sh, const size_t argc, char **argv) {
    if (argc == 1) {
        for (size_t i = 0; i < ARRAY_SIZE(paw3950_devs); i++) {
            const struct device *dev = paw3950_devs[i];
            const struct pixart_config *config = dev->config;
            const struct pixart_data *data = dev->data;
            if (!data->ready) {
                shell_print(sh, "Sensor #%u: not ready", config->id);
                continue;
            }
            const uint16_t corrected = ((uint16_t) data->last_squal) << 2;
            shell_print(sh, "Sensor #%u: surface quality = %u/%u (last reported)",
                        config->id, corrected, PAW3950_SQUAL_MAX_FEATURES);
        }
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "--accum-ms") == 0) {
        char *end;
        const unsigned long ms = strtoul(argv[2], &end, 10);
        if (*end != '\0' || ms == 0 || ms > 60000) {
            shell_error(sh, "Invalid duration: %s (1..60000 ms)", argv[2]);
            return -EINVAL;
        }

        k_spinlock_key_t key = k_spin_lock(&squal_accum_lock);
        for (size_t i = 0; i < ARRAY_SIZE(squal_accum); i++) {
            squal_accum[i] = (struct paw3950_squal_accum){ .active = true };
        }
        k_spin_unlock(&squal_accum_lock, key);
        k_msleep((int32_t) ms);

        struct paw3950_squal_accum snapshot[ARRAY_SIZE(squal_accum)];
        key = k_spin_lock(&squal_accum_lock);
        for (size_t i = 0; i < ARRAY_SIZE(squal_accum); i++) {
            squal_accum[i].active = false;
            snapshot[i] = squal_accum[i];
        }
        k_spin_unlock(&squal_accum_lock, key);

        for (size_t i = 0; i < ARRAY_SIZE(paw3950_devs); i++) {
            const struct pixart_config *config = paw3950_devs[i]->config;
            const struct paw3950_squal_accum *a = &snapshot[i];
            if (a->count == 0) {
                shell_print(sh, "Sensor #%u: no samples (move pointer during the test)", config->id);
                continue;
            }
            const uint16_t min_f = ((uint16_t) a->min) << 2;
            const uint16_t max_f = ((uint16_t) a->max) << 2;
            const uint32_t avg_f = (a->sum << 2) / a->count;
            shell_print(sh, "Sensor #%u: %u/%u/%u of %u (min/max/avg) over %u samples",
                        config->id, min_f, max_f, avg_f,
                        PAW3950_SQUAL_MAX_FEATURES, a->count);
        }
        return 0;
    }

    shell_error(sh, "usage: sensor surface [--accum-ms N]");
    return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sensor,
    SHELL_CMD_ARG(surface, NULL, "Read surface quality. Usage: surface [--accum-ms N]", cmd_sensor_surface, 1, 2),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(sensor, &sub_sensor, "Sensor diagnostics", NULL);

#endif
