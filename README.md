### PAW3950 driver

The static library is built for nRF52 family + Zephyr 3.5.

#### Example DeviceTree

```dts
#include <dt-bindings/zmk/paw3950-modes.h>

&spi0 {
	status = "okay";
	compatible = "nordic,nrf-spim";
	pinctrl-0 = <&spi0_default>;
	pinctrl-1 = <&spi0_sleep>;
	pinctrl-names = "default", "sleep";
	cs-gpios = <&gpioX XX (GPIO_ACTIVE_LOW)>;
	clock-frequency = <8000000>;

	trackball_primary: trackball_primary@0 {
		status = "okay";
		compatible = "pixart,paw3950";
		reg = <0>;
		spi-max-frequency = <8000000>;
		irq-gpios = <&gpioX XX (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
		cpi = <3200>;
		evt-type = <INPUT_EV_REL>;
		x-input-code = <INPUT_REL_X>;
		y-input-code = <INPUT_REL_Y>;
		power-mode = <PAW3950_HIGH_PERFORMANCE>;
		liftoff-dist = <PAW3950_LIFT_CONFIG_20MM>;
		ripple-control;
		wakeup-source;
        /delete-property/ invert-x;
        /delete-property/ invert-y;
	};
};
```

#### Runtime mode switching

Add the behavior to DeviceTree:

```dts
/ {
	behaviors {
		paw_mode: paw_mode {
			compatible = "zmk,behavior-paw3950-power-mode-toggle";
			#binding-cells = <2>;
			display-name = "Switch mode of the sensor";
			bindings = <&trackball_primary>;
		};
	}
}
```
