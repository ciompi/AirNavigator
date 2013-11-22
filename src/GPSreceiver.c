//============================================================================
// Name        : NMEAreader.c
// Since       : 9/2/2011
// Author      : Alberto Realis-Luc <alberto.realisluc@gmail.com>
// Web         : http://www.alus.it/airnavigator/
// Copyright   : (C) 2010-2013 Alberto Realis-Luc
// License     : GNU GPL v2
// Repository  : https://github.com/AirNavigator/AirNavigator.git
// Last change : 21/11/2013
// Description : Reads from a NMEA serial device NMEA sentences and parse them
//============================================================================


//#define SERIAL_DEVICE

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef SERIAL_DEVICE
#include <termios.h>
#endif
#include <pthread.h>
#include <fcntl.h>
#include "GPSreceiver.h"
#include "AirNavigator.h"
#include "Configuration.h"
#include "AirCalc.h"
#include "Geoidal.h"
#include "NMEAparser.h"
#include "FBrender.h"
#include "HSI.h"
#include "BlackBox.h"

//#define PRINT_RECEIVED_DATA

void configureGPSreceiver(void);
void* run(void *ptr);

struct GPSdata gps = {
	.timestamp=-1,
	.speedKmh=-100,
	.speedKnots=-100,
	.altMt=-100,
	.altFt=-100,
	.realAltMt=-100,
	.realAltFt=-100,
	.trueTrack=-500,
	.day=-65,
	.second=-65,
	.latMinDecimal=-70,
	.lonMinDecimal=-70,
	.lat=100,
	.pdop=50,
	.hdop=50,
	.vdop=50,
	.fixMode=MODE_UNKNOWN
};

#ifdef SERIAL_DEVICE
long BAUD;
int DATABITS,STOPBITS,PARITYON,PARITY;
#endif

pthread_t thread;
volatile short readingGPS=-1; //-1 means still not initialized


void configureGPSreceiver(void) {
	if(config.GPSdevName==NULL) config.GPSdevName=strdup("/var/run/gpsfeed"); //Default value
#ifdef SERIAL_DEVICE
	switch(config.GPSbaudRate){ //configuration of the serial port
		case 230400:
			BAUD=B230400;
			break;
		case 115200:
		default:
			BAUD=B115200;
			break;
		case 57600:
			BAUD=B57600;
			break;
		case 38400:
			BAUD=B38400;
			break;
		case 19200:
			BAUD=B19200;
			break;
		case 9600:
			BAUD=B9600;
			break;
		case 4800:
			BAUD=B4800;
			break;
		case 2400:
			BAUD=B2400;
			break;
		case 1800:
			BAUD=B1800;
			break;
		case 1200:
			BAUD=B1200;
			break;
		case 600:
			BAUD=B600;
			break;
		case 300:
			BAUD=B300;
			break;
		case 200:
			BAUD=B200;
			break;
		case 150:
			BAUD=B150;
			break;
		case 134:
			BAUD=B134;
			break;
		case 110:
			BAUD=B110;
			break;
		case 75:
			BAUD=B75;
			break;
		case 50:
			BAUD=B50;
			break;
	} //end of switch baud_rate
	switch(config.GPSdataBits){
		case 8:
		default:
			DATABITS=CS8;
			break;
		case 7:
			DATABITS=CS7;
			break;
		case 6:
			DATABITS=CS6;
			break;
		case 5:
			DATABITS=CS5;
			break;
	} //end of switch data_bits
	switch(config.GPSstopBits){
		case 1:
		default:
			STOPBITS=0;
			break;
		case 2:
			STOPBITS=CSTOPB;
			break;
	} //end of switch stop bits
	switch(config.GPSparity){
		case 0:
		default: //none
			PARITYON=0;
			PARITY=0;
			break;
		case 1: //odd
			PARITYON=PARENB;
			PARITY=PARODD;
			break;
		case 2: //even
			PARITYON=PARENB;
			PARITY=0;
			break;
	} //end of switch parity
#endif
	GeoidalOpen();
	readingGPS=0;
	updateNumOfTotalSatsInView(0); //Display: at the moment we have no info from GPS
	updateNumOfActiveSats(0);
	FBrenderFlush();
}

