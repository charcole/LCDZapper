/*
             LUFA Library
     Copyright (C) Dean Camera, 2017.

  dean [at] fourwalledcubicle [dot] com
           www.lufa-lib.org
*/

/*
  Copyright 2017  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the Mouse demo. This file contains the main tasks of the demo and
 *  is responsible for the initial application hardware configuration.
 */

#include "Mouse.h"

#define BAUD 9600

#include <util/setbaud.h>

/** Indicates what report mode the host has requested, true for normal HID reporting mode, \c false for special boot
 *  protocol reporting mode.
 */
static bool UsingReportProtocol = true;

/** Current Idle period. This is set by the host via a Set Idle HID class request to silence the device's reports
 *  for either the entire idle duration, or until the report status changes (e.g. the user moves the mouse).
 */
static uint16_t IdleCount = 0;

/** Current Idle period remaining. When the IdleCount value is set, this tracks the remaining number of idle
 *  milliseconds. This is separate to the IdleCount timer and is incremented and compared as the host may request
 *  the current idle period via a Get Idle HID class request, thus its value must be preserved.
 */
static uint16_t IdleMSRemaining = 0;

void UART_Tick(void);

/** Main program entry point. This routine configures the hardware required by the application, then
 *  enters a loop to run the application tasks in sequence.
 */
int main(void)
{
	SetupHardware();

	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
	GlobalInterruptEnable();

	for (;;)
	{
		UART_Tick();
		Mouse_Task();
		USB_USBTask();
	}
}

// Screen flash control
volatile int FrameNum = 0;
int LastSentFrame = -1;
volatile bool bScreenDetected[4] = {false, false, false, false};

ISR(INT0_vect)
{
	bScreenDetected[FrameNum & 3] = true;
}

ISR(INT1_vect)
{
	bScreenDetected[(FrameNum + 1) & 3] = false;
	FrameNum++;
}

void UART_Init(void)
{
    UBRR1H = UBRRH_VALUE;
    UBRR1L = UBRRL_VALUE;

#if USE_2X
    UCSR1A |= _BV(U2X1);
#else
    UCSR1A &= ~(_BV(U2X1));
#endif

	UCSR1B = _BV(RXEN1);
	UCSR1C = _BV(UCSZ11) | _BV(UCSZ10);
}

#define CONTROLLER_DATA_SIZE 8  // From LightGunVerter
#define INVALID_WIIMOTE_POS 0x3FFF
uint8_t ControllerReadIndex = 0;
uint8_t ControllerData[CONTROLLER_DATA_SIZE];
uint16_t WiimoteButtons = 0;
uint16_t WiimoteX = INVALID_WIIMOTE_POS;
uint16_t WiimoteY = INVALID_WIIMOTE_POS;

void UART_Tick(void)
{
	if ((UCSR1A & _BV(RXC1)) != 0) // Serial data ready
	{
		//if((UCSR1A & (_BV(FE1) | _BV(DOR1) | _BV(UPE1))) == 0)
		{
			uint8_t SerialData = UDR1;
			if (SerialData == 0x80)
			{
				if (ControllerReadIndex == CONTROLLER_DATA_SIZE) // Last lot of serial data was good
				{
					if (ControllerData[0] == 0x80 && ControllerData[1] == 0xCC) // Player 1 data
					{
						WiimoteX = (ControllerData[2] << 7) + ControllerData[3];
            			WiimoteY = (ControllerData[4] << 7) + ControllerData[5];	
						WiimoteButtons = (ControllerData[6] << 7) | ControllerData[7];
					}
				}
				ControllerReadIndex = 0;
			}
			if (ControllerReadIndex < CONTROLLER_DATA_SIZE)
			{
				ControllerData[ControllerReadIndex] = SerialData;
			}
			ControllerReadIndex++;
		}
	}
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
#if (ARCH == ARCH_AVR8)
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);
#elif (ARCH == ARCH_XMEGA)
	/* Start the PLL to multiply the 2MHz RC oscillator to 32MHz and switch the CPU core to run from it */
	XMEGACLK_StartPLL(CLOCK_SRC_INT_RC2MHZ, 2000000, F_CPU);
	XMEGACLK_SetCPUClockSource(CLOCK_SRC_PLL);

	/* Start the 32MHz internal RC oscillator and start the DFLL to increase it to 48MHz using the USB SOF as a reference */
	XMEGACLK_StartInternalOscillator(CLOCK_SRC_INT_RC32MHZ);
	XMEGACLK_StartDFLL(CLOCK_SRC_INT_RC32MHZ, DFLL_REF_INT_USBSOF, F_USB);

	PMIC.CTRL = PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
#endif

	/* Hardware Initialization */
	//Joystick_Init();
	LEDs_Init();
	//Buttons_Init();
	USB_Init();
	
	UART_Init();

	EIMSK |= _BV(INT0); // Trigger INT0 on falling edge
	EICRA |= _BV(ISC01);
	EICRA &= ~_BV(ISC00);
	
	EIMSK |= _BV(INT1);	// Trigger INT1 on rising edge
	EICRA |= _BV(ISC10) | _BV(ISC11);

	DDRB &= ~((1 << 6) | (1 << 5)); // Setup pin 9 + 10 (PB5 + PB6) as an input with pullup
	PORTB |= (1 << 6) | (1 << 5);
}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs and
 *  starts the library USB task to begin the enumeration and USB management process.
 */
