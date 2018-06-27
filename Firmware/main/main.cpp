// (c) Charlie Cole 2017
//
// This is licensed under
// - Creative Commons Attribution-NonCommercial 3.0 Unported
// - https://creativecommons.org/licenses/by-nc/3.0/
// - Or see LICENSE.txt
//
// The short of it is...
//   You are free to:
//     Share — copy and redistribute the material in any medium or format
//     Adapt — remix, transform, and build upon the material
//   Under the following terms:
//     NonCommercial — You may not use the material for commercial purposes.
//     Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.

#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/rmt.h"
#include "driver/timer.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_ota_ops.h"
#include "rom/rtc.h"
};
#include "esp_wiimote.h"
#include "images.h"

#define OUT_SCREEN_DIM  (GPIO_NUM_23) // Controls drawing spot on screen
#define OUT_SCREEN_DIMER  (GPIO_NUM_33) // Controls drawing spot on screen
#define OUT_SCREEN_DIM_INV (GPIO_NUM_22) // Inverted version of above
#define OUT_SCREEN_DIM_SELECTION_REG (GPIO_FUNC23_OUT_SEL_CFG_REG) // Used to route which bank goes to the screen dimming
#define OUT_SCREEN_DIMER_SELECTION_REG (GPIO_FUNC33_OUT_SEL_CFG_REG) // Used to route which bank goes to the screen dimming
#define OUT_SCREEN_DIM_INV_SELECTION_REG (GPIO_FUNC22_OUT_SEL_CFG_REG) // Used to route which bank goes to the screen dimming
#define OUT_PLAYER1_LED (GPIO_NUM_18) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED (GPIO_NUM_17) // ANDed with detected white level in HW
#define OUT_PLAYER1_LED_DELAYED (GPIO_NUM_5) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED_DELAYED (GPIO_NUM_16) // ANDed with detected white level in HW
#define OUT_PLAYER1_TRIGGER1_PULLED (GPIO_NUM_25) // Used for Wiimote-only operation
#define OUT_PLAYER1_TRIGGER2_PULLED (GPIO_NUM_26) // Used for Wiimote-only operation
#define OUT_PLAYER2_TRIGGER1_PULLED (GPIO_NUM_27) // Used for Wiimote-only operation
#define OUT_PLAYER2_TRIGGER2_PULLED (GPIO_NUM_14) // Used for Wiimote-only operation
#define OUT_WHITE_OVERRIDE (GPIO_NUM_19) // Ignore the white level
#define OUT_FRONT_PANEL_LED1 (GPIO_NUM_32) // Green LED on RJ45
#define OUT_FRONT_PANEL_LED2 (GPIO_NUM_4) // Green LED on RJ45

#define IN_COMPOSITE_SYNC (GPIO_NUM_21) // Compsite sync input (If changed change also in asm loop)
#define IN_UPLOAD_BUTTON (GPIO_NUM_0) // Upload button

#define RMT_SCREEN_DIM_CHANNEL    	RMT_CHANNEL_1     /*!< RMT channel for screen*/
#define RMT_TRIGGER_CHANNEL			RMT_CHANNEL_3     /*!< RMT channel for trigger */
#define RMT_DELAY_TRIGGER_CHANNEL	RMT_CHANNEL_5     /*!< RMT channel for delayed trigger */
#define RMT_BACKGROUND_CHANNEL		RMT_CHANNEL_7     /*!< RMT channel for menu background */

#define LEDC_WHITE_LEVEL_TIMER      LEDC_TIMER_0
#define LEDC_WHITE_LEVEL_MODE       LEDC_HIGH_SPEED_MODE
#define LEDC_WHITE_LEVEL_GPIO       (GPIO_NUM_13)
#define LEDC_WHITE_LEVEL_CHANNEL    LEDC_CHANNEL_0
#define WHITE_LEVEL_STEP			248				  // About 0.1V steps

#define HOME_TIME_UNTIL_FIRMWARE_UPDATE 10000

// Change these if using with NTSC
#define TIMING_RETICULE_WIDTH 75.0f // Generates a circle in PAL but might need adjusting for NTSC (In 80ths of a microsecond)
#define TIMING_BACK_PORCH 8*80		// In 80ths of a microsecond	(Should be about 6*80)
#define TIMING_LINE_DURATION  8*465 // In 80ths of a microsecond  (Should be about 52*80 but need to clip when off edge)
#define TIMING_BLANKED_LINES 24		// Should be about 16?
#define TIMING_VISIBLE_LINES 250	// Should be 288
#define TIMING_VSYNC_THRESHOLD (40*16) // If sync is longer than this then doing a vertical sync
#define TEXT_START_LINE 105
#define TEXT_END_LINE (TEXT_START_LINE + 80)
#define LOGO_START_LINE (TIMING_BLANKED_LINES + 25)
#define LOGO_END_LINE (LOGO_START_LINE + 200)
#define MENU_START_MARGIN 100		// In 80th of microsecond
#define NUM_TEXT_SUBLINES 20		// Vertical resolution of font
#define NUM_TEXT_ROWS 10			// Num rows of text
#define NUM_TEXT_COLUMNS 20			// Num characters across screen
#define NUM_TEXT_BORDER_LINES 2		// Blank lines between lines of text
#define MENU_START_LINE (TIMING_BLANKED_LINES + 25)
#define MENU_END_LINE (MENU_START_LINE + NUM_TEXT_ROWS * (NUM_TEXT_SUBLINES + NUM_TEXT_BORDER_LINES))
#define FONT_WIDTH 160				// In 80th of microsecond
#define MENU_BORDER 40				// In 80th of microsecond

#define PERSISTANT_POWER_ON_VALUE		0xCDC00000ull
#define PERSISTANT_FIRMWARE_UPDATE_MODE	0xCDC10000ull
#define PERSISTANT_FIRMWARE_DONE_UPDATE	0xCDC20000ull

#define ARRAY_NUM(x) (sizeof(x)/sizeof(x[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

enum EUIState
{
	kUIState_Playing,
	kUIState_InMenu,
	kUIState_CalibrationMode,
	kUIState_FirmwareUpdate
};

enum MenuControl
{
	kMenu_None,
	kMenu_Up,
	kMenu_Down,
	kMenu_Right,
	kMenu_Left,
	kMenu_Select
};

EUIState UIState = kUIState_Playing;
static int CursorSize = 3;
static int DelayDecimal = 0;
static int WhiteLevelDecimal = 13;
static int IOType = 0;
static int CursorBrightness = 3;
static int SelectedRow = 2;
static bool LogoMode = true;
static bool TextMode = true;
static uint32_t *ImageData = &ImagePress12[0][0];
static int LogoTime = 4000;
static bool ShowPointer = true;
static int Coop = 0;
static int CurrentLine = 0;
static int CurrentTextLine = 0;
static int CurrentTextSubLine = 0;
static int PlayerMask = 0; // Set to 1 for two player
static int ReticuleStartLineNum[2] = { 1000,1000 };
static int ReticuleXPosition[2] = { 320,320 };
static int ReticuleSizeLookup[2][14];
static int CalibrationDelay = 0;
static int LastActivePlayer = 0;
static int WhiteLevel = 3225;	// Should produce test voltage of 1.3V (good for composite video)
static unsigned char TextBuffer[NUM_TEXT_ROWS][NUM_TEXT_COLUMNS];

bool MenuInput(MenuControl Input, class PlayerInput *MenuPlayer);
void InitializeFirmwareUpdateScreen();
void SetMenuState();

void SetPersistantStorage(uint64_t PersistantValue)
{
	timer_set_counter_value(TIMER_GROUP_1, TIMER_0, PersistantValue);
}

uint64_t GetPersistantStorage()
{
	uint64_t TimerValue = 0;
	timer_get_counter_value(TIMER_GROUP_1, TIMER_0, &TimerValue);
	return TimerValue;
}

void InitPersistantStorage()
{
	// Timers aren't re-initialised by esp_restart()
	// Used to restart into firmware update mode
	timer_pause(TIMER_GROUP_1, TIMER_0);
	if (rtc_get_reset_reason(0) == POWERON_RESET)
	{
		SetPersistantStorage(PERSISTANT_POWER_ON_VALUE);
	}
}