void* run(void *ptr) { //listening function, it will be ran in a separate thread
	static int fd=-1;
	fd=open(config.GPSdevName,O_RDONLY|O_NOCTTY|O_NONBLOCK); //read only, non blocking
	if(fd<0) {
		fd=-1;
		logText("ERROR: Can't open the gps serial port on device: %s\n",config.GPSdevName);
		readingGPS=0;
		pthread_exit(NULL);
		return NULL;
	}
#ifdef SERIAL_DEVICE
	struct termios oldtio,newtio; //place for old and new port settings for serial port
	tcgetattr(fd,&oldtio); // save current port settings
	newtio.c_cflag=BAUD|CRTSCTS|DATABITS|STOPBITS|PARITYON|PARITY|CLOCAL|CREAD; // set new port settings for canonical input processing
	newtio.c_iflag=IGNPAR;
	newtio.c_oflag=0;
	newtio.c_lflag=0; //ICANON;
	newtio.c_cc[VMIN]=1;
	newtio.c_cc[VTIME]=0;
	tcsetattr(fd,TCSANOW,&newtio); // Set the new options for the port...
	tcflush(fd,TCIFLUSH);
#endif

	short isSiRF=0; //TODO: this will select between SiRF or NMEA put in a proper configurable place!
	static unsigned char *buf; //read buffer
	if(isSiRF) { //allocate buffer for SiRF protocol
		//buf=(unsigned char *) malloc(SIRF_BUFFER_SIZE*sizeof(unsigned char)); //TODO: set the correct buffer size for SiRF
	} else { //allocate buffer for NMEA protocol
		buf=(unsigned char *) malloc(NMEA_BUFFER_SIZE*sizeof(unsigned char));
	}
	int maxfd=fd+1;
	fd_set readfs;
	long redBytes;
	//long timestamp; //timestamp of last bytes sequence received
	while(readingGPS) { // loop while waiting for input
		FD_SET(fd,&readfs);
		select(maxfd,&readfs,NULL,NULL,NULL); //wait to read because the read is now non-blocking
		if(readingGPS) { //further check if we still want to read after waiting
			redBytes=read(fd,buf,NMEA_BUFFER_SIZE);
			//timestamp=time(NULL); //get the timestamp of last bytes sequence received. Do we really need it?
			//if(isSiRF) SiRFparserProcessBuffer(buf,timestamp,redBytes); //TODO: still to be done...
			/* else */ NMEAparserProcessBuffer(buf,redBytes);
		}
	}
#ifdef SERIAL_DEVICE
	tcsetattr(fd,TCSANOW,&oldtio); //restore old port settings
#endif
	if(fd>=0) close(fd); //close the serial port
	free(buf);
	pthread_exit(NULL);
	return NULL;
}

short GPSreceiverStart(void) { //function to start the listening thread
	if(readingGPS==-1) configureGPSreceiver();
	if(!readingGPS) {
		readingGPS=1;
		if(pthread_create(&thread,NULL,run,(void*)NULL)) {
			readingGPS=0;
			logText("GPSreceiver: ERROR unable to create the reading thread.\n");
		}
	}
	return readingGPS;
}

void GPSreceiverStop(void) {
	if(readingGPS==1) readingGPS=0;
}

void GPSreceiverClose(void) {
	GPSreceiverStop();
	GeoidalClose();
	pthread_join(thread,NULL); //wait for thread death
}

void updateHdiluition(float hDiluition) {
	if(gps.hdop!=hDiluition) {
		gps.hdop=hDiluition;
		PrintDiluitions(gps.pdop,gps.hdop,gps.vdop);
	}
}

void updateDiluition(float pDiluition, float hDiluition, float vDiluition) {
	if(gps.pdop!=pDiluition||gps.hdop!=hDiluition||gps.vdop!=vDiluition) {
		gps.pdop=pDiluition;
		gps.hdop=hDiluition;
		gps.vdop=vDiluition;
		PrintDiluitions(pDiluition,hDiluition,vDiluition);
	}
}

short updateDate(int newDay, int newMonth, int newYear) {
	if(gps.day!=newDay) {
		gps.day=newDay;
		gps.month=newMonth;
		gps.year=newYear;
		if(gps.year<2000) gps.year+=2000;
		PrintDate(gps.day,gps.month,gps.year);
		return 1;
	}
	return 0;
}

void updateTime(float timestamp, int newHour, int newMin, float newSec, short timeWithNoFix) {
	if(gps.timestamp!=timestamp) {
		gps.timestamp=timestamp;
		gps.hour=newHour;
		gps.minute=newMin;
		gps.second=newSec;
		PrintTime(gps.hour,gps.minute,gps.second,timeWithNoFix);
		if(timeWithNoFix) FBrenderFlush();
	}
}

void updateGroundSpeedAndDirection(float newSpeedKmh, float newSpeedKnots, float newTrueTrack, float newMagneticTrack) {
	if(newSpeedKnots!=gps.speedKnots) {
		gps.speedKnots=newSpeedKnots;
		gps.speedKmh=newSpeedKmh;
		PrintSpeed(newSpeedKmh,newSpeedKnots);
	}
	if(newSpeedKmh>2) if(newTrueTrack!=gps.trueTrack) {
		gps.trueTrack=newTrueTrack;
		gps.magneticTrack=newMagneticTrack;
		HSIupdateDir(newTrueTrack,newMagneticTrack);
	}
	BlackBoxRecordSpeed(Kmh2ms(newSpeedKmh));
	if(gps.speedKmh>4) BlackBoxRecordCourse(newTrueTrack);
}

void updateSpeed(float newSpeedKnots) {
	if(newSpeedKnots!=gps.speedKnots) {
		gps.speedKnots=newSpeedKnots;
		gps.speedKmh=Nm2Km(newSpeedKnots);
		PrintSpeed(gps.speedKmh,gps.speedKnots);
	}
	BlackBoxRecordSpeed(Kmh2ms(gps.speedKmh));
}

void updateNumOfTotalSatsInView(int totalSats) {
	if(gps.satsInView!=totalSats) {
		gps.satsInView=totalSats;
		PrintNumOfSats(gps.activeSats,gps.satsInView);
	}
}

void updateNumOfActiveSats(int workingSats) {
	if(gps.activeSats!=workingSats) {
		gps.activeSats=workingSats;
		PrintNumOfSats(gps.activeSats,gps.satsInView);
	}
}

void updateFixMode(int fixMode) {
	if(gps.fixMode!=fixMode) {
		if(fixMode==MODE_GPS_FIX && (gps.fixMode==MODE_2D_FIX || gps.fixMode==MODE_3D_FIX)) return;
		gps.fixMode=fixMode;
		PrintFixMode(fixMode);
		if(fixMode==MODE_NO_FIX) FBrenderFlush(); //because we want to show it immediately
	}
}
