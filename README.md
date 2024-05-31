# fanChat
Raspberry pi Active cooler FAN controller daemon rethinked from scratch and written in C

Is a cool (freddo!! :-)) fan driver for Raspberry Pi. It works using GPIO PIN 18 that has
hardware driven PWM capabilities, this way we can handle fan speed changing the PIN's duty cycle.

We continuously read the CPU temperature thanks to the /sys/class/thermal/thermal_zone0/temp file and we take care of cooling
the CPU and board by setting up the right fan speed modulating the pin's PWM attached to the fan hat.

**Since I really hate when the fan is always on at low speed** I thinked of using a high and low watermarks.
If the cpu's temperature is below the LW the fan will be shut down, and when the cpu's temperature raises above the HW the fan
will come into play at the right speed. Also, there is a trigger timeout that will fire if the fan is down for more then a few minutes
after the LW event. In this way we can cool down the RPI's temperature a little bit without reaching the HW or stressing us too much.

**SILENCE IS GOLD!**

NOTE: it's advisable to upgrade to the last linux OS and install the latest firmware that is well known to drop rpi temperature down without touching performance.

Enjoy your cool silence!!

**Installation and usage**
```
apt install libpigpio-dev libbsd-dev
./make.sh
./fanChat
```

Then check /var/log/messages for fanChat cool messages.
Also check ps xaf to see what's going on:
```
root@firegate3:~# ps axf | grep fanCh | grep -v grep
  769 ?        S      2:14 fanChat: 51.1 C (LW: 59.6 C, HW: 69.4 C) - idle
```

Send kill -10 to run the fan at 100% for 30 seconds:
```
root@firegate3:~# kill -SIGUSR1 769
root@firegate3:~# ps axf | grep fanCh | grep -v grep
  769 ?        S      2:14 fanChat: 53.1 C (LW: 59.6 C, HW: 69.4 C) - TURBO cooling at 100%
```
