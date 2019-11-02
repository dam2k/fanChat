/**
 * Author: Dino Ciuffetti - dam2000 at gmail dot com
 * Home: https://github.com/dam2k/fanChat
 * Date: 2019-10-27
 * Whatis: Melopero's FanHat Raspberry Pi FAN controller daemon rethinked from scratch and written in C
 * Work with: Melopero FAN HAT for Raspberry Pi 4
 *   https://www.melopero.com/shop/melopero-engineering/melopero-fan-hat-for-raspberry-pi-4/
 * Requires: PiGpio library: http://abyz.me.uk/rpi/pigpio/ - apt update && apt install libpigpio-dev
 * Replaces: official fan driver written in python: https://www.melopero.com/fan-hat/
 * License: MIT License - https://opensource.org/licenses/MIT
 *
 * Notes:
 * The Melopero FAN HAT for Raspberry Pi 4 is a cool (freddo!! :-)) fan driver for Raspberry Pi 4. It works using GPIO PIN 18 that
 * has hardware driven PWM capabilities. This way they can handle fan speed changing the PIN's duty cycle.
 * We continuously read the CPU temperature thanks to the /sys/class/thermal/thermal_zone0/temp file and we take care of cooling
 * the CPU by setting up the right fan speed modulating the pin's PWM attached to the fan hat.
 * Since I really hate when the fan is always on at low speed, I thinked of using a high and low watermarks.
 * If the cpu's temperature is below the LW the fan will be shut down, and when the cpu's temperature raises above the HW the fan
 * will come into play at the right speed. Also, there is a trigger timeout that will fire if the fan is down for more then a few
 * minutes after the LW event. In this way we can cool down the RPI's temperature a little bit without reaching the HW or
 * stressing us too much. Enjoy it!!
 */

/**
 * Copyright 2019 Dino Ciuffetti - dam2000 at gmail dot com
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 * IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * Get CPU temperature storing it into T and return -1 on errors or 0 on success
 */
int getcputemp(double *T);

/**
 * close file descriptor
 */
void cputemp_close(void);
