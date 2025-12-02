# fanChat
Raspberry pi Active cooler FAN controller daemon rethinked from scratch and written in C

Is a cool (freddo!! :-)) fan driver for Raspberry Pi. It works using GPIO PIN 18 that has
hardware driven PWM capabilities, this way we can handle fan speed changing the PIN's duty cycle.

We continuously read the CPU temperature thanks to the /sys/class/thermal/thermal_zone0/temp file and we take care of cooling
the CPU and board by setting up the right fan speed modulating the pin's PWM attached to the fan hat.

**Since I really hate when the fan is always on at low speed** I thinked of using high and low watermark.
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

There is a good chance that on newer raspberry pi os versions (debian 13) the package libpigpio-dev is no longer available.
In this case, just do this:
```
cd /usr/src
git clone https://github.com/joan2937/pigpio.git
cd pigpio
make install
```

Then continue to compile fanChat with ./make.sh.

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

Syslog is also supported, any activity is reported:
```
root@firegate3:~# grep fanChat /var/log/syslog
2024-05-28T01:07:18.508305+02:00 firegate3 fanChat[769]: Caught signal 10 (User defined signal 1).
2024-05-28T01:07:18.510432+02:00 firegate3 fanChat[769]: Signal trapped, fan at maximum speed for a while (30) seconds
2024-05-30T00:07:41.937697+02:00 firegate3 fanChat[769]: Trigger Timeout reached (too much time after LWT). Temp 61.3 C, set fan speed to 42%
2024-05-30T00:08:16.945385+02:00 firegate3 fanChat[769]: Temp 59.4 C below LW (59.6 C), set fan speed to 0%
2024-05-30T00:26:29.639405+02:00 firegate3 fanChat[769]: Trigger Timeout reached (too much time after LWT). Temp 60.9 C, set fan speed to 42%
2024-05-30T00:26:56.144483+02:00 firegate3 fanChat[769]: Temp 59.4 C below LW (59.6 C), set fan speed to 0%
2024-05-30T00:38:01.761311+02:00 firegate3 fanChat[769]: Trigger Timeout reached (too much time after LWT). Temp 62.3 C, set fan speed to 46%
2024-05-30T00:38:20.765959+02:00 firegate3 fanChat[769]: Temp 59.4 C below LW (59.6 C), set fan speed to 0%
2024-05-30T00:52:02.411938+02:00 firegate3 fanChat[769]: Trigger Timeout reached (too much time after LWT). Temp 61.8 C, set fan speed to 46%
2024-05-30T00:52:25.417095+02:00 firegate3 fanChat[769]: Temp 59.4 C below LW (59.6 C), set fan speed to 0%
2024-05-30T23:42:05.296487+02:00 firegate3 fanChat[769]: Trigger Timeout reached (too much time after LWT). Temp 60.9 C, set fan speed to 42%
2024-05-30T23:42:24.300892+02:00 firegate3 fanChat[769]: Temp 59.4 C below LW (59.6 C), set fan speed to 0%
2024-05-31T10:52:47.906609+02:00 firegate3 fanChat[769]: Caught signal 10 (User defined signal 1).
2024-05-31T10:52:47.913889+02:00 firegate3 fanChat[769]: Signal trapped, fan at maximum speed for a while (30) seconds
2024-05-31T10:53:04.167573+02:00 firegate3 fanChat[769]: Caught signal 10 (User defined signal 1).
2024-05-31T10:53:04.167927+02:00 firegate3 fanChat[769]: Signal trapped, fan at maximum speed for a while (30) seconds
```