class PlayerInput
{
private:
	struct Vector2D
	{
		Vector2D()
		{
			X = Y = 0.0f;
		}
		Vector2D(float InX, float InY)
		{
			X = InX;
			Y = InY;
		}
		Vector2D operator-(const Vector2D &RHS) const
		{
			return Vector2D(X - RHS.X, Y - RHS.Y);
		}
		Vector2D operator+(const Vector2D &RHS) const
		{
			return Vector2D(X + RHS.X, Y + RHS.Y);
		}
		Vector2D operator*(const float &RHS) const
		{
			return Vector2D(X * RHS, Y * RHS);
		}
		float X;
		float Y;
	};

public:
	PlayerInput(int PlayerNum)
	{
		Wiimote = GWiimoteManager.CreateNewWiimote();
		FrameNumber = 0;
		OldButtons = 0;
		PlayerIdx = PlayerNum;
		CalibrationPhase = 4;
		DoneCalibration = false;
		SpotX = ~0;
		SpotY = ~0;
	}

	void Tick()
	{
		const WiimoteData *Data = Wiimote->GetData();
		if (Data->FrameNumber != FrameNumber)
		{
			FrameNumber = Data->FrameNumber;

			if (UIState == kUIState_CalibrationMode && CalibrationPhase < 4)
			{
				if (ButtonClicked(Data->Buttons, (WiimoteData::kButton_B | WiimoteData::kButton_A)))
				{
					if (Data->IRSpot[0].X != 0x3FF || Data->IRSpot[0].Y != 0x3FF)
					{
						CalibrationData[CalibrationPhase].X = (float)Data->IRSpot[0].X;
						CalibrationData[CalibrationPhase].Y = (float)Data->IRSpot[0].Y;
						CalibrationPhase++;
						DoneCalibration = (CalibrationPhase == 4);
						if (DoneCalibration)
						{
							UIState = kUIState_Playing;
						}
					}
				}
				else if (ButtonClicked(Data->Buttons, WiimoteData::kButton_Home))
				{
					ResetCalibration();
				}
			}

			if (UIState == kUIState_CalibrationMode && CalibrationPhase < 4)
			{
				ImageData = &ImageAim[0][0];
				TextMode = true;
				switch (CalibrationPhase)
				{
					case 0:
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES;
						break;
					case 1:
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + TIMING_LINE_DURATION;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES;
						break;
					case 2:
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + TIMING_VISIBLE_LINES;
						break;
					case 3:
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + TIMING_LINE_DURATION;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + TIMING_VISIBLE_LINES;
						break;

				}
			}
			else
			{
				if (DoneCalibration)
				{
					Vector2D Spot = Vector2D((float)Data->IRSpot[0].X, (float)Data->IRSpot[0].Y);
					if (Within(Spot))
					{
						Spot = RemapVector(Spot);
						Spot = Spot * 1023.0f;
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + (TIMING_LINE_DURATION*(int)Spot.X) / 1024;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + (TIMING_VISIBLE_LINES*(int)Spot.Y) / 1024;
						SpotX = (uint16_t)Spot.X;
						SpotY = (uint16_t)Spot.Y;
					}
					else
					{
						ReticuleStartLineNum[PlayerIdx] = 1000; // Don't draw
						SpotX = ~0;
						SpotY = ~0;
					}
				}
				else
				{
					if (Data->IRSpot[0].X != 0x3FF || Data->IRSpot[0].Y != 0x3FF)
					{
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + (TIMING_LINE_DURATION*(1023 - Data->IRSpot[0].X)) / 1024;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + (TIMING_VISIBLE_LINES*(Data->IRSpot[0].Y + Data->IRSpot[0].Y / 3)) / 1024;
						SpotX = Data->IRSpot[0].X;
						SpotY = Data->IRSpot[0].Y;
					}
					else
					{
						ReticuleStartLineNum[PlayerIdx] = 1000; // Don't draw
						SpotX = ~0;
						SpotY = ~0;
					}
				}
			}
			
			if (PlayerIdx == 1)
			{
				PlayerMask = 1; // If second Wiimote connected enable two player mode
			}

			if (TextMode)
			{
				if (ImageData == &ImagePress12[0][0])
				{
					TextMode = false;	// Turn off sync message once connected
				}
				else if (UIState != kUIState_CalibrationMode)
				{
					TextMode = false;	// Turn off calibration message
				}
			}

			OldButtons = Data->Buttons;
		}
	}

	bool ButtonClicked(int ButtonFlags, int ButtonSelect)
	{
		return ((ButtonFlags & ButtonSelect) && !(OldButtons & ButtonSelect));
	}

	bool ButtonWasPressed(int ButtonSelect)
	{
		return (OldButtons & ButtonSelect) != 0;
	}
	
	uint16_t GetSpotX()
	{
		return SpotX;
	}
	
	uint16_t GetSpotY()
	{
		return SpotY;
	}

	uint16_t GetButtons()
	{
		return OldButtons;
	}

	void ResetCalibration()
	{
		CalibrationPhase = 4;
		DoneCalibration = false;
		UIState = kUIState_Playing;
	}
		
	void StartCalibration()
	{
		CalibrationPhase = 0;
		DoneCalibration = false;
		UIState = kUIState_CalibrationMode;
	}

	bool IsConnected()
	{
		return FrameNumber != 0;
	}

private:
	float Cross(const Vector2D &LHS, const Vector2D &RHS) const
	{
		return LHS.X*RHS.Y - LHS.Y*RHS.X;
	}

	bool Within(const Vector2D &Pos) const
	{
		if (Cross(CalibrationData[1] - CalibrationData[0], Pos - CalibrationData[0]) > 0.0f)
			return false;
		if (Cross(CalibrationData[3] - CalibrationData[1], Pos - CalibrationData[1]) > 0.0f)
			return false;
		if (Cross(CalibrationData[2] - CalibrationData[3], Pos - CalibrationData[3]) > 0.0f)
			return false;
		if (Cross(CalibrationData[0] - CalibrationData[2], Pos - CalibrationData[2]) > 0.0f)
			return false;
		return true;
	}

	float Remap(const Vector2D &Pos, int a, int b, int c, int d)
	{
		Vector2D Cal[4];
		for (int i = 0; i < 4; i++)
		{
			Cal[i] = CalibrationData[i]-Pos;
		}
		float Qa = Cross(Cal[b]-Cal[d], Cal[c]-Cal[a]);
		float Qb = 2.0f*Cross(Cal[b], Cal[a]);
		Qb -= Cross(Cal[b], Cal[c]);
		Qb -= Cross(Cal[d], Cal[a]);
		float Qc = Cross(Cal[a], Cal[b]);
		if (Qa == 0.0f)
			return -Qc / Qb;
		float Inner = Qb*Qb - 4.0f*Qa*Qc;
		if (Inner < 0.0f)
			return -1.0f;
		float Root = sqrtf(Inner);
		float Result = (-Qb + Root) / (2.0f*Qa);
		if (Result<0.0f || Result>1.0f)
			Result = (-Qb - Root) / (2.0f*Qa);
		return Result;
	}

	Vector2D RemapVector(const Vector2D &Input)
	{
		Vector2D Result(Remap(Input,0,2,1,3), Remap(Input,0,1,2,3));
		Result.X = (Result.X < 0.0f) ? 0.0f : (Result.X > 1.0f) ? 1.0f : Result.X;
		Result.Y = (Result.Y < 0.0f) ? 0.0f : (Result.Y > 1.0f) ? 1.0f : Result.Y;
		return Result;
	}

private:
	IWiimote *Wiimote;
	int FrameNumber;
	int OldButtons;
	int PlayerIdx;
	int CalibrationPhase;
	Vector2D CalibrationData[4];
	bool DoneCalibration;
	uint16_t SpotX;
	uint16_t SpotY;
};

