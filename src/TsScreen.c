//============================================================================
// Name        : TsScreen.c
// Since       : 3/11/2013
// Author      : Alberto Realis-Luc <alberto.realisluc@gmail.com>
// Web         : http://www.alus.it/airnavigator/
// Copyright   : (C) 2010-2013 Alberto Realis-Luc
// License     : GNU GPL v2
// Repository  : https://github.com/AirNavigator/AirNavigator.git
// Last change : 5/11/2013
// Description : Touch screen manager
//============================================================================

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <barcelona/Barc_ts.h>
#include <barcelona/Barc_gps.h>
#include <barcelona/Barc_Battery.h>
#include "TsScreen.h"
#include "AirNavigator.h"


volatile short readingTS=-1; //-1 means still not initialized
static int tsfd=-1;
pthread_t threadTsScreen;
condVar_t condVar=NULL;
TS_EVENT lastTouch;
static int gps;


void TsScreenRelease() {
	pthread_mutex_destroy(&condVar->lastTouchMutex);
	pthread_cond_destroy(&condVar->lastTouchSignal);
	free(condVar);
	if(tsfd>=0) close(tsfd);
	tsfd=-1;
	readingTS=-1;
}

void initializeTsScreen() {
	condVar=(condVar_t)malloc(sizeof(struct condVarStruct));
	if(pthread_mutex_init(&condVar->lastTouchMutex,NULL) || pthread_cond_init(&condVar->lastTouchSignal,NULL)) {
		logText("TsScreen: ERROR while initializing mutex and condition variable\n");
		TsScreenRelease();
	}
	tsfd=open("/dev/ts",O_RDONLY|O_NOCTTY|O_NONBLOCK);
	if(tsfd<0) {
		logText("TsScreen: ERROR can't open the device: /dev/ts\n");
		TsScreenRelease();
	}
	ioctl(tsfd,TS_SET_RAW_OFF,NULL);
	readingTS=0; //in this case we can start the thread
}

void* runTsScreen(void *arg) { //This is the thread listening for input on the touch screen
	struct sigaction sa;
	sa.sa_handler = NULL;
	sa.sa_sigaction = NULL/*gotsig*/;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	fd_set fdset;
	int maxfd=tsfd+1;
	int read_len;
	while(readingTS) { //loop while waiting for input
		FD_ZERO(&fdset);
		FD_SET(tsfd, &fdset);
		if(sigaction((int)arg, &sa, NULL)<0) { //register signal to kill the thread otherwise it will wait until next input
			logText("TsScreen: ERROR while registering signal to stop listen thread.");
			readingTS=0;
			pthread_exit(NULL);
			return NULL;
		}
		select(maxfd,&fdset,NULL,NULL,NULL); //wait until we have something new
		TS_EVENT event;
		read_len=read(tsfd,&event,sizeof(TS_EVENT));
		if(read_len==sizeof(TS_EVENT)) {
			if(event.pressure==0) { //to detect when the finger is going away from the screen so the touch is completed
				pthread_mutex_lock(&condVar->lastTouchMutex);
				lastTouch=event; //it's a simple struct no need to use memcpy
				//memcpy(&new_event,&prev_event,sizeof(TS_EVENT));
				pthread_cond_signal(&condVar->lastTouchSignal); //signal to the main that we have a new touch
				pthread_mutex_unlock(&condVar->lastTouchMutex);
			}
		}
	}
	pthread_exit(NULL);
	return NULL;
}

short TsScreenStart() {
	if(readingTS==-1) initializeTsScreen();
	if(readingTS==0) {
		readingTS=1;
		if(pthread_create(&threadTsScreen,NULL,runTsScreen,(void*)SIGUSR1)) {
			readingTS=0;
			logText("TsScreen: ERROR unable to create the reading thread.\n");
			TsScreenRelease();
		}
	}
	return readingTS;
}

void TsScreenClose() {
	if(readingTS==1) {
		readingTS=0;
		pthread_kill(threadTsScreen,SIGUSR1); //send the signa to kill the thread
		pthread_join(threadTsScreen,NULL); //wait for thread death
	}
	TsScreenRelease();
}

condVar_t TsScreenGetCondVar() {
	return condVar;
}

TS_EVENT TsScreenGetLastTouch() {
	return lastTouch;
}

short checkBattery(short *batVolt, short *refVolt, short *chargeCurr) {
	int battery=open("/dev/battery",O_RDONLY|O_NOCTTY);
	if(battery<0) return 0;
	else {
		BATTERY_STATUS bs;
		if(ioctl(battery,IOR_BATTERY_STATUS,&bs)==-1) { //if the return values is -1 it means fault
			close(battery);
			return -1;
		}
		*batVolt=(short)bs.u16BatteryVoltage;   // battery voltage
		*refVolt=(short)bs.u16ReferenceVoltage; // reference voltage
		*chargeCurr=(short)bs.u16ChargeCurrent; // Charge current
		//What kind of units do we have here?
		close(battery);
	}
	return 1;
}

short enableGPS() {
	gps=open("/dev/gps",O_RDWR);
	if(gps<0) return 0;
	else {
		if(ioctl(gps,IOW_GPS_ON,NULL)==-1) { //if the return values is -1 it means fault
			close(gps);
			gps=-1;
			return -1;
		}
	}
	return 1;
}

void disableGPS() {
	if(gps>=0) close(gps);
}