void EVENT_USB_Device_Connect(void)
{
	/* Indicate USB enumerating */
	LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);

	/* Default to report protocol on connect */
	UsingReportProtocol = true;
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
 *  the status LEDs and stops the USB management and Mouse reporting tasks.
 */
void EVENT_USB_Device_Disconnect(void)
{
	/* Indicate USB not ready */
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host sets the current configuration
 *  of the USB device after enumeration - the device endpoints are configured and the mouse reporting task started.
 */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	/* Setup HID Report Endpoint */
	ConfigSuccess &= Endpoint_ConfigureEndpoint(MOUSE_EPADDR, EP_TYPE_INTERRUPT, MOUSE_EPSIZE, 1);

	/* Turn on Start-of-Frame events for tracking HID report period expiry */
	USB_Device_EnableSOFEvents();

	/* Indicate endpoint configuration success or failure */
	LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

/** Event handler for the USB_ControlRequest event. This is used to catch and process control requests sent to
 *  the device from the USB host before passing along unhandled control requests to the library for processing
 *  internally.
 */
void EVENT_USB_Device_ControlRequest(void)
{
	/* Handle HID Class specific requests */
	switch (USB_ControlRequest.bRequest)
	{
		case HID_REQ_GetReport:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				USB_MouseReport_Data_t MouseReportData;

				/* Create the next mouse report for transmission to the host */
				CreateMouseReport(&MouseReportData);

				Endpoint_ClearSETUP();

				/* Write the report data to the control endpoint */
				Endpoint_Write_Control_Stream_LE(&MouseReportData, sizeof(MouseReportData));
				Endpoint_ClearOUT();

				/* Clear the report data afterwards */
				memset(&MouseReportData, 0, sizeof(MouseReportData));
			}

			break;
		case HID_REQ_GetProtocol:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				/* Write the current protocol flag to the host */
				Endpoint_Write_8(UsingReportProtocol);

				Endpoint_ClearIN();
				Endpoint_ClearStatusStage();
			}

			break;
		case HID_REQ_SetProtocol:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();
				Endpoint_ClearStatusStage();

				/* Set or clear the flag depending on what the host indicates that the current Protocol should be */
				UsingReportProtocol = (USB_ControlRequest.wValue != 0);
			}

			break;
		case HID_REQ_SetIdle:
			if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();
				Endpoint_ClearStatusStage();

				/* Get idle period in MSB, must multiply by 4 to get the duration in milliseconds */
				IdleCount = ((USB_ControlRequest.wValue & 0xFF00) >> 6);
			}

			break;
		case HID_REQ_GetIdle:
			if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
			{
				Endpoint_ClearSETUP();

				/* Write the current idle duration to the host, must be divided by 4 before sent to host */
				Endpoint_Write_8(IdleCount >> 2);

				Endpoint_ClearIN();
				Endpoint_ClearStatusStage();
			}

			break;
	}
}

/** Event handler for the USB device Start Of Frame event. */
void EVENT_USB_Device_StartOfFrame(void)
{
	/* One millisecond has elapsed, decrement the idle time remaining counter if it has not already elapsed */
	if (IdleMSRemaining)
	  IdleMSRemaining--;
}

/** Fills the given HID report data structure with the next HID report to send to the host.
 *
 *  \param[out] ReportData  Pointer to a HID report data structure to be filled
 */
void CreateMouseReport(USB_MouseReport_Data_t* const ReportData)
{
#if 0
	uint8_t JoyStatus_LCL    = Joystick_GetStatus();
	uint8_t ButtonStatus_LCL = Buttons_GetStatus();
#endif

	/* Clear the report contents */
	memset(ReportData, 0, sizeof(USB_MouseReport_Data_t));

#if 0
	if (JoyStatus_LCL & JOY_UP)
	  ReportData->Y = -1;
	else if (JoyStatus_LCL & JOY_DOWN)
	  ReportData->Y =  1;

	if (JoyStatus_LCL & JOY_LEFT)
	  ReportData->X = -1;
	else if (JoyStatus_LCL & JOY_RIGHT)
	  ReportData->X =  1;

	if (JoyStatus_LCL & JOY_PRESS)
	  ReportData->Button |= (1 << 0);

	if (ButtonStatus_LCL & BUTTONS_BUTTON1)
	  ReportData->Button |= (1 << 1);
#endif
	  ReportData->X =  1;
}