void WiimoteTask(void *pvParameters)
{
	bool WasPlayer1Button = false;
	bool WasPlayer2Button = false;
	bool WasHomeButton = false;
	int HomeButtonTimer = 0;
	printf("WiimoteTask running on core %d\n", xPortGetCoreID());
	GWiimoteManager.Init();
	PlayerInput Player1(0);
	PlayerInput Player2(1);
	while (true)
	{
		GWiimoteManager.Tick();
		Player1.Tick();
		Player2.Tick();
			
		bool bHomePressed = Player1.ButtonWasPressed(WiimoteData::kButton_Home) || Player2.ButtonWasPressed(WiimoteData::kButton_Home);
		if (bHomePressed && !WasHomeButton)
		{
			if (UIState == kUIState_InMenu)
				UIState = kUIState_Playing;
			else if (UIState == kUIState_Playing)
				UIState = kUIState_InMenu;
		}
		WasHomeButton = bHomePressed;

		if (UIState == kUIState_InMenu)
		{
			MenuControl CurrentMenuControl = kMenu_None;

			PlayerInput *MenuPlayerInput = nullptr;
			for (int Player = 0; Player < 2; Player++)
			{
				PlayerInput &Input = (Player == 0) ? Player1 : Player2;

				if (Input.ButtonWasPressed(WiimoteData::kButton_Down))
					CurrentMenuControl = kMenu_Down;
				else if (Input.ButtonWasPressed(WiimoteData::kButton_Up))
					CurrentMenuControl = kMenu_Up;
				else if (Input.ButtonWasPressed(WiimoteData::kButton_Left))
					CurrentMenuControl = kMenu_Left;
				else if (Input.ButtonWasPressed(WiimoteData::kButton_Right))
					CurrentMenuControl = kMenu_Right;
				else if (Input.ButtonWasPressed(WiimoteData::kButton_A | WiimoteData::kButton_B))
					CurrentMenuControl = kMenu_Select;

				if (CurrentMenuControl != kMenu_None)
				{
					MenuPlayerInput = &Input;
					break;
				}
			}

			if (MenuInput(CurrentMenuControl, MenuPlayerInput))
			{
				SetMenuState();
			}
		}

		bool Player1AButton = Player1.ButtonWasPressed(WiimoteData::kButton_A);
		bool Player1BButton = Player1.ButtonWasPressed(WiimoteData::kButton_B);
		bool Player2AButton = Player2.ButtonWasPressed(WiimoteData::kButton_A);
		bool Player2BButton = Player2.ButtonWasPressed(WiimoteData::kButton_B);
		bool Player1Buttons = Player1AButton || Player1BButton;	
		bool Player2Buttons = Player2AButton || Player2BButton;	
		
		if (Coop)
		{	
			if (Player1Buttons && !WasPlayer1Button)
				LastActivePlayer = 0;
			else if (Player2Buttons && !WasPlayer2Button)
				LastActivePlayer = 1;

			Player1AButton |= Player2AButton;
			Player2AButton |= Player1AButton;
			
			Player1BButton |= Player2BButton;
			Player2BButton |= Player1BButton;
		}
		
		WasPlayer1Button = Player1Buttons;
		WasPlayer2Button = Player2Buttons;

		if (IOType != 4)
		{
			bool bInvert = ((IOType & 2) != 0);
			gpio_matrix_out(OUT_PLAYER1_TRIGGER1_PULLED, SIG_GPIO_OUT_IDX, bInvert, false);
			gpio_matrix_out(OUT_PLAYER1_TRIGGER2_PULLED, SIG_GPIO_OUT_IDX, bInvert, false);
			gpio_matrix_out(OUT_PLAYER2_TRIGGER1_PULLED, SIG_GPIO_OUT_IDX, bInvert, false);
			gpio_matrix_out(OUT_PLAYER2_TRIGGER2_PULLED, SIG_GPIO_OUT_IDX, bInvert, false);

			if (IOType & 1)
			{
				gpio_set_level(OUT_PLAYER1_TRIGGER1_PULLED, Player1BButton);
				gpio_set_level(OUT_PLAYER1_TRIGGER2_PULLED, Player1AButton);
				gpio_set_level(OUT_PLAYER2_TRIGGER1_PULLED, Player2BButton);
				gpio_set_level(OUT_PLAYER2_TRIGGER2_PULLED, Player2AButton);
			}
			else
			{
				gpio_set_level(OUT_PLAYER1_TRIGGER1_PULLED, Player1AButton);
				gpio_set_level(OUT_PLAYER1_TRIGGER2_PULLED, Player1BButton);
				gpio_set_level(OUT_PLAYER2_TRIGGER1_PULLED, Player2AButton);
				gpio_set_level(OUT_PLAYER2_TRIGGER2_PULLED, Player2BButton);
			}
		}
		else
		{
			if (UART1.status.txfifo_cnt == 0) // UART FIFO is zero
			{
				uint8_t ToTransmit[16];
				for (int i = 0; i < 2; i++)
				{
					PlayerInput *Player = i ? &Player2 : &Player1;
					uint16_t SpotX = Player->GetSpotX();
					uint16_t SpotY = Player->GetSpotY();
					uint16_t Buttons = Player->GetButtons();
					ToTransmit[8 * i + 0] = 0x80;
					ToTransmit[8 * i + 1] = i;
					ToTransmit[8 * i + 2] = (SpotX >> 7) & 0x7F;
					ToTransmit[8 * i + 3] = (SpotX & 0x7F);
					ToTransmit[8 * i + 4] = (SpotY >> 7) & 0x7F;
					ToTransmit[8 * i + 5] = (SpotY & 0x7F);
					ToTransmit[8 * i + 6] = (Buttons >> 7) & 0x7F;
					ToTransmit[8 * i + 7] = (Buttons & 0x7F);
				}

				gpio_matrix_out(OUT_PLAYER1_TRIGGER1_PULLED, U1TXD_OUT_IDX, false, false);
				gpio_matrix_out(OUT_PLAYER1_TRIGGER2_PULLED, U1TXD_OUT_IDX, false, false);
				gpio_matrix_out(OUT_PLAYER2_TRIGGER1_PULLED, U1TXD_OUT_IDX, false, false);
				gpio_matrix_out(OUT_PLAYER2_TRIGGER2_PULLED, U1TXD_OUT_IDX, false, false);

				uart_tx_chars(UART_NUM_1, (char*)ToTransmit, sizeof(ToTransmit));
			}
		}

		if (LogoTime > 0)
		{
			LogoTime--;
		}
		LogoMode = (LogoTime > 0);

		if (!LogoMode) // LEDs both on at start up
		{
			gpio_set_level(OUT_FRONT_PANEL_LED1, Player1.IsConnected() ? 1 : 0);
			gpio_set_level(OUT_FRONT_PANEL_LED2, Player2.IsConnected() ? 1 : 0);
		}

		if (!bHomePressed)
		{
			HomeButtonTimer = 0;
		}
		else
		{
			HomeButtonTimer++;
			if (HomeButtonTimer > HOME_TIME_UNTIL_FIRMWARE_UPDATE)
			{
				UIState = kUIState_FirmwareUpdate;
				InitializeFirmwareUpdateScreen();
			}
		}
		if ((UIState == kUIState_FirmwareUpdate && (Player1.ButtonWasPressed(WiimoteData::kButton_A) || Player2.ButtonWasPressed(WiimoteData::kButton_A))) || !gpio_get_level(IN_UPLOAD_BUTTON))
		{
			printf("Restarting\n");
			GWiimoteManager.DeInit();
			SetPersistantStorage(PERSISTANT_FIRMWARE_UPDATE_MODE);
			esp_restart();
		}

		vTaskDelay(1);
	}
}

