#pragma once

#define PAW3950_HIGH_PERFORMANCE 0x00
#define PAW3950_LOW_POWER        0x01
#define PAW3950_OFFICE           0x02

/* Universal lift cut-off settings for the `liftoff-dist` property.
 * 0.7 mm is the chip default after initialization. */
#define PAW3950_LIFT_CONFIG_07MM 0x00
#define PAW3950_LIFT_CONFIG_10MM 0x85
#define PAW3950_LIFT_CONFIG_20MM 0x8F
