/*
 *	wiiuse
 *
 *	Written By:
 *		Michael Laforest	< para >
 *		Email: < thepara (--AT--) g m a i l [--DOT--] com >
 *
 *	Copyright 2006-2007
 *
 *	This file is part of wiiuse.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *	$Header$
 *
 */

/**
 *	@file
 *
 *	@brief Example using the wiiuse API.
 *
 *	This file is an example of how to use the wiiuse library.
 */

#include <stdio.h>                      /* for printf */
#include <pthread.h>

#include "wiiuse.h"                     /* for wiimote_t, classic_ctrl_t, etc */
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#ifndef WIIUSE_WIN32
#include <unistd.h>                     /* for usleep */
#endif

#define MAX_WIIMOTES				4

int uart0_filestream=-1;
pthread_mutex_t mutex;
int currentB=0, currentA=0, current1=0;
int continueToRun=1;

float currentX=0.5f, currentY=0.5f;
int calibrationPhase=4;
float calibrationX[4]={0,560,0,560};
float calibrationY[4]={0,0,420,420};
int calibrationButton=0;

float Cross(float ux, float uy, float vx, float vy)
{
	return ux*vy-uy*vx;
}

int Within(float x, float y)
{
	if (Cross(calibrationX[1]-calibrationX[0], calibrationY[1]-calibrationY[0], x-calibrationX[0], y-calibrationY[0])<0.0f)
		return 0;
	if (Cross(calibrationX[3]-calibrationX[1], calibrationY[3]-calibrationY[1], x-calibrationX[1], y-calibrationY[1])<0.0f)
		return 0;
	if (Cross(calibrationX[2]-calibrationX[3], calibrationY[2]-calibrationY[3], x-calibrationX[3], y-calibrationY[3])<0.0f)
		return 0;
	if (Cross(calibrationX[0]-calibrationX[2], calibrationY[0]-calibrationY[2], x-calibrationX[2], y-calibrationY[2])<0.0f)
		return 0;
	return 1;
}

float Solve(float x, float y, int a, int b, int c, int d)
{
	float calX[4], calY[4];
	int i;
	for (i=0; i<4; i++)
	{
		calX[i]=calibrationX[i]-x;
		calY[i]=calibrationY[i]-y;
	}
	float qa=Cross(calX[b]-calX[d], calY[b]-calY[d], calX[c]-calX[a], calY[c]-calY[a]);
	float qb=2.0f*Cross(calX[b], calY[b], calX[a], calY[a]);
	qb-=Cross(calX[b], calY[b], calX[c], calY[c]);
	qb-=Cross(calX[d], calY[d], calX[a], calY[a]);
	float qc=Cross(calX[a], calY[a], calX[b], calY[b]);
	if (qa==0.0f)
		return -qc/qb;
	float inner=qb*qb-4.0f*qa*qc;
	if (inner<0.0f)
		return -1.0f;
	float r=(-qb+sqrtf(inner))/(2.0f*qa);
	if (r<0.0f || r>1.0f)
		r=(-qb-sqrtf(inner))/(2.0f*qa);
	return r;
}

void *asyncSend(void *param)
{
	int odd=0;
	int lastA=0, last1=0;
	while (continueToRun)
	{
		int x,y,b,a,one;
		pthread_mutex_lock(&mutex);
		if (currentX>=0.0f && currentX<=1.0f && currentY>=0.0f && currentY<=1.0f)
		{
			x=currentX*640+0.5f;
			y=currentY*240+0.5f;
		}
		else
		{
			// TODO: Send that we are offscreen
			x=962;
			y=0;
		}
		b=currentB;
		a=currentA;
		one=current1;
		pthread_mutex_unlock(&mutex);
		y+=b?512:0;
		if (a!=lastA)
		{
			if (a)
				x=960;
			lastA=a;
		}
		else if (one!=last1)
		{
			if (one)
				x=961;
			last1=one;
		}
		unsigned char fibbles[4];
		fibbles[0]=x&0x1F;
		fibbles[0]|=odd<<5;
		fibbles[0]|=0<<6;
		fibbles[1]=(x>>5)&0x1F;
		fibbles[1]|=odd<<5;
		fibbles[1]|=1<<6;
		fibbles[2]=y&0x1F;
		fibbles[2]|=odd<<5;
		fibbles[2]|=2<<6;
		fibbles[3]=(y>>5)&0x1F;
		fibbles[3]|=odd<<5;
		fibbles[3]|=3<<6;
		odd=1-odd;
		if (write(uart0_filestream, fibbles, 4)<0)
			break;
		if (write(uart0_filestream, fibbles, 4)<0)
			break;
		if (write(uart0_filestream, fibbles, 4)<0)
			break;
		if (write(uart0_filestream, fibbles, 4)<0)
			break;
		if (write(uart0_filestream, fibbles, 3)<0)
			break;
		usleep(20000);
	}
	return NULL;
}

