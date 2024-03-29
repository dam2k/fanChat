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
static const double LW=59.6;
// High Watermark: at this temperature the fan will be on
static const double HW=69.4;
// Max fan speed will be reached when temperature rise above this one
static const double max=79.4;
// Last Low Watermark Time: last time we reached Low Watermark
static struct timespec LWT;
// Trigger Timeout: after this time from Last Watermark the fan will be on (if temperature is above low watermark)
static const struct timespec TTT = {.tv_sec=273, .tv_nsec=0 }; // 4min + 33 secs
// how many seconds after Last Watermark and still no temperature down
static const time_t max_seconds_after_LWT_and_no_temp_down = TTT.tv_sec * 2;

/*
  Fan speed steps. When the fan is ON its speed can be incremented by steps from 0 to 10, where step 0 is 42%, step 1 is 46%, and so on.
  Step 0 will be on LW, Step 10 will be on max, btw the fan will be ALWAYS ON ONLY IF the temperature exceeds HW, eventually the Trigger timeout
  will fire if temperature stands between LW and HW. Under LW the fan will be off.
*/
//                          STEPS:    0   1   2   3   4   5   6   7   8   9   10
static const int fanstepsperc[11] = {42, 46, 52, 57, 61, 66, 72, 80, 88, 94, 100}; // %

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
	int i;
	double tsbase, ts;
	
	tsbase=(max-LW)/10;
	for(i=10; i>=0; i--) { // find the fan speed (from step 10 to 0)
		ts=(LW+(tsbase*i));
		if(T>ts) { // temperature exceed step i, returning proper fan speed
			//syslog(LOG_INFO, "Temp: %2.1f C > %2.1f (step %i (0/10)), fan set at %i%%\n", T, ts, i, fanstepsperc[i]);
			return fanstepsperc[i];
		}
	}
	
	//syslog(LOG_INFO, "Temp (%2.1f C) is under %2.1f, fan not needed at the moment\n", T, LW);
	return 0;
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
	if(T>77.6) {
		su=5000000;
	}
	
	return su;
}

/**
 * update process title. If p==-1 retain the last given perc value
 */
static void updateProcessTitle(double T, int p) {
	char ops[14];
	static int perc=0;
	
	if(p<0) { // restore the previously given value
		p=perc;
	}
	if(p>0 && p<86) {
		perc=p; // save the value
		strcpy(ops, "cooling");
		setproctitle("%2.1f C (LW: %2.1f C, HW: %2.1f C) - %s at %d%%", T, LW, HW, ops, p);
	}
	if(p>85) {
		perc=p; // save the value
		strcpy(ops, "TURBO cooling");
		setproctitle("%2.1f C (LW: %2.1f C, HW: %2.1f C) - %s at %d%%", T, LW, HW, ops, p);
	}
	if(p==0) {
		perc=p; // save the value
		strcpy(ops, "idle");
		setproctitle("%2.1f C (LW: %2.1f C, HW: %2.1f C) - %s", T, LW, HW, ops);
	}
}

/**
 * This is the controller, or main loop
 */
int controller(void) {
	int ret;
	double T;
	useconds_t su;
	struct timespec now, et, TT, tmp, tusr; // now: now, et: time elapsed from LWT, TT: Trigger Time, tmp: temporary counter, tusr: sigusr1 signal driven
	int tahdlsf=0; // don't logspam flag for temperature above high watermask messages
	int tbldlsf=0; // don't logspam flag for temperature below low watermask messages
	int ttrdlsf=0; // don't logspam flag for trigger timeout reached messages
	int ttndlsf=1; // don't logspam flag for trigger timeout not reached messages
	
	clock_gettime(CLOCK_BOOTTIME, &tusr); // now
	tusr.tv_sec-=1; // now - 1 second
	
	clock_gettime(CLOCK_BOOTTIME, &LWT); // resetting Last Low Watermark
	syslog(LOG_NOTICE, "Low Watermark: %2.1f C, High Watermark: %2.1f C, Trigger Timeout: %lds+%ldns", LW, HW, TTT.tv_sec, TTT.tv_nsec);
	
	while(1) {
		clock_gettime(CLOCK_BOOTTIME, &now);
		
		// 1- get the current temperature
		ret=getcputemp(&T);
		if(ret<0) {
			syslog(LOG_ERR, "ERROR: Cannot read CPU temperature! Assuming temperature is not so high.");
			T=58;
		} /* else {
			syslog(LOG_INFO, "CPU temperature is %2.1f C.", T);
		} */
		
		if(timespec_subtract(&tmp,&tusr,&now)==0) { // still let the fan at full speed...
			//syslog(LOG_NOTICE, "Temp %2.1f C, fan at full speed for a while", T);
			updateProcessTitle(T, 100);
			su=1000000; // sleep for a second
		} else {
			// 2- calculate the right fan speed in case we need to put the fan ON
			ret=calculateFanSpeedByTemp(T);
			
			// 3- is the current temperature above the HW?
			if(T>=HW) {
				if(tahdlsf==0) {
					syslog(LOG_NOTICE, "Temp %2.1f C above HW (%2.1f C), set fan speed to %d%%", T, HW, ret);
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
					syslog(LOG_NOTICE, "Temp %2.1f C below LW (%2.1f C), set fan speed to %d%%", T, LW, ret);
					tbldlsf=1;
					tahdlsf=0;
					ttrdlsf=0;
				}
				clock_gettime(CLOCK_BOOTTIME, &LWT);
				fan_set(ret);
				updateProcessTitle(T, ret);
			}
			
			// 5- is LWT happened more than TT ago?
			tmp=LWT;
			timespec_subtract(&et, &now, &tmp); // time elapsed from LWT
			su=calculateSleepDependingOnTemp(T);
			tmp=TTT;
			if(timespec_subtract(&TT,&tmp,&et)==1) { // we've reached the TTT
				if(ttrdlsf==0) {
					syslog(LOG_NOTICE, "Trigger Timeout reached (too much time after LWT). Temp %2.1f C, set fan speed to %d%%", T, ret);
					ttrdlsf=1;
					tahdlsf=0;
					tbldlsf=0;
				}
				if(et.tv_sec > max_seconds_after_LWT_and_no_temp_down) {
					if(ttrdlsf==1) {
						syslog(LOG_WARNING, "Too much time after LWT and temperature is not going down! Fan locked or load is high? Temp %2.1f C", T);
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
					syslog(LOG_INFO, "Trigger Timeout NOT again reached. Temp %2.1f C", T);
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
		}
		if(e_flag) { // signal trapped, we should exit
			syslog(LOG_NOTICE, "Termination signal trapped, shutdown sequence initiated");
			return 0;
		}
		if(fanonforawhile) { // signal trapped. Fan at maximum speed for a while
			syslog(LOG_NOTICE, "Signal trapped, fan at maximum speed for a while (%i) seconds", FANONFORAWHILESECS);
			fanonforawhile=0;
			updateProcessTitle(T, 100);
			fan_set(100);
			clock_gettime(CLOCK_BOOTTIME, &tusr); // now
			// after this time we should run normally
			tusr.tv_sec+=FANONFORAWHILESECS;
		}
		
		//syslog(LOG_INFO, "Sleeping for %u useconds", su);
		usleep(su);
	}
	
	return 0;
}