static void RMTPeripheralInit()
{
	// RMT (Remote Control Peripheral)
	// This generates a spot at a point in a screen line (basically the X coordinate)
	// The generated pulse is split 4 ways
	// - OUT_PLAYER1_LED - Generates a pulse that gets ANDed with the white level detector (in HW) and triggers the 555 to flash the output LED
	// - OUT_PLAYER2_LED - The same but for the second player's LED when playing in 2-player mode
	// - OUT_SCREEN_DIM - The pulse that dims the screen
	// - OUT_SCREEN_DIM_INV - Inverse of the dim signal to simplify some HW

	rmt_config_t RMTTxConfig;
	RMTTxConfig.channel = RMT_SCREEN_DIM_CHANNEL;
	RMTTxConfig.gpio_num = OUT_SCREEN_DIM;
	RMTTxConfig.mem_block_num = 1;
	RMTTxConfig.clk_div = 1;
	RMTTxConfig.tx_config.loop_en = false;
	RMTTxConfig.tx_config.carrier_duty_percent = 50;
	RMTTxConfig.tx_config.carrier_freq_hz = 38000;
	RMTTxConfig.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
	RMTTxConfig.tx_config.carrier_en = false;
	RMTTxConfig.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
	RMTTxConfig.tx_config.idle_output_en = true;
	RMTTxConfig.rmt_mode = RMT_MODE_TX;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = (rmt_channel_t)(RMT_SCREEN_DIM_CHANNEL + 1);
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = RMT_BACKGROUND_CHANNEL;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = RMT_TRIGGER_CHANNEL;
	RMTTxConfig.gpio_num = OUT_PLAYER1_LED;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = (rmt_channel_t)(RMT_TRIGGER_CHANNEL + 1);
	RMTTxConfig.gpio_num = OUT_PLAYER2_LED;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = RMT_DELAY_TRIGGER_CHANNEL;
	RMTTxConfig.gpio_num = OUT_PLAYER1_LED_DELAYED;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);
	RMTTxConfig.channel = (rmt_channel_t)(RMT_DELAY_TRIGGER_CHANNEL + 1);
	RMTTxConfig.gpio_num = OUT_PLAYER2_LED_DELAYED;
	rmt_config(&RMTTxConfig);
	rmt_driver_install(RMTTxConfig.channel, 0, 0);

	// Screen dimmer
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_SCREEN_DIM], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_SCREEN_DIM, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_SCREEN_DIM, 1); // If we turn it off keep high
	gpio_matrix_out(OUT_SCREEN_DIM, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, true, false);
	
	// Screen dimmer (second channel)
	rtc_gpio_deinit(OUT_SCREEN_DIMER);
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_SCREEN_DIMER], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_SCREEN_DIMER, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_SCREEN_DIMER, 1); // If we turn it off keep high
	gpio_matrix_out(OUT_SCREEN_DIMER, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, true, false);

	// Screen dimmer (inverted)
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_SCREEN_DIM_INV], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_SCREEN_DIM_INV, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_SCREEN_DIM_INV, 1); // If we turn it off keep high (will be inverted)
	gpio_matrix_out(OUT_SCREEN_DIM_INV, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, false, false);

	// Invert triggers (they are active high)
	gpio_matrix_out(OUT_PLAYER1_LED, RMT_SIG_OUT0_IDX + RMT_TRIGGER_CHANNEL + 0, true, false);
	gpio_matrix_out(OUT_PLAYER2_LED, RMT_SIG_OUT0_IDX + RMT_TRIGGER_CHANNEL + 1, true, false);
	gpio_matrix_out(OUT_PLAYER1_LED_DELAYED, RMT_SIG_OUT0_IDX + RMT_DELAY_TRIGGER_CHANNEL + 0, true, false);
	gpio_matrix_out(OUT_PLAYER2_LED_DELAYED, RMT_SIG_OUT0_IDX + RMT_DELAY_TRIGGER_CHANNEL + 1, true, false);
}

static void PWMPeripherialInit()
{
	ledc_timer_config_t WhiteLevelPWMTimer;
	WhiteLevelPWMTimer.bit_num = LEDC_TIMER_13_BIT;
	WhiteLevelPWMTimer.freq_hz = 9000;
	WhiteLevelPWMTimer.speed_mode = LEDC_WHITE_LEVEL_MODE;
	WhiteLevelPWMTimer.timer_num = LEDC_WHITE_LEVEL_TIMER;
	ledc_timer_config(&WhiteLevelPWMTimer);

	ledc_channel_config_t WhiteLevelPWMConfig;
	WhiteLevelPWMConfig.channel    = LEDC_WHITE_LEVEL_CHANNEL;
	WhiteLevelPWMConfig.duty       = 0;
	WhiteLevelPWMConfig.gpio_num   = LEDC_WHITE_LEVEL_GPIO;
	WhiteLevelPWMConfig.speed_mode = LEDC_WHITE_LEVEL_MODE;
	WhiteLevelPWMConfig.timer_sel  = LEDC_WHITE_LEVEL_TIMER;
	ledc_channel_config(&WhiteLevelPWMConfig);

	ledc_set_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL, WhiteLevel);
	ledc_update_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL);
}