/**
 *	@brief Callback that handles an event.
 *
 *	@param wm		Pointer to a wiimote_t structure.
 *
 *	This function is called automatically by the wiiuse library when an
 *	event occurs on the specified wiimote.
 */
void handle_event(struct wiimote_t* wm) {
	if (WIIUSE_USING_IR(wm)) {
		int visible=0, i;
		if (IS_PRESSED(wm, WIIMOTE_BUTTON_HOME))
		{
			calibrationPhase=0;
			wiiuse_set_leds(wm, 0x10);
		}
		for (i=0; i<4; i++)
		{
			if (wm->ir.dot[i].visible)
			{
				visible=1;
				break;
			}
		}
		if (calibrationPhase<4 && visible)
		{
			int calBut=IS_PRESSED(wm, WIIMOTE_BUTTON_B);
			if (!calibrationButton && calBut)
			{
				calibrationX[calibrationPhase]=wm->ir.x;
				calibrationY[calibrationPhase]=wm->ir.y;
				calibrationPhase++;
				if (calibrationPhase<4)
				{
					wiiuse_set_leds(wm, 0x10<<calibrationPhase);
				}
				else
				{
					wiiuse_set_leds(wm, 0xF0);
				}
			}
			calibrationButton=calBut;
		}
		pthread_mutex_lock(&mutex);
		float X=wm->ir.x;
		float Y=wm->ir.y;
		if (visible && Within(X,Y))
		{
			currentX=Solve(X,Y,0,2,1,3);
			currentY=Solve(X,Y,0,1,2,3);
			currentX=(currentX<0.0f)?0.0f:(currentX>1.0f)?1.0f:currentX;
			currentY=(currentY<0.0f)?0.0f:(currentY>1.0f)?1.0f:currentY;
		}
		else
		{
			currentX=-2.0f;
			currentY=-2.0f;
		}
		currentA=IS_PRESSED(wm, WIIMOTE_BUTTON_A);
		current1=IS_PRESSED(wm, WIIMOTE_BUTTON_ONE);
		currentB=(calibrationPhase>=4) && IS_PRESSED(wm, WIIMOTE_BUTTON_B);
		pthread_mutex_unlock(&mutex);
		printf("IR cursor: %d, %d, (%.3f, %.3f)\n", wm->ir.x, wm->ir.y, currentX, currentY);
	}
}

/**
 *	@brief Callback that handles a read event.
 *
 *	@param wm		Pointer to a wiimote_t structure.
 *	@param data		Pointer to the filled data block.
 *	@param len		Length in bytes of the data block.
 *
 *	This function is called automatically by the wiiuse library when
 *	the wiimote has returned the full data requested by a previous
 *	call to wiiuse_read_data().
 *
 *	You can read data on the wiimote, such as Mii data, if
 *	you know the offset address and the length.
 *
 *	The \a data pointer was specified on the call to wiiuse_read_data().
 *	At the time of this function being called, it is not safe to deallocate
 *	this buffer.
 */
void handle_read(struct wiimote_t* wm, byte* data, unsigned short len) {
	int i = 0;

	printf("\n\n--- DATA READ [wiimote id %i] ---\n", wm->unid);
	printf("finished read of size %i\n", len);
	for (; i < len; ++i) {
		if (!(i % 16)) {
			printf("\n");
		}
		printf("%x ", data[i]);
	}
	printf("\n\n");
}


