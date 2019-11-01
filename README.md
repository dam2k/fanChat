# fanChat
Melopero's FanHat Raspberry pi FAN controller daemon rethinked from scratch and written in C

The Melopero FAN HAT for Raspberry Pi 4 is a cool (freddo!! :-)) fan driver for Raspberry Pi 4. It works using GPIO PIN 18 that has
hardware driven PWM capabilities. This way they can handle fan speed changing the PIN's duty cycle.
We continuously read the CPU temperature thanks to the /sys/class/thermal/thermal_zone0/temp file and we take care of cooling
the CPU by setting up the right fan speed modulating the pin's PWM attached to the fan hat.
Since I really hate when the fan is always on at low speed, I thinked of using a high and low watermarks.
If the cpu's temperature is below the LW the fan will be shut down, and when the cpu's temperature raises above the HW the fan
will come into play at the right speed. Also, there is a trigger timeout that will fire if the fan is down for more then a few minutes
after the LW event. In this way we can cool down the RPI's temperature a little bit without reaching the HW or stressing us too much.

You also enjoy it, but don't stress me!

apt install libpigpio-dev libbsd-dev

./make.sh

./fanChat

Then check /var/log/messages for fanChat cool messages.
Also check ps xaf to see what's going on:

dino@firegate2:~/fanChat$ ps fxa | grep fanChat | grep -v grep
 2598 ?        S      0:00 fanChat: 58.913 C (LW: 57.600 C, HW: 69.300 C) - idle