#define ActivateRMTOnSyncFallingEdgeAsmInner(Extra, Ident)\
	asm volatile\
		(\
			"\
			memw;\
SPIN" #Ident ":   l32i.n %0, %1, 0;\
			bbsi %0, 21, SPIN" #Ident ";\
			l32i.n %0, %2, 0;\
			or %0, %0, %3;\
			"\
			Extra \
			"\
			memw;"\
			: "+r"(Temp)\
			: "r"(GPIOIn), "r"(RMTConfig1), "r"(TXStart), "r"(RMTP1Config1), "r"(RMTP2Config1), "r"(RMTP1DConfig1), "r"(RMTP2DConfig1), "r"(RMTBGConfig1)\
			:\
		)

#define ActivateRMTOnSyncFallingEdgeAsmLine(Extra, Ident) ActivateRMTOnSyncFallingEdgeAsmInner(Extra, Ident)
#define ActivateRMTOnSyncFallingEdgeAsm(Extra) ActivateRMTOnSyncFallingEdgeAsmLine(Extra, __LINE__)

void IRAM_ATTR ActivateRMTOnSyncFallingEdge(uint32_t Bank, int Active)
{
	// Tight loop that sits spinning until GPIO21 (see assembly) aka IN_COMPOSITE_SYNC falls low and then starts RMT peripherals

	volatile uint32_t *RMTConfig1 = &RMT.conf_ch[RMT_SCREEN_DIM_CHANNEL + Bank].conf1.val;
	volatile uint32_t *RMTP1Config1 = &RMT.conf_ch[RMT_TRIGGER_CHANNEL].conf1.val;
	volatile uint32_t *RMTP2Config1 = &RMT.conf_ch[RMT_TRIGGER_CHANNEL + 1].conf1.val;
	volatile uint32_t *RMTP1DConfig1 = &RMT.conf_ch[RMT_DELAY_TRIGGER_CHANNEL].conf1.val;
	volatile uint32_t *RMTP2DConfig1 = &RMT.conf_ch[RMT_DELAY_TRIGGER_CHANNEL + 1].conf1.val;
	volatile uint32_t *RMTBGConfig1 = &RMT.conf_ch[RMT_BACKGROUND_CHANNEL].conf1.val;
	volatile uint32_t *GPIOIn = &GPIO.in;
	uint32_t Temp = 0, TXStart = 1 | 8; // Start and reset
	switch (Active)
	{
		case 1: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0;"); break;
		case 2: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %4, 0; s32i.n %0, %6, 0;"); break;
		case 3: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %4, 0; s32i.n %0, %6, 0;"); break;
		case 4: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %5, 0; s32i.n %0, %7, 0;"); break;
		case 5: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %5, 0; s32i.n %0, %7, 0;"); break;
		case 6: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %4, 0; s32i.n %0, %5, 0; s32i.n %0, %6, 0; s32i.n %0, %7, 0;"); break;
		case 7: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %4, 0; s32i.n %0, %5, 0; s32i.n %0, %6, 0; s32i.n %0, %7, 0;"); break;
		case 8: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %8, 0;"); break;
		case 9: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %8, 0;"); break;
		case 10: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %4, 0; s32i.n %0, %6, 0; s32i.n %0, %8, 0;"); break;
		case 11: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %4, 0; s32i.n %0, %6, 0; s32i.n %0, %8, 0;"); break;
		case 12: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %5, 0; s32i.n %0, %7, 0; s32i.n %0, %8, 0;"); break;
		case 13: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %5, 0; s32i.n %0, %7, 0; s32i.n %0, %8, 0;"); break;
		case 14: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %4, 0; s32i.n %0, %5, 0; s32i.n %0, %6, 0; s32i.n %0, %7, 0; s32i.n %0, %8, 0;"); break;
		case 15: ActivateRMTOnSyncFallingEdgeAsm("s32i.n %0, %2, 0; s32i.n %0, %4, 0; s32i.n %0, %5, 0; s32i.n %0, %6, 0; s32i.n %0, %7, 0; s32i.n %0, %8, 0;"); break;
	}
}

int IRAM_ATTR SetupLine(uint32_t Bank, const int *StartingLine)
{
	int Active = 0;

	rmt_item32_t EndTerminator;
	EndTerminator.level0 = 1;
	EndTerminator.duration0 = 0;
	EndTerminator.level1 = 1;
	EndTerminator.duration1 = 0;
	
	if ((UIState == kUIState_InMenu || UIState == kUIState_FirmwareUpdate) && CurrentLine >= MENU_START_LINE && CurrentLine < MENU_END_LINE)
	{
		if (CurrentTextSubLine < NUM_TEXT_SUBLINES)
		{
			int CurData=0;
			rmt_item32_t StartingDelay;
			StartingDelay.level0 = 1;
			StartingDelay.duration0 = TIMING_BACK_PORCH;
			StartingDelay.level1 = 1;
			StartingDelay.duration1 = MENU_START_MARGIN;
			const unsigned char *Message = TextBuffer[CurrentTextLine];
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = StartingDelay.val;
			for (int Column = 0; Column < NUM_TEXT_COLUMNS; Column++)
			{
				int Remapped = Message[Column];
				const uint32_t *FontData=Font[Remapped][CurrentTextSubLine];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[0];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[1];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[2];
			}
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = EndTerminator.val;
			Active = 1;
		}
		Active |= 8; // Background menu
		CurrentTextSubLine++;
		if (CurrentTextSubLine >= NUM_TEXT_SUBLINES + NUM_TEXT_BORDER_LINES)
		{
			CurrentTextSubLine = 0;
			CurrentTextLine++;
		}
	}
	else if (UIState == kUIState_CalibrationMode || ShowPointer)
	{
		bool bPlayerVisibleOnLine[2];
		bPlayerVisibleOnLine[0] = CurrentLine >= StartingLine[0] && CurrentLine < StartingLine[0] + ARRAY_NUM(ReticuleSizeLookup[0]);
		bPlayerVisibleOnLine[1] = CurrentLine >= StartingLine[1] && CurrentLine < StartingLine[1] + ARRAY_NUM(ReticuleSizeLookup[0]);
		for (int Player = 0; Player < 2; Player++)
		{
			if (bPlayerVisibleOnLine[Player])
			{
				if (ReticuleSizeLookup[Player][CurrentLine - StartingLine[Player]] < 4) // Pulses less than 4 cause issues
				{
					bPlayerVisibleOnLine[Player] = false;
				}
			}
		}
		if (LogoMode && CurrentLine >= LOGO_START_LINE && CurrentLine < LOGO_END_LINE)
		{
			int LineIdx = CurrentLine - LOGO_START_LINE;
			for (int i = 0; i < 8; i++)
			{
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[i].val = ImageLogo[LineIdx][i];
			}
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[8].val = EndTerminator.val;
			Active = 1;
		}
		else if (TextMode && CurrentLine >= TEXT_START_LINE && CurrentLine < TEXT_END_LINE)
		{
			int LineIdx = CurrentLine - TEXT_START_LINE;
			for (int i = 0; i < 8; i++)
			{
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[i].val = ImageData[8*LineIdx + i];
			}
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[8].val = EndTerminator.val;
			Active = 1;
		}
		else if (bPlayerVisibleOnLine[0] && bPlayerVisibleOnLine[1])
		{
			int XStart[2];
			int XEnd[2];
			XStart[0] = ReticuleXPosition[0] - ReticuleSizeLookup[0][CurrentLine - StartingLine[0]];
			XStart[1] = ReticuleXPosition[1] - ReticuleSizeLookup[1][CurrentLine - StartingLine[1]];
			XEnd[0] = XStart[0] + 2 * ReticuleSizeLookup[0][CurrentLine - StartingLine[0]];
			XEnd[1] = XStart[1] + 2 * ReticuleSizeLookup[1][CurrentLine - StartingLine[1]];
			int MinPlayer = (XStart[0] < XStart[1]) ? 0 : 1;
			rmt_item32_t HorizontalPulse;
			HorizontalPulse.level0 = 1;
			HorizontalPulse.level1 = 0;
			if (XStart[1-MinPlayer] <= XEnd[MinPlayer]) // Overlapping
			{
				HorizontalPulse.duration0 = XStart[MinPlayer];
				HorizontalPulse.duration1 = MAX(XEnd[1 - MinPlayer], XEnd[MinPlayer]) - XStart[MinPlayer];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[0].val = HorizontalPulse.val;
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[1].val = EndTerminator.val;
			}
			else // No overlap
			{
				HorizontalPulse.duration0 = XStart[MinPlayer];
				HorizontalPulse.duration1 = XEnd[MinPlayer] - XStart[MinPlayer];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[0].val = HorizontalPulse.val;
				HorizontalPulse.duration0 = XStart[1 - MinPlayer] - XEnd[MinPlayer];
				HorizontalPulse.duration1 = XEnd[1 - MinPlayer] - XStart[1 - MinPlayer];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[1].val = HorizontalPulse.val;
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[2].val = EndTerminator.val;
			}
			Active = 1;
		}
		else if (bPlayerVisibleOnLine[0] || bPlayerVisibleOnLine[1])
		{
			int CurrentPlayer = bPlayerVisibleOnLine[0] ? 0 : 1;
			rmt_item32_t HorizontalPulse;
			HorizontalPulse.level0 = 1;
			HorizontalPulse.duration0 = ReticuleXPosition[CurrentPlayer] - ReticuleSizeLookup[CurrentPlayer][CurrentLine - StartingLine[CurrentPlayer]];
			HorizontalPulse.level1 = 0;
			HorizontalPulse.duration1 = 2 * ReticuleSizeLookup[CurrentPlayer][CurrentLine - StartingLine[CurrentPlayer]];
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[0].val = HorizontalPulse.val;
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[1].val = EndTerminator.val;
			Active = 1;
		}
	}

	for (int Player=0; Player<2; Player++)
	{
		int SourcePlayer = Player;
		if (Coop)
		{
			SourcePlayer = LastActivePlayer;
		}
		if (CurrentLine == StartingLine[SourcePlayer] + ARRAY_NUM(ReticuleSizeLookup[0])/2)
		{
			int Channel = RMT_TRIGGER_CHANNEL + Player;
			int DelayChannel = RMT_DELAY_TRIGGER_CHANNEL + Player;
			int PulseWidth = 20; // 1/4th microsecond
			rmt_item32_t HorizontalPulse;
			HorizontalPulse.level0 = 1;
			HorizontalPulse.duration0 = ReticuleXPosition[SourcePlayer] - PulseWidth/2;
			HorizontalPulse.level1 = 0;
			HorizontalPulse.duration1 = PulseWidth;
			RMTMEM.chan[Channel].data32[0].val = HorizontalPulse.val;
			RMTMEM.chan[Channel].data32[1].val = EndTerminator.val;
			HorizontalPulse.duration0 += CalibrationDelay;
			RMTMEM.chan[DelayChannel].data32[0].val = HorizontalPulse.val;
			RMTMEM.chan[DelayChannel].data32[1].val = EndTerminator.val;
			Active |= (2 << Player);
		}
	}

	return Active;
}

void IRAM_ATTR DoOutputSelection(uint32_t Bank, bool bInMenu)
{
	// Select between holding high or actually outputting

	if (bInMenu)
	{
		WRITE_PERI_REG(OUT_SCREEN_DIM_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | ((RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) << GPIO_FUNC0_OUT_SEL_S));
		WRITE_PERI_REG(OUT_SCREEN_DIMER_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | ((RMT_SIG_OUT0_IDX + RMT_BACKGROUND_CHANNEL) << GPIO_FUNC0_OUT_SEL_S));
		WRITE_PERI_REG(OUT_SCREEN_DIM_INV_SELECTION_REG, (RMT_SIG_OUT0_IDX + RMT_BACKGROUND_CHANNEL) << GPIO_FUNC0_OUT_SEL_S);
	}
	else
	{
		uint32_t HighChannel = (CursorBrightness&2) ? (RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) : SIG_GPIO_OUT_IDX;
		uint32_t LowChannel = (CursorBrightness&1) ? (RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) : SIG_GPIO_OUT_IDX;
		WRITE_PERI_REG(OUT_SCREEN_DIM_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (HighChannel << GPIO_FUNC0_OUT_SEL_S));
		WRITE_PERI_REG(OUT_SCREEN_DIMER_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (LowChannel << GPIO_FUNC0_OUT_SEL_S));
		WRITE_PERI_REG(OUT_SCREEN_DIM_INV_SELECTION_REG, (RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) << GPIO_FUNC0_OUT_SEL_S);
	}
}

void IRAM_ATTR CompositeSyncPositiveEdge(uint32_t &Bank, int &Active, const int *CachedStartingLines)
{
	if (Active != 0 && CurrentLine != 0)
	{
		ActivateRMTOnSyncFallingEdge(Bank, Active);
		DoOutputSelection(Bank, (Active&8) != 0);
	}
	CurrentLine++;
	Bank = 1 - Bank;
	Active = SetupLine(Bank, CachedStartingLines);
}

void IRAM_ATTR SpotGeneratorInnerLoop()
{
	timer_idx_t timer_idx = TIMER_1;
	uint32_t Bank = 0;
	int Active = 0;
	int CachedStartingLines[2];
	while (true)
	{
		TIMERG1.hw_timer[timer_idx].reload = 1;
		while ((GPIO.in & BIT(IN_COMPOSITE_SYNC)) != 0); // while sync is still happening
		TIMERG1.hw_timer[timer_idx].update = 1;
		// Don't really need the 64-bit time but only reading cnt_low seems to caused it to sometimes not update. Adding some nops also worked but not as reliably as this
		uint64_t Time = 2*(((uint64_t)TIMERG1.hw_timer[timer_idx].cnt_high<<32) | TIMERG1.hw_timer[timer_idx].cnt_low); // Timer's clk is half APB hence 2x.
		while ((GPIO.in & BIT(IN_COMPOSITE_SYNC)) == 0); // while not sync
		if (Time > TIMING_VSYNC_THRESHOLD)
		{
			CurrentLine = 0;
			CurrentTextLine = 0;
			CurrentTextSubLine = 0;
			// Cache starting lines as they will be changing on other thread
			// Otherwise if player moving cursor up we could miss triggering
			CachedStartingLines[0] = ReticuleStartLineNum[0];
			CachedStartingLines[1] = ReticuleStartLineNum[1];
		}
		else
		{
			CompositeSyncPositiveEdge(Bank, Active, CachedStartingLines);
		}
	}
}

void SetReticuleSize()
{
	float Scale = 1.0f;
	switch (CursorSize)
	{
		case 0: Scale = 1.00f; break; // Off (Will be shown during calibration)
		case 1: Scale = 0.25f; break; // Small
		case 2: Scale = 0.50f; break; // Medium
		case 3: Scale = 1.00f; break; // Large
	}
	int ReticuleNumLines = ARRAY_NUM(ReticuleSizeLookup[0]);
	float ReticuleHalfSize = Scale * ReticuleNumLines / 2.0f;
	float ReticuleMiddle = (ReticuleNumLines - 1) / 2.0f;
	for (int i = 0; i < ReticuleNumLines; i++)
	{
		float y = (i - ReticuleMiddle) / ReticuleHalfSize;
		if (y * y < 1.0f)
		{
			float x = sqrtf(1.0f - y*y);
			ReticuleSizeLookup[0][i] = Scale * TIMING_RETICULE_WIDTH * x;
			x = 1.0f - fabsf(y);
			ReticuleSizeLookup[1][i] = Scale * TIMING_RETICULE_WIDTH * x;
		}
		else
		{
			ReticuleSizeLookup[0][i] = 0;
			ReticuleSizeLookup[1][i] = 0;
		}
	}
}

void SetMenuState()
{
	ShowPointer = (CursorSize != 0);
	SetReticuleSize();
	CalibrationDelay = DelayDecimal * 8; // 80th of microsecond
	WhiteLevel = WhiteLevelDecimal * WHITE_LEVEL_STEP;
	if (WhiteLevel == 0)
	{
		gpio_set_direction(OUT_WHITE_OVERRIDE, GPIO_MODE_OUTPUT);
		gpio_set_level(OUT_WHITE_OVERRIDE, 1); // Force white level high
	}
	else
	{
		gpio_set_direction(OUT_WHITE_OVERRIDE, GPIO_MODE_INPUT);
		ledc_set_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL, WhiteLevel);
		ledc_update_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL);
	}
}

