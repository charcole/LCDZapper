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
#include <math.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "driver/timer.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
};
#include "esp_wiimote.h"
#include "images.h"

#define OUT_SCREEN_DIM  (GPIO_NUM_23) // Controls drawing spot on screen
#define OUT_SCREEN_DIM_INV (GPIO_NUM_22) // Inverted version of above
#define OUT_SCREEN_DIM_SELECTION_REG (GPIO_FUNC23_OUT_SEL_CFG_REG) // Used to route which bank goes to the screen dimming
#define OUT_SCREEN_DIM_INV_SELECTION_REG (GPIO_FUNC22_OUT_SEL_CFG_REG) // Used to route which bank goes to the screen dimming
#define OUT_PLAYER1_LED (GPIO_NUM_18) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED (GPIO_NUM_17) // ANDed with detected white level in HW
#define OUT_PLAYER1_LED_DELAYED (GPIO_NUM_5) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED_DELAYED (GPIO_NUM_16) // ANDed with detected white level in HW
#define OUT_PLAYER1_TRIGGER_PULLED (GPIO_NUM_25) // Used for Wiimote-only operation
#define OUT_PLAYER2_TRIGGER_PULLED (GPIO_NUM_27) // Used for Wiimote-only operation
#define OUT_WHITE_OVERRIDE (GPIO_NUM_19) // Ignore the white level
#define OUT_FRONT_PANEL_LED1 (GPIO_NUM_32) // Ignore the white level
#define OUT_FRONT_PANEL_LED2 (GPIO_NUM_4) // Ignore the white level

#define IN_COMPOSITE_SYNC (GPIO_NUM_21) // Compsite sync input (If changed change also in asm loop)

#define RMT_SCREEN_DIM_CHANNEL    	RMT_CHANNEL_1     /*!< RMT channel for screen*/
#define RMT_TRIGGER_CHANNEL			RMT_CHANNEL_3     /*!< RMT channel for trigger */
#define RMT_DELAY_TRIGGER_CHANNEL	RMT_CHANNEL_5     /*!< RMT channel for delayed trigger */

#define LEDC_WHITE_LEVEL_TIMER      LEDC_TIMER_0
#define LEDC_WHITE_LEVEL_MODE       LEDC_HIGH_SPEED_MODE
#define LEDC_WHITE_LEVEL_GPIO       (GPIO_NUM_13)
#define LEDC_WHITE_LEVEL_CHANNEL    LEDC_CHANNEL_0
#define WHITE_LEVEL_STEP			250				  // About 0.1V steps

// Change these if using with NTSC
#define TIMING_RETICULE_WIDTH 75.0f // Generates a circle in PAL but might need adjusting for NTSC (In 80ths of a microsecond)
#define TIMING_BACK_PORCH 8*80		// In 80ths of a microsecond	(Should be about 6*80)
#define TIMING_LINE_DURATION  8*465 // In 80ths of a microsecond  (Should be about 52*80 but need to clip when off edge)
#define TIMING_BLANKED_LINES 24		// Should be about 16?
#define TIMING_VISIBLE_LINES 250	// Should be 288
#define TIMING_VSYNC_THRESHOLD (80*16) // If sync is longer than this then doing a vertical sync
#define TEXT_START_LINE 105
#define TEXT_END_LINE (TEXT_START_LINE + 80)
#define LOGO_START_LINE (TIMING_BLANKED_LINES + 25)
#define LOGO_END_LINE (LOGO_START_LINE + 200)

#define ARRAY_NUM(x) (sizeof(x)/sizeof(x[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

