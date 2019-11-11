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

#include "common.h"
#include <time.h>
#include "daemon.h"
#include "cputemp.h"
#include "fan.h"
#include "controller.h"

// Low Watermark: at this temperature the fan will be off
static double LW=56.9;
// High Watermark: at this temperature the fan will be on
static double HW=69.2;
// Last Low Watermark Time: last time we reached Low Watermark
static struct timespec LWT;
// Trigger Timeout: after this time from Last Watermark the fan will be on
const static struct timespec TTT = {.tv_sec=273, .tv_nsec=0 }; // 4min + 33 secs
// how many seconds after Last Watermark and still no temperature down
const static time_t max_seconds_after_LWT_and_no_temp_down = TTT.tv_sec * 3;

/**
 * subtract the 'struct timespec' values X and Y, storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
static int timespec_subtract(struct timespec *result, struct timespec *x, struct timespec *y) {
	// perform the carry for the later subtraction by updating y.
	if (x->tv_nsec < y->tv_nsec) {
		int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}
	
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
		y->tv_nsec += 1000000000 * nsec;
		y->tv_sec -= nsec;
	}
	
	// compute the time remaining to wait. tv_nsec is certainly positive.
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = x->tv_nsec - y->tv_nsec;
	
	// return 1 if result is negative
	return x->tv_sec < y->tv_sec;
}

/**
 * calculate fan speed (HW PWM driven) by the temperature. Return percentage chose.
 */
static int calculateFanSpeedByTemp(double T) {
	int p=0; // fan stopped
	if(T>LW) {
		p=42; // fan at 42% if temperature is above LW
	}
	if(T>58.7) {
		p=47;
	}
	if(T>61.2) {
		p=52;
	}
	if(T>63.0) {
		p=57;
	}
	if(T>65.3) {
		p=61;
	}
	if(T>67.0) {
		p=66;
	}
	if(T>70.1) {
		p=72;
	}
	if(T>73.7) {
		p=80;
	}
	if(T>76.4) {
		p=90;
	}
	if(T>78.0) {
		p=100;
	}
	
	return p;
}

/**
 * calculate usleep time depending on temperature. Higher temperatures require slower readings. Return useconds to sleep
 */
static useconds_t calculateSleepDependingOnTemp(double T) {
	useconds_t su=750000; // default is 0.75 seconds
	
	if(T>50.2) {
		su=1000000;
	}
	if(T>62.5) {
		su=1500000;
	}
	if(T>65.1) {
		su=2000000;
	}
	if(T>70.6) {
		su=3000000;
	}
	if(T>75.3) {
		su=4000000;
	}
	if(T>78.0) {
		su=5000000;
	}
	
	return su;
}

/**
 * update process title. If p==-1 retain the last given perc value
 */
static void updateProcessTitle(double T, int p) {
	char ops[8];
	static int perc=0;
	
	if(p<0) { // restore the previously given value
		p=perc;
	}
	if(p>0) {
		perc=p; // save the value
		strcpy(ops, "cooling");
		setproctitle("%6.3f C (LW: %6.3f C, HW: %6.3f C) - %s at %d%%", T, LW, HW, ops, p);
	}
	if(p==0) {
		perc=p; // save the value
		strcpy(ops, "idle");
		setproctitle("%6.3f C (LW: %6.3f C, HW: %6.3f C) - %s", T, LW, HW, ops);
	}
}

/**
 * This is the controller, or main loop
 */