void InitSpotGenerator()
{
	gpio_config_t CSyncGPIOConfig;
	CSyncGPIOConfig.intr_type = GPIO_INTR_DISABLE;
	CSyncGPIOConfig.pin_bit_mask = BIT(IN_COMPOSITE_SYNC);
	CSyncGPIOConfig.mode = GPIO_MODE_INPUT;
	CSyncGPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
	CSyncGPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&CSyncGPIOConfig);
	
	PWMPeripherialInit();

	RMTPeripheralInit();
	rmt_item32_t RMTInitialValues[1];
	RMTInitialValues[0].level0 = 1;
	RMTInitialValues[0].duration0 = 1;
	RMTInitialValues[0].level1 = 1;
	RMTInitialValues[0].duration1 = 1;
	rmt_write_items(RMT_SCREEN_DIM_CHANNEL, RMTInitialValues, 1, false);	// Prime the RMT
	rmt_write_items(RMT_TRIGGER_CHANNEL, RMTInitialValues, 1, false);	// Prime the RMT
	rmt_write_items(RMT_DELAY_TRIGGER_CHANNEL, RMTInitialValues, 1, false);	// Prime the RMT
	rmt_write_items((rmt_channel_t)(RMT_SCREEN_DIM_CHANNEL + 1), RMTInitialValues, 1, false);	// Prime the RMT
	rmt_write_items((rmt_channel_t)(RMT_TRIGGER_CHANNEL + 1), RMTInitialValues, 1, false);	// Prime the RMT
	rmt_write_items((rmt_channel_t)(RMT_DELAY_TRIGGER_CHANNEL + 1), RMTInitialValues, 1, false);	// Prime the RMT
	
	rmt_item32_t RMTMenuBackground[2];
	RMTMenuBackground[0].level0 = 1;
	RMTMenuBackground[0].duration0 = TIMING_BACK_PORCH;
	RMTMenuBackground[0].level1 = 1;
	RMTMenuBackground[0].duration1 = MENU_START_MARGIN - MENU_BORDER;
	RMTMenuBackground[1].level0 = 0;
	RMTMenuBackground[1].duration0 = (NUM_TEXT_COLUMNS + 1)*FONT_WIDTH + 2*MENU_BORDER;
	RMTMenuBackground[1].level1 = 1;
	RMTMenuBackground[1].duration1 = 0;
	rmt_write_items(RMT_BACKGROUND_CHANNEL, RMTMenuBackground, 2, false);	// Prime the RMT

	timer_group_t timer_group = TIMER_GROUP_1;
	timer_idx_t timer_idx = TIMER_1;
	timer_config_t config;
	config.alarm_en = TIMER_ALARM_DIS;
	config.auto_reload = TIMER_AUTORELOAD_DIS;
	config.counter_dir = TIMER_COUNT_UP;
	config.divider = 2;
	config.intr_type = TIMER_INTR_LEVEL;
	config.counter_en = TIMER_START;
	timer_init(timer_group, timer_idx, &config);
	timer_set_counter_value(timer_group, timer_idx, 0ULL);
}

void SpotGeneratorTask(void *pvParameters)
{
	printf("SpotGeneratorTask starting on core %d\n", xPortGetCoreID());

	SetReticuleSize();

	InitSpotGenerator();

	vTaskEndScheduler(); // Disable FreeRTOS on this core as we don't need it anymore

	SpotGeneratorInnerLoop();
}