/**
 *	@brief Callback that handles a controller status event.
 *
 *	@param wm				Pointer to a wiimote_t structure.
 *	@param attachment		Is there an attachment? (1 for yes, 0 for no)
 *	@param speaker			Is the speaker enabled? (1 for yes, 0 for no)
 *	@param ir				Is the IR support enabled? (1 for yes, 0 for no)
 *	@param led				What LEDs are lit.
 *	@param battery_level	Battery level, between 0.0 (0%) and 1.0 (100%).
 *
 *	This occurs when either the controller status changed
 *	or the controller status was requested explicitly by
 *	wiiuse_status().
 *
 *	One reason the status can change is if the nunchuk was
 *	inserted or removed from the expansion port.
 */
void handle_ctrl_status(struct wiimote_t* wm) {
	printf("\n\n--- CONTROLLER STATUS [wiimote id %i] ---\n", wm->unid);

	printf("attachment:      %i\n", wm->exp.type);
	printf("speaker:         %i\n", WIIUSE_USING_SPEAKER(wm));
	printf("ir:              %i\n", WIIUSE_USING_IR(wm));
	printf("leds:            %i %i %i %i\n", WIIUSE_IS_LED_SET(wm, 1), WIIUSE_IS_LED_SET(wm, 2), WIIUSE_IS_LED_SET(wm, 3), WIIUSE_IS_LED_SET(wm, 4));
	printf("battery:         %f %%\n", wm->battery_level);
}


/**
 *	@brief Callback that handles a disconnection event.
 *
 *	@param wm				Pointer to a wiimote_t structure.
 *
 *	This can happen if the POWER button is pressed, or
 *	if the connection is interrupted.
 */
void handle_disconnect(wiimote* wm) {
	printf("\n\n--- DISCONNECTED [wiimote id %i] ---\n", wm->unid);
}


void test(struct wiimote_t* wm, byte* data, unsigned short len) {
	printf("test: %i [%x %x %x %x]\n", len, data[0], data[1], data[2], data[3]);
}

short any_wiimote_connected(wiimote** wm, int wiimotes) {
	int i;
	if (!wm) {
		return 0;
	}

	for (i = 0; i < wiimotes; i++) {
		if (wm[i] && WIIMOTE_IS_CONNECTED(wm[i])) {
			return 1;
		}
	}

	return 0;
}


/**
 *	@brief main()
 *
 *	Connect to up to two wiimotes and print any events
 *	that occur on either device.
 */
int main(int argc, char** argv) {
	wiimote** wiimotes;
	int found, connected;

	uart0_filestream=open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NDELAY);
	if (uart0_filestream == -1)
	{
		printf("Couldn't open serial\n");
		return 1;
	}
	struct termios options;
	tcgetattr(uart0_filestream, &options);
	options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	options.c_iflag = IGNPAR;
	options.c_oflag = 0;
	options.c_lflag = 0;
	tcflush(uart0_filestream, TCIFLUSH);
	tcsetattr(uart0_filestream, TCSANOW, &options);

	pthread_mutex_init(&mutex, NULL);
	//pthread_t thread;
	//pthread_create(&thread, NULL, asyncSend, NULL);

	/*
	 *	Initialize an array of wiimote objects.
	 *
	 *	The parameter is the number of wiimotes I want to create.
	 */
	wiimotes =  wiiuse_init(MAX_WIIMOTES);

	/*
	 *	Find wiimote devices
	 *
	 *	Now we need to find some wiimotes.
	 *	Give the function the wiimote array we created, and tell it there
	 *	are MAX_WIIMOTES wiimotes we are interested in.
	 *
	 *	Set the timeout to be 5 seconds.
	 *
	 *	This will return the number of actual wiimotes that are in discovery mode.
	 */
	found = wiiuse_find(wiimotes, MAX_WIIMOTES, 5);
	if (!found) {
		printf("No wiimotes found.\n");
		return 0;
	}

	/*
	 *	Connect to the wiimotes
	 *
	 *	Now that we found some wiimotes, connect to them.
	 *	Give the function the wiimote array and the number
	 *	of wiimote devices we found.
	 *
	 *	This will return the number of established connections to the found wiimotes.
	 */
	connected = wiiuse_connect(wiimotes, MAX_WIIMOTES);
	if (connected) {
		printf("Connected to %i wiimotes (of %i found).\n", connected, found);
	} else {
		printf("Failed to connect to any wiimote.\n");
		return 0;
	}

	/*
	 *	Now set the LEDs and rumble for a second so it's easy
	 *	to tell which wiimotes are connected (just like the wii does).
	 */
	wiiuse_set_leds(wiimotes[0], 0xF0);
	wiiuse_rumble(wiimotes[0], 1);
	wiiuse_set_ir(wiimotes[0], 1);