int controller(void) {
	int ret;
	double T;
	useconds_t su;
	struct timespec now, et, TT, tmp; // now: now, et: time elapsed from LWT, TT: Trigger Time, tmp: temporary counter
	int tahdlsf=0; // don't logspam flag for temperature above high watermask messages
	int tbldlsf=0; // don't logspam flag for temperature below low watermask messages
	int ttrdlsf=0; // don't logspam flag for trigger timeout reached messages
	int ttndlsf=1; // don't logspam flag for trigger timeout not reaced messages
	
	clock_gettime(CLOCK_BOOTTIME, &LWT); // resetting Last Low Watermark
	syslog(LOG_NOTICE, "Low Watermark: %6.3f C, High Watermark: %6.3f C, Trigger Timeout: %lds+%ldns", LW, HW, TTT.tv_sec, TTT.tv_nsec);
	
	while(1) {
		// 1- get the current temperature
		ret=getcputemp(&T);
		if(ret<0) {
			syslog(LOG_ERR, "ERROR: Cannot read CPU temperature! Assuming temperature is not so high.");
			T=58;
		} else {
			syslog(LOG_INFO, "CPU temperature is %6.3f C.", T);
		}
		
		// 2- calculate the right fan speed in case we need to put the fan ON
		ret=calculateFanSpeedByTemp(T);
		
		// 3- is the current temperature above the HW?
		if(T>=HW) {
			if(tahdlsf==0) {
				syslog(LOG_NOTICE, "Temp %6.3f C above HW (%6.3f C), set fan speed to %d%%", T, HW, ret);
				tahdlsf=1;
				tbldlsf=0;
				ttrdlsf=1;
			}
			fan_set(ret);
			updateProcessTitle(T, ret);
		}
		
		// 4- is the current temperature under the LW?
		if(T<=LW) {
			if(tbldlsf==0) {
				syslog(LOG_NOTICE, "Temp %6.3f C below LW (%6.3f C), set fan speed to %d%%", T, LW, ret);
				tbldlsf=1;
				tahdlsf=0;
				ttrdlsf=0;
			}
			clock_gettime(CLOCK_BOOTTIME, &LWT);
			fan_set(ret);
			updateProcessTitle(T, ret);
		}
		
		// 5- is LWT happened more than TT ago?
		clock_gettime(CLOCK_BOOTTIME, &now);
		tmp=LWT;
		timespec_subtract(&et, &now, &tmp); // time elapsed from LWT
		su=calculateSleepDependingOnTemp(T);
		tmp=TTT;
		if(timespec_subtract(&TT,&tmp,&et)==1) { // we've reached the TTT
			if(ttrdlsf==0) {
				syslog(LOG_NOTICE, "Trigger Timeout reached (too much time after LWT). Temp %6.3f C, set fan speed to %d%%", T, ret);
				ttrdlsf=1;
				tahdlsf=0;
				tbldlsf=0;
			}
			if(et.tv_sec > max_seconds_after_LWT_and_no_temp_down) {
				if(ttrdlsf==1) {
					syslog(LOG_WARNING, "Too much time after LWT and temperature is not going down! Fan locked or load is high? Temp %6.3f C", T);
					syslog(LOG_WARNING, "Trying to unlock fan, just in case, giving it a strong 0-100 pulse");
					ttrdlsf=2;
					
					ret=0;
					fan_set(ret);
					updateProcessTitle(T, ret);
					usleep(830000);
				}
				ret=100;
				fan_set(ret);
				updateProcessTitle(T, ret);
				usleep(1000000);
			} else {
				fan_set(ret);
				updateProcessTitle(T, ret);
			}
		} else {
			if(ttndlsf==0) {
				syslog(LOG_INFO, "Trigger Timeout NOT again reached. Temp %6.3f C", T);
				ttndlsf=1;
			}
			// TT is the trigger time that we need to sleep before next round
			if((TT.tv_sec==0) && ((TT.tv_nsec*1000) < su)) { // we need to wake up earlier
				syslog(LOG_NOTICE, "We would wake up earlier: %ld usecs instead of %d usecs", (TT.tv_nsec*1000), su);
				su=TT.tv_nsec*1000;
				ttrdlsf=0;
				ttndlsf=0;
				tahdlsf=0;
				tbldlsf=0;
			}
			updateProcessTitle(T, -1);
		}
		syslog(LOG_INFO, "Sleeping for %u useconds", su);
		usleep(su);
		
		if(e_flag) { /* signal trapped, we should exit */
			syslog(LOG_NOTICE, "Termination signal trapped, shutdown sequence initiated");
			return 0;
		}
	}
	
	return 0;
}