/** Sends the next HID report to the host, via the keyboard data endpoint. */
void SendNextReport(void)
{
	//static USB_MouseReport_Data_t PrevMouseReportData;
	//USB_MouseReport_Data_t        MouseReportData;
	bool                          SendReport = false;
	static uint8_t                PrevGunConData[6] = {0};
	uint8_t                       GunConData[6] = { 0 };

	/* Create the next mouse report for transmission to the host */
	//CreateMouseReport(&MouseReportData);

	/* Check to see if the report data has changed - if so a report MUST be sent */
	//SendReport = (memcmp(&PrevMouseReportData, &MouseReportData, sizeof(USB_MouseReport_Data_t)) != 0);
	#define START 0x8000
	#define SELECT 0x4000
	#define TRIGGER 0x2000
	#define LEFT 0x0080
	#define DOWN 0x0040
	#define RIGHT 0x0020
	#define UP 0x0010
	#define LEFTBUT 0x0008
	#define RIGHTBUT 0x0004
	#define STOCK 0x0002

	enum WiimoteData
	{
		kButton_Left=(1<<0),
		kButton_Right=(1<<1),
		kButton_Down=(1<<2),
		kButton_Up=(1<<3),
		kButton_Plus=(1<<4),
		kButton_Two=(1<<8),
		kButton_One=(1<<9),
		kButton_B=(1<<10),
		kButton_A=(1<<11),
		kButton_Minus=(1<<12),
		kButton_Home=(1<<15),
	};

	uint16_t X = WiimoteX;
	//X = 1023 - X;
	//X = (X >> 1) + (X >> 4) + 132; // X = 132 to 708 (diff 576)
	uint16_t Y = WiimoteY;
	//Y = (Y >> 2) + (Y >> 4) + (Y >> 6) + (Y >> 7) + 30; // Y = 30 to 288 (diff 258)
	if (WiimoteX == INVALID_WIIMOTE_POS || WiimoteY == INVALID_WIIMOTE_POS)
	{
		X = 0;
		Y = 0;
	}

	uint16_t Buttons = 0xFFFF;
	if (WiimoteButtons & kButton_Left)
		Buttons &= ~LEFT;
	if (WiimoteButtons & kButton_Right)
		Buttons &= ~RIGHT;
	if (WiimoteButtons & kButton_Up)
		Buttons &= ~UP;
	if (WiimoteButtons & kButton_Down)
		Buttons &= ~DOWN;
	if ((WiimoteButtons & kButton_B) || (PINB & (1 << 5)) == 0) // Allow pull down on pin 9 (PB5) for external trigger
		Buttons &= ~TRIGGER;
	if ((WiimoteButtons & kButton_A) || (PINB & (1 << 6)) == 0) // Allow pull down on pin 10 (PB6) for external pedal
		Buttons &= ~STOCK;
	if (WiimoteButtons & kButton_Minus)
		Buttons &= ~SELECT;
	if (WiimoteButtons & kButton_Plus)
		Buttons &= ~START;
	if (WiimoteButtons & kButton_One)
		Buttons &= ~LEFTBUT;
	if (WiimoteButtons & kButton_Two)
		Buttons &= ~RIGHTBUT;
	
	int CurrentFrameNum = FrameNum;
	if (CurrentFrameNum != LastSentFrame)
	{
		LastSentFrame = CurrentFrameNum;
		SendReport = true;
	}

	if (!bScreenDetected[(CurrentFrameNum - 1) & 3])
	{
		X = 0;
		Y = 0;
	}
	
	GunConData[0] = Buttons & 0xFF;
	GunConData[1] = Buttons >> 8;
	GunConData[2] = X & 0xFF;
	GunConData[3] = X >> 8;
	GunConData[4] = Y & 0xFF;
	GunConData[5] = Y >> 8;

	//SendReport = (memcmp(PrevGunConData, GunConData, sizeof(GunConData)) != 0);

	/* Override the check if the Y or X values are non-zero - we want continuous movement while the joystick
	 * is being held down (via continuous reports), otherwise the cursor will only move once per joystick toggle */
	//if ((MouseReportData.Y != 0) || (MouseReportData.X != 0))
	//  SendReport = true;

	/* Check if the idle period is set and has elapsed */
	if (IdleCount && (!(IdleMSRemaining)))
	{
		/* Reset the idle time remaining counter */
		IdleMSRemaining = IdleCount;

		/* Idle period is set and has elapsed, must send a report to the host */
		SendReport = true;
	}

	/* Select the Mouse Report Endpoint */
	Endpoint_SelectEndpoint(MOUSE_EPADDR);

	/* Check if Mouse Endpoint Ready for Read/Write and if we should send a new report */
	if (Endpoint_IsReadWriteAllowed() && SendReport)
	{
		/* Save the current report data for later comparison to check for changes */
		//PrevMouseReportData = MouseReportData;
		memcpy(PrevGunConData, GunConData, sizeof(GunConData));

		/* Write Mouse Report Data */
		//Endpoint_Write_Stream_LE(&MouseReportData, sizeof(MouseReportData), NULL);
		Endpoint_Write_Stream_LE(GunConData, sizeof(GunConData), NULL);

		/* Finalize the stream transfer to send the last packet */
		Endpoint_ClearIN();
	}
}

/** Task to manage HID report generation and transmission to the host, when in report mode. */
void Mouse_Task(void)
{
	/* Device must be connected and configured for the task to run */
	if (USB_DeviceState != DEVICE_STATE_Configured)
	  return;

	/* Send the next mouse report to the host */
	SendNextReport();
}

