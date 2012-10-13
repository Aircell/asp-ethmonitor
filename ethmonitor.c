/*
 * Author: Matthew Ranostay <Matt_Ranostay@mentor.com>
 * Copyright (C) 2009 Mentor Graphics
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

/* 
 Retry interval;  may as well be short, the phone is usless otherwise,
 but the value should also be relatively prime with broadcast intervals 
 such as AMP
 */
#define RETRY_DHCP_SECS 11 

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ethtool-copy.h>

#include <time.h>

#include <cutils/properties.h>
#include <private/android_filesystem_config.h>

int ifc_init();
void ifc_close();
int ifc_up(char *iname);
int ifc_down(char *iname);
typedef struct dhcp_info dhcp_info;

struct dhcp_info {
    uint32_t type;

    uint32_t ipaddr;
    uint32_t gateway;
    uint32_t netmask;

    uint32_t dns1;
    uint32_t dns2;

    uint32_t serveraddr;
    uint32_t lease;
};
int do_dhcp(char *iname, dhcp_info *pinfo);
int do_dhcp_renew(char *iname, dhcp_info *pinfo);


static int thread_running = 0;
static int renew_running = 0;
static int LONG_TIME = 600;
static int retry_dhcp = 0;
static struct timespec retry_time;

typedef struct {
	char *interface;
	dhcp_info info;
} tdata_t;
	
void dhcp_function_renew(void *ptr)
{
	tdata_t *tdata = ptr;
	int retval;
	renew_running = 1;
	  fprintf(stdout, "ethmonitor: start dhcp_function_renew on %s\n", tdata->interface);
	retval = do_dhcp_renew(tdata->interface, &(tdata->info));
	renew_running = 0;
	pthread_exit(NULL);

}

void dhcp_function(void *ptr)
{
	char buf[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];
	tdata_t *tdata = ptr;
        dhcp_info info;
	int retval;
  fprintf(stdout, "ethmonitor: start dhcp_function on %s\n",
		tdata->interface);
	thread_running = 1;

#ifdef DEBUG
	printf("dhcp_function calling do_dhcp\n");
#endif

	retval = do_dhcp(tdata->interface, &(tdata->info));
#ifdef DEBUG
	printf("dhcp_function do_dhcp returned %d\n", retval);
#endif
	sleep(1); /* beagleboard needs a second */
	if (retval != 0) {
		/* DHCP did not succeed */
		if (RETRY_DHCP_SECS) {
			clock_gettime(CLOCK_MONOTONIC, &retry_time);
			retry_time.tv_sec += RETRY_DHCP_SECS;
			retry_dhcp = 1;
		} else {
			retry_dhcp = 0; /* Just don't want to retry */
		}
		thread_running = 0;
		fprintf(stdout, "ethmonitor: exit dhcp_function (failed)\n");
		return;
	} else {
		retry_dhcp = 0;
	}

	/* DNS setting #1 */
	snprintf(buf, sizeof(buf), "net.%s.dns1", tdata->interface);
	property_get(buf, value, "");
	property_set("net.dns1", value);

	/* Report status of network connection */
	snprintf(buf, sizeof(buf), "net.%s.status", tdata->interface);
	property_set(buf, !!strcmp(value, "") ? "dhcp" : "up");

	/* DNS setting #2 */
	snprintf(buf, sizeof(buf), "net.%s.dns2", tdata->interface);
	property_get(buf, value, "");
	property_set("net.dns2", value);

	thread_running = 0;
	fprintf(stdout, "ethmonitor: exit dhcp_function\n");
	pthread_exit(NULL);
}


int get_link_status(int fd, struct ifreq *ifr)
{
	struct ethtool_value edata;
	int err;

	edata.cmd = ETHTOOL_GLINK;
	ifr->ifr_data = (caddr_t)&edata;
	err = ioctl(fd, SIOCETHTOOL, ifr);

	if (err < 0) {

		return -EINVAL;
	} 

	return edata.data;
}

