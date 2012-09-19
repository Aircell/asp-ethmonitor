/*
 * Author: Matthew Ranostay <Matt_Ranostay@mentor.com>
 * Copyright (C) 2009 Mentor Graphics
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#define DEBUG 0


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

#include <cutils/properties.h>
#include <private/android_filesystem_config.h>

int ifc_init();
void ifc_close();
int ifc_up(char *iname);
int ifc_down(char *iname);
int do_dhcp(char *iname);

static int thread_running = 0;
static int LONG_TIME = 600;

void dhcp_function(void *ptr)
{
	char buf[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];
	char *interface = (char *) ptr;
  fprintf(stdout, "ethmonitor: start dhcp_function\n");
	thread_running = 1;

#ifdef DEBUG
	printf("dhcp_function calling do_dhcp\n");
#endif

	do_dhcp(interface);
#ifdef DEBUG
	printf("dhcp_function do_dhcp returned\n");
#endif
	sleep(1); /* beagleboard needs a second */

	/* DNS setting #1 */
	snprintf(buf, sizeof(buf), "net.%s.dns1", interface);
	property_get(buf, value, "");
	property_set("net.dns1", value);

	/* Report status of network connection */
	snprintf(buf, sizeof(buf), "net.%s.status", interface);
	property_set(buf, !!strcmp(value, "") ? "dhcp" : "up");

	/* DNS setting #2 */
	snprintf(buf, sizeof(buf), "net.%s.dns2", interface);
	property_get(buf, value, "");
	property_set("net.dns2", value);

	thread_running = 0;
  fprintf(stdout, "ethmonitor: exit dhcp_function\n");
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

	pthread_t thread;
#ifdef DEBUG
   printf("ethmonitor monitor_connection 1\n");
#endif

	while (1) {
			sleep(5); 
		/* setup the control structures */
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, interface);

		fd = socket(AF_INET, SOCK_DGRAM, 0);

		if (fd < 0) { /* this shouldn't ever happen */
			fprintf(stderr, "Cannot open control interface for %s 1. try again", interface);
			continue; //exit(errno);
		}

		tmp_state = get_link_status(fd, &ifr);
#ifdef DEBUG
      printf("ethmonitor.monitor_connection 4 get_link_status returned. connection state changed: %d previous state: %d\n", tmp_state, state);
#endif
		if (tmp_state != state) { /* state changed */

			state = tmp_state;

			/* Used by JNI code to find current DHCP device */
			property_set("net.device", interface);

			snprintf(buf, sizeof(buf), "net.%s.status", interface);
			property_set(buf, state ? "up" : "down");

			if (thread_running && !tmp_state){
#ifdef DEBUG
           printf("Cannot open control interface for %s 2. try again\n", interface);
#endif
           //exit(0); /* pthread_kill doesn't work correctly */
           continue;
         }

			if (state) { /* bring up connection */
#ifdef DEBUG
				printf("Connection up %s\n", interface);
#endif
        fprintf(stdout, "ethmonitor: monitor_connection  5 call dhcp_function\n");
				pthread_create (&thread, NULL, (void *) &dhcp_function, (void *) interface);

			} else { /* down connection */
#ifdef DEBUG
				printf("Connection down %s\n", interface);
#endif
			}

		}
	    close(fd);
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
