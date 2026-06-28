/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include "drivers/behavior.h"
#include <zmk/sensors.h>
#include "paw3950.h"
#include "dt-bindings/zmk/paw3950-dt.h"

#define DT_DRV_COMPAT zmk_behavior_paw3950_power_mode_toggle

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata mtd_param1_values[] = {
    {
        .display_name = "Set",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = PAW3950_POWER_MODE_ACTION_SET,
    },
    {
        .display_name = "Toggle",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = PAW3950_POWER_MODE_ACTION_TOGGLE,
    },
};

static const struct behavior_parameter_value_metadata mtd_param2_values[] = {
    {
        .display_name = "High Power",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = 0,
    },
    {
        .display_name = "Low Power",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = 1,
    },
    {
        .display_name = "Office",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = 2,
    },
};

static const struct behavior_parameter_metadata_set profile_index_metadata_set = {
    .param1_values = mtd_param1_values,
    .param1_values_len = ARRAY_SIZE(mtd_param1_values),
    .param2_values = mtd_param2_values,
    .param2_values_len = ARRAY_SIZE(mtd_param2_values),
};

static const struct behavior_parameter_metadata_set metadata_sets[] = {profile_index_metadata_set};
static const struct behavior_parameter_metadata metadata = { .sets_len = ARRAY_SIZE(metadata_sets), .sets = metadata_sets};
#endif

struct behavior_paw3950_power_mode_toggle_config {
    const struct device *bindings;
    const struct gpio_dt_spec feedback_gpios;
    const struct gpio_dt_spec feedback_extra_gpios;
    const uint16_t feedback_duration;
};

struct behavior_paw3950_power_mode_toggle_data {
    const struct device *dev;
    struct k_work_delayable feedback_off_work;
    int previous_feedback_extra_state;
    uint8_t current_mode;
};

static void feedback_off_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    const struct behavior_paw3950_power_mode_toggle_data *data = CONTAINER_OF(dwork, struct behavior_paw3950_power_mode_toggle_data, feedback_off_work);
    const struct device *dev = data->dev;
    const struct behavior_paw3950_power_mode_toggle_config *config = dev->config;

    if (config->feedback_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_gpios, 0);
    }

    if (config->feedback_extra_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_extra_gpios, data->previous_feedback_extra_state);
    }

    LOG_DBG("Feedback turned off");
}

static int on_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_paw3950_power_mode_toggle_config *config = dev->config;
    struct behavior_paw3950_power_mode_toggle_data *data = dev->data;

    uint8_t new_mode;
    switch (binding->param1) {
    case PAW3950_POWER_MODE_ACTION_SET:
        if (binding->param2 > 2) {
            LOG_ERR("Invalid power mode %d (max 2)", binding->param2);
            return -EINVAL;
        }
        new_mode = (uint8_t)binding->param2;
        break;
    case PAW3950_POWER_MODE_ACTION_TOGGLE:
        new_mode = (data->current_mode + 1) % 3;
        break;
    default:
        return -EINVAL;
    }

    struct sensor_value val = { .val1 = new_mode };
    int ret = sensor_attr_set(config->bindings, SENSOR_CHAN_ALL, PAW3950_ATTR_POWER_MODE, &val);

    if (ret != 0) {
        LOG_ERR("Failed to set power mode");
        return ret;
    }

    data->current_mode = new_mode;

    if (config->feedback_duration > 0 && config->feedback_gpios.port != NULL) {
        if (config->feedback_extra_gpios.port != NULL) {
            data->previous_feedback_extra_state = gpio_pin_get_dt(&config->feedback_extra_gpios);
            gpio_pin_set_dt(&config->feedback_extra_gpios, 1);
        }

        if (gpio_pin_set_dt(&config->feedback_gpios, 1) == 0) {
            k_work_reschedule(&data->feedback_off_work, K_MSEC(config->feedback_duration));
        } else {
            LOG_ERR("Failed to enable the feedback");
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_paw3950_power_mode_toggle_init(const struct device *dev) {
    const struct behavior_paw3950_power_mode_toggle_config *config = dev->config;
    struct behavior_paw3950_power_mode_toggle_data *data = dev->data;

    if (config->feedback_gpios.port != NULL) {
        if (gpio_pin_configure_dt(&config->feedback_gpios, GPIO_OUTPUT) != 0) {
            LOG_WRN("Failed to configure power mode toggle feedback GPIO");
        } else {
            LOG_DBG("Power mode toggle feedback GPIO configured");
        }

        k_work_init_delayable(&data->feedback_off_work, feedback_off_work_cb);
    }

    data->dev = dev;
    return 0;
}

static const struct behavior_driver_api behavior_paw3950_power_mode_toggle_driver_api = {
    .binding_pressed = on_binding_pressed,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define BINDING_INIT(n)                                                                              \
    static struct behavior_paw3950_power_mode_toggle_data data_##n = {};                       \
    static const struct behavior_paw3950_power_mode_toggle_config config_##n = {               \
        .bindings = DEVICE_DT_GET(DT_INST_PHANDLE(n, bindings)),                                      \
        .feedback_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_gpios, { .port = NULL }),                      \
        .feedback_extra_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_extra_gpios, { .port = NULL }),      \
        .feedback_duration = DT_INST_PROP_OR(n, feedback_duration, 0),                                        \
    };                                                                                                      \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_paw3950_power_mode_toggle_init, NULL, &data_##n,   \
        &config_##n, POST_KERNEL, 99, &behavior_paw3950_power_mode_toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BINDING_INIT)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