void monitor_connection(char *interface)
{
	struct ifreq ifr;

	char buf[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];

	int state = 0;
	int tmp_state = 0;
	int fd;
	struct timespec time_now;
	tdata_t  tdata;

	tdata.interface = interface;
	memset(&tdata.info, 0, sizeof(dhcp_info));

	pthread_t thread;
#ifdef DEBUG
   printf("ethmonitor monitor_connection 1\n");
#endif

	while (1) {
		/* setup the control structures */
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, interface);

		fd = socket(AF_INET, SOCK_DGRAM, 0);

		if (fd < 0) { /* this shouldn't ever happen */
			fprintf(stderr, "Cannot open control interface for %s 1. try again", interface);
			continue; //exit(errno);
		}

		tmp_state = get_link_status(fd, &ifr);
	    close(fd);

#ifdef DEBUG
		printf("ethmonitor.monitor_connection 4 get_link_status returned. connection state changed: %d previous state: %d\n", tmp_state, state);
#endif
		if (tmp_state != state) { /* state changed */

			if (thread_running) {
#ifdef DEBUG
				printf("Cannot open control interface for %s 2. try again\n", interface);
#endif
				//exit(0); /* pthread_kill doesn't work correctly */
				continue;
			}

			state = tmp_state;

			/* Used by JNI code to find current DHCP device */
			property_set("net.device", interface);

			snprintf(buf, sizeof(buf), "net.%s.status", interface);
			property_set(buf, state ? "up" : "down");

			if (state) { /* bring up connection */
#ifdef DEBUG
				printf("Connection up %s\n", interface);
#endif
				fprintf(stdout, "ethmonitor: monitor_connection  5 call dhcp_function\n");
				pthread_create (&thread, NULL, (void *) &dhcp_function, (void *) &tdata);

			} else { /* down connection */
#ifdef DEBUG
				printf("Connection down %s\n", interface);
#endif
			}

		} else if (state && (! thread_running) && retry_dhcp) {
			/* It may be time to retry */
			clock_gettime(CLOCK_MONOTONIC, &time_now);
			if (time_now.tv_sec < retry_time.tv_sec) continue; /* not time yet */
			if (time_now.tv_sec == retry_time.tv_sec &&
				time_now.tv_nsec < retry_time.tv_nsec) continue; /* not time yet */
			/* yes it's time */
			retry_dhcp = 0; /* Just to avoid a race condition */
			pthread_create (&thread, NULL, (void *) &dhcp_function, (void *) &tdata);
		} else if (state && (! renew_running) && !retry_dhcp) {
			/* IF is up, DHCP has succeeded, and not currently renewing, so renew */
			pthread_create (&thread, NULL, (void *) &dhcp_function_renew, (void *) &tdata);
		}
		sleep(30); 
	}
}
/*
 * isEthernetInUse
 *
 * test if ethernet is in use, or wifi.
 * If wifi is detected, return 0 else return 1
 *
 * gpio94 0 is radio off
 */
int isEthernetInUse(){
  char buf[8];
  int rtnVal = -1;
  int value = -1;
  FILE* file = fopen("/sys/class/gpio/gpio94/value","r");
  if(file){
    int rtn = -1;
    rtn = fread(buf, 1, 3, file);

    if(rtn > 0){
      int flag = atoi(buf);
      /* if flag low, radio is off, therefore eth is in use */
      rtnVal = flag == 0 ? 1 : 0; 
#ifdef DEBUG
      fprintf(stdout, "ethmonitor: isEthernetInUse. gpio94: %d rtnVal: %d\n", flag, rtnVal);
#endif
    }else{
      fprintf(stderr, "ethmonitor: isEthernetInUse. Error reading gpio94\n");
    }
  }else{
    fprintf(stderr, "ethmonitor: isEthernetInUse. Can't open gpio94\n");
  }
  return rtnVal;
}

// This tool is started in the Android init.rc with respawn
// to ensure connection reliability. Unfortunately it also
// gets started on a Wifi phone. Here we just let it sleep
// forever.
int main(int argc, char *argv[])
{

	if (argc == 1) {
		printf("Usage: ./ethmonitor eth0\n");
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "Invalid number of arguments\n");
		return -EINVAL;
	}

	if (ifc_init()) {
		fprintf(stderr, "ifc_init: Cannot perform requested operation\n");
		exit(EINVAL);
	}

	if (ifc_up(argv[1])) {
		fprintf(stderr, "ifc_up: Cannot bring up interface %s\n",argv[1]);
        sleep(LONG_TIME);
		return -ENODEV;
	}
   /* if not using ethernet, exit */
   if(isEthernetInUse() == 0){
     fprintf(stdout, "ethmonitor: ethernet not is use, exiting\n");
     exit(0);
   }

	monitor_connection(argv[1]);

    ifc_close();

	return 0;
};