void InitializeMiscGPIO()
{
	gpio_config_t GPIOConfig;
	GPIOConfig.intr_type = GPIO_INTR_DISABLE;
	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER1_TRIGGER1_PULLED);
	GPIOConfig.mode = GPIO_MODE_OUTPUT;
	GPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
	GPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&GPIOConfig);
	
	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER1_TRIGGER2_PULLED);
	gpio_config(&GPIOConfig);

	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER2_TRIGGER1_PULLED);
	gpio_config(&GPIOConfig);
	
	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER2_TRIGGER2_PULLED);
	gpio_config(&GPIOConfig);
	
	GPIOConfig.pin_bit_mask = BIT(OUT_WHITE_OVERRIDE);
	GPIOConfig.mode = GPIO_MODE_INPUT;	// Let white level detect from signal
	gpio_config(&GPIOConfig);
	gpio_set_level(OUT_WHITE_OVERRIDE, 1); // Force white level high when used as output
	
	GPIOConfig.pin_bit_mask = 1ull<<OUT_FRONT_PANEL_LED1;
	GPIOConfig.mode = GPIO_MODE_OUTPUT;
	gpio_config(&GPIOConfig);
	gpio_set_level(OUT_FRONT_PANEL_LED1, 1);
	
	GPIOConfig.pin_bit_mask = BIT(OUT_FRONT_PANEL_LED2);
	gpio_config(&GPIOConfig);
	gpio_set_level(OUT_FRONT_PANEL_LED2, 1);
	
	gpio_config_t UploadButtonGPIOConfig;
	UploadButtonGPIOConfig.intr_type = GPIO_INTR_DISABLE;
	UploadButtonGPIOConfig.pin_bit_mask = BIT(IN_UPLOAD_BUTTON);
	UploadButtonGPIOConfig.mode = GPIO_MODE_INPUT;
	UploadButtonGPIOConfig.pull_up_en = GPIO_PULLUP_ENABLE;
	UploadButtonGPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&UploadButtonGPIOConfig);

	uart_config_t UARTConfig = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &UARTConfig);
    uart_driver_install(UART_NUM_1, UART_FIFO_LEN * 2, 0, 0, NULL, 0);
}

void ConvertText(const char *Text, int Row, int Column)
{
	while (*Text)
	{
		char Character = *(Text++);
		unsigned char Remapped = FontRemap[(unsigned char)Character];
		TextBuffer[Row][Column++] = Remapped;
	}
}

void DrawNumber(int Value, int Row, int Column)
{
	int Tens = Value / 10;
	int Ones = Value - Tens * 10;
	TextBuffer[Row][Column] = Tens;
	TextBuffer[Row][Column + 1] = FontRemap['.'];
	TextBuffer[Row][Column + 2] = Ones;
}

void UpdateMenu()
{
	int Tab = 14;
	switch (CursorSize)
	{
		case 0: ConvertText("OFF   ", 2, Tab); break;
		case 1: ConvertText("SMALL ", 2, Tab); break;
		case 2: ConvertText("MEDIUM", 2, Tab); break;
		case 3: ConvertText("LARGE ", 2, Tab); break;
	}
	if (Coop)
		ConvertText("CO OP ", 3, Tab);
	else
		ConvertText("VERSUS", 3, Tab);
	DrawNumber(DelayDecimal, 4, Tab);
	if (WhiteLevelDecimal)
	{
		DrawNumber(WhiteLevelDecimal, 5, Tab);
		TextBuffer[5][Tab + 3] = FontRemap['V'];
	}
	else
	{
		ConvertText("OFF ", 5, Tab);
	}
	switch (CursorBrightness)
	{
		case 1: ConvertText("DARK  ", 6, Tab); break;
		case 2: ConvertText("MEDIUM", 6, Tab); break;
		case 3: ConvertText("BRIGHT", 6, Tab); break;
	}
	switch (IOType)
	{
		case 0: ConvertText("A + B ", 7, Tab); break;
		case 1: ConvertText("B + A ", 7, Tab); break;
		case 2: ConvertText("INV AB", 7, Tab); break;
		case 3: ConvertText("INV BA", 7, Tab); break;
		case 4: ConvertText("SERIAL", 7, Tab); break;
	}
	for (int i=2; i<=9; i++)
	{
		TextBuffer[i][0] = FontRemap[(unsigned char)((i == SelectedRow) ? '+' : ' ')];
	}
}

bool AdjustRange(MenuControl Input, MenuControl Lower, MenuControl Raise, int &Value, int Min, int Max)
{
	if (Input == Lower && Value > Min)
		Value--;
	else if (Input == Raise && Value < Max)
		Value++;
	else
		return false;
	return true;
}

MenuControl AutoRepeat(MenuControl Input)
{
	static MenuControl Last = kMenu_None;
	static int Repeat = 0;
	static int RepeatTimer = 0;
	if (Input == kMenu_None || Last != Input)
	{
		Last = Input;
		RepeatTimer = 400;
		Repeat = RepeatTimer;
	}
	else
	{
		Repeat--;
		if (Repeat > 0)
		{
			Input = kMenu_None;
		}
		else
		{
			RepeatTimer = (RepeatTimer / 8) * 7;
			if (RepeatTimer < 30)
				RepeatTimer = 30;
			Repeat = RepeatTimer;
		}
	}
	return Input;
}

bool MenuInput(MenuControl Input, PlayerInput *MenuPlayer)
{
	Input = AutoRepeat(Input);
	if (Input != kMenu_None)
	{
		bool bDirty = AdjustRange(Input, kMenu_Up, kMenu_Down, SelectedRow, 2, 9);
		switch (SelectedRow)
		{
			case 2: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, CursorSize, 0, 3); break;
			case 3: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, Coop, 0, 1); break;
			case 4: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, DelayDecimal, 0, 99); break;
			case 5: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, WhiteLevelDecimal, 0, 33); break;
			case 6: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, CursorBrightness, 1, 3); break;
			case 7: bDirty |= AdjustRange(Input, kMenu_Left, kMenu_Right, IOType, 0, 4); break;
		}
		if (Input == kMenu_Select)
		{
			switch (SelectedRow)
			{
				case 8: bDirty |= true; MenuPlayer->StartCalibration(); break;
				case 9: bDirty |= true; MenuPlayer->ResetCalibration(); break;
			}
		}
		if (bDirty)
		{
			UpdateMenu();
		}
		return bDirty;
	}
	return false;
}

void InitializeMenu()
{
	ConvertText("   CONFIGURE MENU   ", 0, 0);
	ConvertText("                    ", 1, 0);
	ConvertText("+CURSOR SIZE: LARGE ", 2, 0);
	ConvertText(" 2 PLAYER:    VERSUS", 3, 0);
	ConvertText(" DELAY:       0.0us ", 4, 0);
	ConvertText(" WHITE LEVEL: 1.3V  ", 5, 0);
	ConvertText(" CURSOR COLOR:BRIGHT", 6, 0);
	ConvertText(" IO TYPE:     NORMAL", 7, 0);
	ConvertText(" START CALIBRATION  ", 8, 0);
	ConvertText(" RESET CALIBRATION  ", 9, 0);
	UpdateMenu();
	SetMenuState();
}

void InitializeFirmwareUpdateScreen()
{
	ConvertText("  FIRMWARE UPDATER  ", 0, 0);
	ConvertText("                    ", 1, 0);
	ConvertText(" PRESS A TO RESTART ", 2, 0);
	ConvertText(" THEN CONNECT TO... ", 3, 0);
	ConvertText("                    ", 4, 0);
	ConvertText("        SSID:       ", 5, 0);
	ConvertText("   LIGHTGUNVERTER   ", 6, 0);
	ConvertText("                    ", 7, 0);
	ConvertText("      WEB PAGE:     ", 8, 0);
	ConvertText("     192.168.4.1    ", 9, 0);
}

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id)
	{
		case SYSTEM_EVENT_AP_STACONNECTED:
			xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
			break;
		case SYSTEM_EVENT_AP_STADISCONNECTED:
			xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
			break;
		default:
			break;
	}
    return ESP_OK;
}

void WifiInitAccessPoint()
{
	wifi_event_group = xEventGroupCreate();

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_config_t WiFiConfig;
	strcpy((char*)WiFiConfig.ap.ssid, "LightGunVerter");
	WiFiConfig.ap.ssid_len = strlen("LightGunVerter");
	WiFiConfig.ap.password[0] = 0;
	WiFiConfig.ap.max_connection = 1;
	WiFiConfig.ap.authmode = WIFI_AUTH_OPEN;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &WiFiConfig));
	ESP_ERROR_CHECK(esp_wifi_start());
}