#ifndef WIIUSE_WIN32
	usleep(200000);
#else
	Sleep(200);
#endif

	wiiuse_rumble(wiimotes[0], 0);
	
	pthread_t thread;
	pthread_create(&thread, NULL, asyncSend, NULL);


	/*
	 *	This is the main loop
	 *
	 *	wiiuse_poll() needs to be called with the wiimote array
	 *	and the number of wiimote structures in that array
	 *	(it doesn't matter if some of those wiimotes are not used
	 *	or are not connected).
	 *
	 *	This function will set the event flag for each wiimote
	 *	when the wiimote has things to report.
	 */
	while (any_wiimote_connected(wiimotes, MAX_WIIMOTES)) {
		if (wiiuse_poll(wiimotes, MAX_WIIMOTES)) {
			/*
			 *	This happens if something happened on any wiimote.
			 *	So go through each one and check if anything happened.
			 */
			int i = 0;
			for (; i < MAX_WIIMOTES; ++i) {
				switch (wiimotes[i]->event) {
					case WIIUSE_EVENT:
						/* a generic event occurred */
						handle_event(wiimotes[i]);
						break;

					case WIIUSE_STATUS:
						/* a status event occurred */
						handle_ctrl_status(wiimotes[i]);
						break;

					case WIIUSE_DISCONNECT:
					case WIIUSE_UNEXPECTED_DISCONNECT:
						/* the wiimote disconnected */
						handle_disconnect(wiimotes[i]);
						break;

					case WIIUSE_READ_DATA:
						/*
						 *	Data we requested to read was returned.
						 *	Take a look at wiimotes[i]->read_req
						 *	for the data.
						 */
						break;

					case WIIUSE_NUNCHUK_INSERTED:
						/*
						 *	a nunchuk was inserted
						 *	This is a good place to set any nunchuk specific
						 *	threshold values.  By default they are the same
						 *	as the wiimote.
						 */
						/* wiiuse_set_nunchuk_orient_threshold((struct nunchuk_t*)&wiimotes[i]->exp.nunchuk, 90.0f); */
						/* wiiuse_set_nunchuk_accel_threshold((struct nunchuk_t*)&wiimotes[i]->exp.nunchuk, 100); */
						printf("Nunchuk inserted.\n");
						break;

					case WIIUSE_CLASSIC_CTRL_INSERTED:
						printf("Classic controller inserted.\n");
						break;

					case WIIUSE_WII_BOARD_CTRL_INSERTED:
						printf("Balance board controller inserted.\n");
						break;

					case WIIUSE_GUITAR_HERO_3_CTRL_INSERTED:
						/* some expansion was inserted */
						handle_ctrl_status(wiimotes[i]);
						printf("Guitar Hero 3 controller inserted.\n");
						break;

					case WIIUSE_MOTION_PLUS_ACTIVATED:
						printf("Motion+ was activated\n");
						break;

					case WIIUSE_NUNCHUK_REMOVED:
					case WIIUSE_CLASSIC_CTRL_REMOVED:
					case WIIUSE_GUITAR_HERO_3_CTRL_REMOVED:
					case WIIUSE_WII_BOARD_CTRL_REMOVED:
					case WIIUSE_MOTION_PLUS_REMOVED:
						/* some expansion was removed */
						handle_ctrl_status(wiimotes[i]);
						printf("An expansion was removed.\n");
						break;

					default:
						break;
				}
			}
		}
	}

	/*
	 *	Disconnect the wiimotes
	 */
	wiiuse_cleanup(wiimotes, MAX_WIIMOTES);
	continueToRun=0;
	sleep(1);
	close(uart0_filestream);

	return 0;
}
