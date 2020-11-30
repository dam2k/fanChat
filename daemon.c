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
#include <signal.h>
#include "cputemp.h"
#include "fan.h"
#include "daemon.h"
#include "controller.h"

#ifdef __STDC_NO_ATOMICS__
#error "Atomics are not supported"
#elif ATOMIC_INT_LOCK_FREE == 0
#error "int is never lock-free"
#endif

// exit flag
atomic_int e_flag = ATOMIC_VAR_INIT(0);

static void shutdown_by_signal(int signum, siginfo_t *info, void *ptr) {
	//fprintf(stderr, "got trapped signal, going to terminate\n");
	syslog(LOG_WARNING, "Caught signal %i (%s). Terminating process.", signum, strsignal(signum));
	e_flag = 1;
}

static void catch_sigterm() {
	static struct sigaction act_term;
	
	sigemptyset(&act_term.sa_mask);
	act_term.sa_sigaction = &shutdown_by_signal;
	act_term.sa_flags = SA_SIGINFO;
	
	sigaction(SIGTERM, &act_term, NULL);
}

static void catch_sigint() {
	static struct sigaction act_int;
	
	sigemptyset(&act_int.sa_mask);
	act_int.sa_sigaction = &shutdown_by_signal;
	act_int.sa_flags = SA_SIGINFO;
	
	sigaction(SIGINT, &act_int, NULL);
}

static void daemonise(void) {
	pid_t p;
	
	// Doing a first fork()
	p=fork();
	switch(p) {
	case -1:
		fprintf(stderr, "Cannot create child process, sorry: %s\n", strerror(errno));
		_exit(1);
		break;
	case 0: // child
		break;
	default: // father
		_exit(0);
	}
	
	// Start a new session for the daemon
	if(setsid()==-1) {
		fprintf(stderr, "Cannot become session leader while daemonising: %s\n", strerror(errno));
		_exit(1);
	}
	
	// Doing another fork(), allowing the parent process to terminate
	signal(SIGHUP, SIG_IGN);
	p=fork();
	switch(p) {
	case -1:
		fprintf(stderr, "Cannot create child process, sorry: %s\n", strerror(errno));
		_exit(1);
		break;
	case 0: // child
		break;
	default: // father
		_exit(0);
	}
	
	// Set the current working directory to the root directory.
	if(chdir("/") == -1) {
		fprintf(stderr, "Failed to change working directory while daemonising: %s\n", strerror(errno));
		_exit(1);
	}
	
	// Set the user file creation mask to zero.
	umask(0);
	
	// Close then reopen standard file descriptors.
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	if(open("/dev/null",O_RDONLY) == -1) {
		fprintf(stderr, "Failed to reopen stdin while daemonising: %s\n", strerror(errno));
		_exit(1);
	}
	if(open("/dev/null",O_WRONLY) == -1) {
		fprintf(stderr, "Failed to reopen stdout while daemonising: %s\n", strerror(errno));
		_exit(1);
	}
	if(open("/dev/null",O_RDWR) == -1) {
		fprintf(stderr, "Failed to reopen stderr while daemonising: %s\n", strerror(errno));
		_exit(1);
	}
	
	int i;
	for(i=1; i<=31 ;i++) { // ignore all possibile ignorable signals
		signal(i, SIG_IGN);
	}
	
	// setup SIGTERM and SIGINT termination handlers
	catch_sigterm();
	catch_sigint();
}

int main(int argc, char *argv[]) {
	int ret;
	double T;
	
#if ATOMIC_INT_LOCK_FREE == 1
	if (!atomic_is_lock_free(&e_flag)) {
		return EXIT_FAILURE;
	}
#endif
	ret=getcputemp(&T);
	if(ret<0) {
		fprintf(stderr, "Cannot read CPU temperature. Sorry.\n");
		return 1;
	}
	printf("RPI CPU temperature is %6.3f C.\nForking to daemon...\n", T);
	
	ret=fan_setup();
	if(ret<0) {
		fprintf(stderr, "Cannot initialize fan. Sorry.\n");
		return 1;
	}
	
	daemonise();
	
	setlogmask(LOG_UPTO(LOG_NOTICE));
	openlog(DAEMON_NAME, LOG_PID | LOG_NDELAY, LOG_LOCAL1);
	syslog(LOG_NOTICE, "%s fan controller started, CPU temp is now %6.3f C.", DAEMON_NAME, T);
	
	/*
	int sl=15;
	do { // in case of a signal handler interruption, continue to sleep until it's finished
		sl=sleep(sl);
	} while(sl != 0);
	*/
	
	// main loop here until e_flag!=0 after any cancellation point...
	//if (e_flag) { /* signal trapped, we should exit */ }
	
	// the controller's main loop
	ret=controller();
	
	fan_shutdown();
	
	syslog(LOG_WARNING, "%s fan controller shut down", DAEMON_NAME);
	cputemp_close();
	closelog ();
	
	return ret;
}