void WifiStartListening()
{
	static char WifiBuffer[2048];

	EventBits_t ConnectBits = 0;
	bool bFlash = false;
	while ((ConnectBits & WIFI_CONNECTED_BIT) == 0)
	{
		ConnectBits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, 250);
		gpio_set_level(OUT_FRONT_PANEL_LED1, bFlash?1:0);
		gpio_set_level(OUT_FRONT_PANEL_LED2, bFlash?0:1);
		bFlash = !bFlash;
	}
	printf("Something connected\n");

	int Sock = socket(PF_INET, SOCK_STREAM, 0);
	
	sockaddr_in SockAddrIn;	
	memset(&SockAddrIn, 0, sizeof(SockAddrIn));
	SockAddrIn.sin_family = AF_INET;
	SockAddrIn.sin_port = htons(80);
	SockAddrIn.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(Sock, (struct sockaddr *)&SockAddrIn, sizeof(SockAddrIn));
	
	listen(Sock, 5);
	
	printf("Listening\n");

	bool bUpdated = false;
	while (!bUpdated)
	{
		gpio_set_level(OUT_FRONT_PANEL_LED1, 0);
		gpio_set_level(OUT_FRONT_PANEL_LED2, 0);

		sockaddr_in ClientSockAddrIn;
		socklen_t ClientSockAddrLen = sizeof(ClientSockAddrIn);
		int ClientSock = accept(Sock, (sockaddr*)&ClientSockAddrIn, &ClientSockAddrLen);
	
		gpio_set_level(OUT_FRONT_PANEL_LED1, 1);
		gpio_set_level(OUT_FRONT_PANEL_LED2, 1);
		printf("Accepted\n");

		if (ClientSock != -1)
		{
			int Recieved = recv(ClientSock, &WifiBuffer, sizeof(WifiBuffer) - 1, 0);
			if (Recieved > 0)
			{
				WifiBuffer[Recieved] = 0;
				printf("Got: %s\n", WifiBuffer);

				const char* ExpectedRequests[] =
				{
					"GET / HTTP/",
					"GET /index.html HTTP/",
					"POST / HTTP/",
					"POST /index.html HTTP/"
				};

				bool bReturnIndex = false;
				bool bStartRecieving = false;
				for (int i = 0; i < ARRAY_NUM(ExpectedRequests); i++)
				{
					int Len = strlen(ExpectedRequests[i]);
					if (Recieved >= Len && strncmp(WifiBuffer, ExpectedRequests[i], Len) == 0)
					{
						printf("Matched: %s\n", ExpectedRequests[i]);
						if (strncmp(WifiBuffer, "POST", 4) == 0)
							bStartRecieving = true;
						else
							bReturnIndex = true;
						break;
					}
				}

				if (bStartRecieving)
				{
					int Length = 0;
					const char* ContentLength = strstr(WifiBuffer, "Content-Length: ");
					if (ContentLength)
					{
						Length = atoi(ContentLength + strlen("Content-Length: "));
					}

					esp_err_t ErrorCode = ESP_OK;
					bool bWaitingForStart = true;
					esp_ota_handle_t UpdateHandle = 0 ;
					const esp_partition_t *UpdatePartition = esp_ota_get_next_update_partition(nullptr);

					if (Length > 0)
					{
						printf("Receiving firmware (Length = %d)\n", Length);

						while (Length > 0)
						{
							Recieved = recv(ClientSock, &WifiBuffer, sizeof(WifiBuffer) - 1, 0);
							if (Recieved <= 0)
								break;

							WifiBuffer[Recieved] = 0;
							if (bWaitingForStart)
							{
								const char *Start = strstr(WifiBuffer, "LGV_FIRM");
								if (Start)
								{
									printf("Found firmware, starting update\n");

									bWaitingForStart = false;
    								ErrorCode = esp_ota_begin(UpdatePartition, OTA_SIZE_UNKNOWN, &UpdateHandle);

									const char* StartOfFirmware = Start + strlen("LGV_FIRM");
									int Afterwards = (WifiBuffer + Recieved) - StartOfFirmware;
									if (Afterwards > 0)
									{
										if (ErrorCode == ESP_OK)
										{
											ErrorCode = esp_ota_write(UpdateHandle, StartOfFirmware, Afterwards);
										}
									}
								}
							}
							else if (ErrorCode == ESP_OK)
							{
								ErrorCode = esp_ota_write(UpdateHandle, WifiBuffer, Recieved);
							}
							Length -= Recieved;
						}
					}

					if (ErrorCode == ESP_OK)
					{
						ErrorCode = esp_ota_end(UpdateHandle);
					}

					if (ErrorCode == ESP_OK)
					{
						ErrorCode = esp_ota_set_boot_partition(UpdatePartition);
					}

					bool bSuccess = (ErrorCode == ESP_OK && Length == 0 && !bWaitingForStart);

					const char* UpdatedResponse = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<!doctype html><title>LightGunVerter Firmware Update</title><style>*{box-sizing: border-box;}body{margin: 0;}#main{display: flex; min-height: calc(100vh - 40vh);}#main > article{flex: 1;}#main > nav, #main > aside{flex: 0 0 20vw;}#main > nav{order: -1;}header, footer, article, nav, aside{padding: 1em;}header, footer{height: 20vh;}</style><body> <header> <center><h1>LightGunVerter Firmware Update</h1></center> </header> <div id=\"main\"> <nav></nav> <article><p>Firmware update successful. Rebooting...</p></article> <aside></aside> </div></body>";
					const char* NotUpdatedResponse = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<!doctype html><title>LightGunVerter Firmware Update</title><style>*{box-sizing: border-box;}body{margin: 0;}#main{display: flex; min-height: calc(100vh - 40vh);}#main > article{flex: 1;}#main > nav, #main > aside{flex: 0 0 20vw;}#main > nav{order: -1;}header, footer, article, nav, aside{padding: 1em;}header, footer{height: 20vh;}</style><body> <header> <center><h1>LightGunVerter Firmware Update</h1></center> </header> <div id=\"main\"> <nav></nav> <article><p>Update failed.</p></article> <aside></aside> </div></body>";
					const char* Response = bSuccess ? UpdatedResponse : NotUpdatedResponse;

					send(ClientSock, Response, strlen(Response), 0);

					bUpdated = bSuccess;
				}
				else
				{
					printf("Sending response\n");
					const char* GoodResponse = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<!doctype html><title>LightGunVerter Firmware Update</title><style>*{box-sizing: border-box;}body{margin: 0;}#main{display: flex; min-height: calc(100vh - 40vh);}#main > article{flex: 1;}#main > nav, #main > aside{flex: 0 0 20vw;}#main > nav{order: -1;}header, footer, article, nav, aside{padding: 1em;}header, footer{height: 20vh;}</style><body> <header> <center><h1>LightGunVerter Firmware Update</h1></center> </header> <div id=\"main\"> <nav></nav> <article> <p> Upload new .bin file: <form id=\"uploadbanner\" enctype=\"multipart/form-data\" method=\"post\" action=\"#\"> <input id=\"fileupload\" name=\"myfile\" type=\"file\"/> <input type=\"submit\" value=\"Update\" id=\"submit\"/> </form> </p><p> <b>Do not remove power while updating</b> </p></article> <aside></aside> </div></body>";
					const char* BadResponse = "HTTP/1.0 404 NOT FOUND\r\nContent-Type: text/html\r\n\r\n<!doctype html><title>Not Found</title><body>Page not found :(</body>";
					const char* Response = bReturnIndex ? GoodResponse : BadResponse;

					send(ClientSock, Response, strlen(Response), 0);
				}
			}

			printf("Close\n");
			close(ClientSock);
		}
	}
}

extern "C" void app_main(void)
{
	InitializeMiscGPIO();
	InitPersistantStorage();

	nvs_flash_init();

	if (GetPersistantStorage() == PERSISTANT_FIRMWARE_UPDATE_MODE)
	{
		SetPersistantStorage(PERSISTANT_FIRMWARE_DONE_UPDATE);

		WifiInitAccessPoint();
		WifiStartListening();
		vTaskDelay(2000);
		esp_wifi_stop();
		esp_wifi_deinit();
		esp_restart();
	}
	
	InitializeMenu();

	xTaskCreatePinnedToCore(&WiimoteTask, "WiimoteTask", 8192, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&SpotGeneratorTask, "SpotGeneratorTask", 2048, NULL, 5, NULL, 1);
}