static bool LogoMode = true;
static bool TextMode = true;
static uint32_t *ImageData = &ImagePress12[0][0];
static int LogoTime = 4000;
static int DisplayTime = 0;
static bool ShowPointer = true;
static bool DoingCalibration = false;
static bool Coop = false;
static int CurrentLine = 0;
static int PlayerMask = 0; // Set to 1 for two player
static int ReticuleStartLineNum[2] = { 1000,1000 };
static int ReticuleXPosition[2] = { 320,320 };
static int ReticuleSizeLookup[2][14];
static int CalibrationDelay = 0;
static int LastActivePlayer = 0;
static int WhiteLevel = 3330;	// Should produce test voltage of 1.3V (good for composite video)

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
	}

	void Tick()
	{
		const WiimoteData *Data = Wiimote->GetData();
		if (Data->FrameNumber != FrameNumber)
		{
			FrameNumber = Data->FrameNumber;
			if (ButtonClicked(Data->Buttons, WiimoteData::kButton_Home))
			{
				ShowPointer = !ShowPointer;
			}

			if (CalibrationPhase < 4 && ButtonClicked(Data->Buttons, (WiimoteData::kButton_B | WiimoteData::kButton_A)))
			{
				if (Data->IRSpot[0].X != 0x3FF || Data->IRSpot[0].Y != 0x3FF)
				{
					CalibrationData[CalibrationPhase].X = (float)Data->IRSpot[0].X;
					CalibrationData[CalibrationPhase].Y = (float)Data->IRSpot[0].Y;
					CalibrationPhase++;
					DoneCalibration = (CalibrationPhase == 4);
					DoingCalibration = !DoneCalibration;
				}
			}

			if (PlayerMask != 0)
			{
				if (!DoingCalibration && (Data->Buttons & WiimoteData::kButton_Plus))
				{
					Coop = true;
					ImageData = &ImageCoop[0][0];
					DisplayTime = 1500;
					TextMode = true;
				}

				if (!DoingCalibration && (Data->Buttons & WiimoteData::kButton_Minus))
				{
					Coop = false;
					ImageData = &ImageDual[0][0];
					DisplayTime = 1500;
					TextMode = true;
				}
			}

			if (Data->Buttons & WiimoteData::kButton_Up)
			{
				if (CalibrationPhase != 4)
				{
					CalibrationPhase = 4;
					DoingCalibration = false;
				}
				DoneCalibration = false;
			}
			
			if (!DoingCalibration && (Data->Buttons & WiimoteData::kButton_Down))
			{
				CalibrationPhase = 0;
				DoneCalibration = false;
				DoingCalibration = true;
				DisplayTime = 0;
			}

			if (CalibrationPhase < 4)
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

					}
					else
					{
						ReticuleStartLineNum[PlayerIdx] = 1000; // Don't draw
					}
				}
				else
				{
					if (Data->IRSpot[0].X != 0x3FF || Data->IRSpot[0].Y != 0x3FF)
					{
						ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + (TIMING_LINE_DURATION*(1023 - Data->IRSpot[0].X)) / 1024;
						ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + (TIMING_VISIBLE_LINES*(Data->IRSpot[0].Y + Data->IRSpot[0].Y / 3)) / 1024;
					}
					else
					{
						ReticuleStartLineNum[PlayerIdx] = 1000; // Don't draw
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
				else if (!DoingCalibration && DisplayTime <= 0)
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
};

void WiimoteTask(void *pvParameters)
{
	bool WasPlayer1Button = false;
	bool WasPlayer2Button = false;
	bool WasCalibrationDelayUp = false;
	bool WasCalibrationDelayDown = false;
	bool WasWhiteLevelChangeUp = true;
	bool WasWhiteLevelChangeDown = true;
	printf("WiimoteTask running on core %d\n", xPortGetCoreID());
	GWiimoteManager.Init();
	PlayerInput Player1(0);
	PlayerInput Player2(1);
	while (true)
	{
		GWiimoteManager.Tick();
		Player1.Tick();
		Player2.Tick();

		bool Player1Button = Player1.ButtonWasPressed(WiimoteData::kButton_A | WiimoteData::kButton_B);
		bool Player2Button = Player2.ButtonWasPressed(WiimoteData::kButton_A | WiimoteData::kButton_B);
		if (Coop)
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, Player1Button || Player2Button);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, false);
			if (Player1Button && !WasPlayer1Button)
				LastActivePlayer = 0;
			else if (Player2Button && !WasPlayer2Button)
				LastActivePlayer = 1;
		}
		else
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, Player1Button);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, Player2Button);
			LastActivePlayer = 0;
		}
		WasPlayer1Button = Player1Button;
		WasPlayer2Button = Player2Button;

		bool CalibrationDelayUp = Player1.ButtonWasPressed(WiimoteData::kButton_Right) || Player2.ButtonWasPressed(WiimoteData::kButton_Right);
		bool CalibrationDelayDown = Player1.ButtonWasPressed(WiimoteData::kButton_Left) || Player2.ButtonWasPressed(WiimoteData::kButton_Left);
		if (CalibrationDelayUp && !WasCalibrationDelayUp)
		{
			CalibrationDelay += 8; // Up 1/10th of microsecond
		}
		else if (CalibrationDelayDown && !WasCalibrationDelayDown)
		{
			CalibrationDelay = MAX(CalibrationDelay - 8, 0); // Down 1/10th of microsecond
		}
		WasCalibrationDelayUp = CalibrationDelayUp;
		WasCalibrationDelayDown = CalibrationDelayDown;
	
		bool WhiteLevelChangeUp = Player1.ButtonWasPressed(WiimoteData::kButton_One) || Player2.ButtonWasPressed(WiimoteData::kButton_One);
		bool WhiteLevelChangeDown = Player1.ButtonWasPressed(WiimoteData::kButton_Two) || Player2.ButtonWasPressed(WiimoteData::kButton_Two);
		if (WhiteLevelChangeDown && !WasWhiteLevelChangeDown)
		{
			if (WhiteLevel > 0)
			{
				WhiteLevel -= WHITE_LEVEL_STEP;
				if (WhiteLevel < 0)
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
		}
		else if (WhiteLevelChangeUp && !WasWhiteLevelChangeUp)
		{
			if (WhiteLevel < (1 << 13) - WHITE_LEVEL_STEP)
			{
				WhiteLevel += WHITE_LEVEL_STEP;
				gpio_set_direction(OUT_WHITE_OVERRIDE, GPIO_MODE_INPUT);
				ledc_set_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL, WhiteLevel);
				ledc_update_duty(LEDC_WHITE_LEVEL_MODE, LEDC_WHITE_LEVEL_CHANNEL);
			}
		}
		WasWhiteLevelChangeDown = WhiteLevelChangeDown;
		WasWhiteLevelChangeUp = WhiteLevelChangeUp;

		if (DisplayTime > 0)
		{
			DisplayTime--;
		}
		if (LogoTime > 0)
		{
			LogoTime--;
		}
		LogoMode = (LogoTime > 0);
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
			: "r"(GPIOIn), "r"(RMTConfig1), "r"(TXStart), "r"(RMTP1Config1), "r"(RMTP2Config1), "r"(RMTP1DConfig1), "r"(RMTP2DConfig1)\
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
	
	if (ShowPointer)
	{
		bool bPlayerVisibleOnLine[2];
		bPlayerVisibleOnLine[0] = CurrentLine >= StartingLine[0] && CurrentLine < StartingLine[0] + ARRAY_NUM(ReticuleSizeLookup[0]);
		bPlayerVisibleOnLine[1] = CurrentLine >= StartingLine[1] && CurrentLine < StartingLine[1] + ARRAY_NUM(ReticuleSizeLookup[0]);
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
	else if (CurrentLine>=100 && CurrentLine<120)
	{
		const char* Message="github.com/charcole";
		int TextLine=CurrentLine-100;
		int CurData=0;
		rmt_item32_t StartingDelay;
		StartingDelay.level0 = 1;
		StartingDelay.duration0 = TIMING_BACK_PORCH;
		StartingDelay.level1 = 1;
		StartingDelay.duration1 = 80;
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = StartingDelay.val;
		for (int Column=0; Column<20; Column++)
		{
			int Remapped = FontRemap[(unsigned char)Message[Column]];
			const uint32_t *FontData=Font[Remapped][TextLine];
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[0];
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[1];
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = FontData[2];
		}
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL + Bank].data32[CurData++].val = EndTerminator.val;
		Active = 1;
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

void IRAM_ATTR DoOutputSelection(uint32_t Bank)
{
	// Select between holding high or actually outputting
	WRITE_PERI_REG(OUT_SCREEN_DIM_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | ((RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) << GPIO_FUNC0_OUT_SEL_S));
	WRITE_PERI_REG(OUT_SCREEN_DIM_INV_SELECTION_REG, (RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL + Bank) << GPIO_FUNC0_OUT_SEL_S);
}

void IRAM_ATTR CompositeSyncPositiveEdge(uint32_t &Bank, int &Active, const int *CachedStartingLines)
{
	if (Active != 0 && CurrentLine != 0)
	{
		ActivateRMTOnSyncFallingEdge(Bank, Active);
		DoOutputSelection(Bank);
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

void SpotGeneratorTask(void *pvParameters)
{
	printf("SpotGeneratorTask starting on core %d\n", xPortGetCoreID());

	ESP_ERROR_CHECK(gpio_set_direction(IN_COMPOSITE_SYNC, GPIO_MODE_INPUT));

	int ReticuleNumLines = ARRAY_NUM(ReticuleSizeLookup[0]);
	float ReticuleHalfSize = ReticuleNumLines / 2.0f;
	float ReticuleMiddle = (ReticuleNumLines - 1) / 2.0f;
	for (int i = 0; i < ReticuleNumLines; i++)
	{
		float y = (i - ReticuleMiddle) / ReticuleHalfSize;
		float x = sqrtf(1.0f - y*y);
		ReticuleSizeLookup[0][i] = TIMING_RETICULE_WIDTH*x;
		x = 1.0f - fabsf(y);
		ReticuleSizeLookup[1][i] = TIMING_RETICULE_WIDTH*x;
	}

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

	gpio_config_t CSyncGPIOConfig;
	CSyncGPIOConfig.intr_type = GPIO_INTR_DISABLE;
	CSyncGPIOConfig.pin_bit_mask = BIT(IN_COMPOSITE_SYNC);
	CSyncGPIOConfig.mode = GPIO_MODE_INPUT;
	CSyncGPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
	CSyncGPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&CSyncGPIOConfig);

	timer_group_t timer_group = TIMER_GROUP_1;
	timer_idx_t timer_idx = TIMER_1;
	timer_config_t config;
	config.alarm_en = TIMER_ALARM_DIS;
	config.auto_reload = TIMER_AUTORELOAD_DIS;
	config.counter_dir = TIMER_COUNT_UP;
	config.divider = 1;
	config.intr_type = TIMER_INTR_LEVEL;
	config.counter_en = TIMER_START;
	timer_init(timer_group, timer_idx, &config);
	timer_set_counter_value(timer_group, timer_idx, 0ULL);

	vTaskEndScheduler(); // Disable FreeRTOS on this core as we don't need it anymore

	SpotGeneratorInnerLoop();
}

void InitializeMiscGPIO()
{
	gpio_config_t GPIOConfig;
	GPIOConfig.intr_type = GPIO_INTR_DISABLE;
	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER1_TRIGGER_PULLED);
	GPIOConfig.mode = GPIO_MODE_OUTPUT;
	GPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
	GPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&GPIOConfig);

	GPIOConfig.pin_bit_mask = BIT(OUT_PLAYER2_TRIGGER_PULLED);
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
}

extern "C" void app_main(void)
{
	// Magic non-sense to make second core work
	vTaskDelay(500 / portTICK_PERIOD_MS);
	nvs_flash_init();

	InitializeMiscGPIO();

	xTaskCreatePinnedToCore(&WiimoteTask, "WiimoteTask", 8192, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&SpotGeneratorTask, "SpotGeneratorTask", 2048, NULL, 5, NULL, 1);
}
