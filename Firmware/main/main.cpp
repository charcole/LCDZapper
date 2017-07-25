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
#include "nvs_flash.h"
};
#include "esp_wiimote.h"
#include "images.h"

#define OUT_SCREEN_DIM  (GPIO_NUM_16) // Controls drawing spot on screen
#define OUT_SCREEN_DIM_INV (GPIO_NUM_21) // Inverted version of above
#define OUT_PLAYER1_LED (GPIO_NUM_26) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED (GPIO_NUM_27) // ANDed with detected white level in HW
#define OUT_PLAYER1_LED_OUT_SELECTION_REG (GPIO_FUNC26_OUT_SEL_CFG_REG) // Used to turn on and off player LED output (for supporting two player)
#define OUT_PLAYER2_LED_OUT_SELECTION_REG (GPIO_FUNC27_OUT_SEL_CFG_REG) // Used to turn on and off player LED output (for supporting two player)
#define OUT_PLAYER1_TRIGGER_PULLED (GPIO_NUM_13) // Used for Wiimote-only operation
#define OUT_PLAYER2_TRIGGER_PULLED (GPIO_NUM_14) // Used for Wiimote-only operation

#define IN_COMPOSITE_SYNC (GPIO_NUM_19) // Compsite sync input (If changed change also in asm loop)

#define RMT_SCREEN_DIM_CHANNEL    RMT_CHANNEL_1     /*!< RMT channel for transmitter */

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

static bool LogoMode = true;
static bool TextMode = true;
static uint32_t *ImageData = &ImagePress12[0][0];
static int LogoTime = 4000;
static int DisplayTime = 0;
static bool ShowPointer = true;
static bool DoingCalibration = false;
static int CurrentLine = 0;
static int PlayerMask = 0; // Set to 1 for two player
static int CoopMask = 1; // Set to 0 for co-op play
static int ReticuleStartLineNum[2] = { 1000,1000 };
static int ReticuleXPosition[2] = { 320,320 };
static int ReticuleSizeLookup[2][14];

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
					CoopMask = 0;	// Enable co-op
					ImageData = &ImageCoop[0][0];
					DisplayTime = 1500;
					TextMode = true;
				}

				if (!DoingCalibration && (Data->Buttons & WiimoteData::kButton_Minus))
				{
					CoopMask = 1;	// Disable co-op
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
	printf("WiimoteTask running on core %d\n", xPortGetCoreID());
	GWiimoteManager.Init();
	PlayerInput Player1(0);
	PlayerInput Player2(1);
	bool LastLocalShowPointer=false;
	while (true)
	{
		GWiimoteManager.Tick();
		Player1.Tick();
		Player2.Tick();
		bool Player1Button = Player1.ButtonWasPressed(WiimoteData::kButton_A | WiimoteData::kButton_B);
		bool Player2Button = Player2.ButtonWasPressed(WiimoteData::kButton_A | WiimoteData::kButton_B);
		if (CoopMask == 0)
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, Player1Button || Player2Button);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, false);
		}
		else
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, Player1Button);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, Player2Button);
		}
		bool LocalShowPointer = ShowPointer || TextMode || LogoMode;
		if (LocalShowPointer != LastLocalShowPointer)
		{
			gpio_matrix_out(OUT_SCREEN_DIM, LocalShowPointer ? RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL : SIG_GPIO_OUT_IDX, false, false);
			gpio_matrix_out(OUT_SCREEN_DIM_INV, LocalShowPointer ? RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL : SIG_GPIO_OUT_IDX, true, false);
			LastLocalShowPointer = LocalShowPointer;
		}
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
	RMTTxConfig.gpio_num = OUT_PLAYER1_LED;
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

	// First player 555 input
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_PLAYER1_LED], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_PLAYER1_LED, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_PLAYER1_LED, 1); // If we turn it off keep high
	gpio_matrix_out(OUT_PLAYER1_LED, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, true, false);

	// Second player 555 input
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_PLAYER2_LED], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_PLAYER2_LED, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_PLAYER2_LED, 1); // If we turn it off keep high
	gpio_matrix_out(OUT_PLAYER2_LED, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, true, false);

	// Screen dimmer
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_SCREEN_DIM], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_SCREEN_DIM, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_SCREEN_DIM, 1); // If we turn it off keep high
	gpio_matrix_out(OUT_SCREEN_DIM, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, false, false);

	// Screen dimmer (inverted)
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[OUT_SCREEN_DIM_INV], PIN_FUNC_GPIO);
	gpio_set_direction(OUT_SCREEN_DIM_INV, GPIO_MODE_OUTPUT);
	gpio_set_level(OUT_SCREEN_DIM_INV, 1); // If we turn it off keep high (will be inverted)
	gpio_matrix_out(OUT_SCREEN_DIM_INV, RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL, true, false);

}

inline void IRAM_ATTR ActivateRMTOnSyncFallingEdge(void)
{
	// Tight loop that sits spinning until GPIO19 (see assembly) aka IN_COMPOSITE_SYNC falls low and then starts RMT peripheral

	volatile uint32_t *RMTConfig1 = &RMT.conf_ch[RMT_SCREEN_DIM_CHANNEL].conf1.val;
	volatile uint32_t *GPIOIn = &GPIO.in;
	uint32_t Temp = 0, TXStart = 1 | 8; // Start and reset
	asm volatile
		(
			"\
			memw;\
SPINLOOP:   l32i.n %0, %1, 0;\
			bbsi %0, 19, SPINLOOP;\
			l32i.n %0, %2, 0;\
			or %0, %0, %3;\
			s32i.n %0, %2, 0;\
			memw;\
		"
			: "+r"(Temp)
			: "r"(GPIOIn), "r"(RMTConfig1), "r"(TXStart)
			:
			);
}

void IRAM_ATTR CompositeSyncPositiveEdge(void)
{
	bool Active = false;
	int CurrentPlayer = (CurrentLine & 1) & PlayerMask;
	int StartingLine = ReticuleStartLineNum[CurrentPlayer];
	int XCoordinate = ReticuleXPosition[CurrentPlayer];
	rmt_item32_t EndTerminator;
	EndTerminator.level0 = 1;
	EndTerminator.duration0 = 0;
	EndTerminator.level1 = 1;
	EndTerminator.duration1 = 0;
	if (LogoMode && CurrentLine >= LOGO_START_LINE && CurrentLine < LOGO_END_LINE)
	{
		WRITE_PERI_REG(OUT_PLAYER1_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
		WRITE_PERI_REG(OUT_PLAYER2_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
		int LineIdx = CurrentLine - LOGO_START_LINE;
		for (int i = 0; i < 8; i++)
		{
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[i].val = ImageLogo[LineIdx][i];
		}
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[8].val = EndTerminator.val;
		Active = true;
	}
	else if (TextMode && CurrentLine >= TEXT_START_LINE && CurrentLine < TEXT_END_LINE)
	{
		WRITE_PERI_REG(OUT_PLAYER1_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
		WRITE_PERI_REG(OUT_PLAYER2_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
		int LineIdx = CurrentLine - TEXT_START_LINE;
		for (int i = 0; i < 8; i++)
		{
			RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[i].val = ImageData[8*LineIdx + i];
		}
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[8].val = EndTerminator.val;
		Active = true;
	}
	else if (CurrentLine >= StartingLine && CurrentLine < StartingLine + ARRAY_NUM(ReticuleSizeLookup[0]))
	{
		int OutputSelect = CurrentPlayer & CoopMask; // In co-op always output to player 1's gun otherwise select which gun to go to
		WRITE_PERI_REG(OutputSelect ? OUT_PLAYER1_LED_OUT_SELECTION_REG : OUT_PLAYER2_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
		WRITE_PERI_REG(OutputSelect ? OUT_PLAYER2_LED_OUT_SELECTION_REG : OUT_PLAYER1_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | ((RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL) << GPIO_FUNC0_OUT_SEL_S)); // Output pulse
		rmt_item32_t HorizontalPulse;
		HorizontalPulse.level0 = 1;
		HorizontalPulse.duration0 = XCoordinate - ReticuleSizeLookup[CurrentPlayer][CurrentLine - StartingLine];
		HorizontalPulse.level1 = 0;
		HorizontalPulse.duration1 = 2 * ReticuleSizeLookup[CurrentPlayer][CurrentLine - StartingLine];
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[0].val = HorizontalPulse.val;
		RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[1].val = EndTerminator.val;
		Active = true;
	}
	if (Active)
	{
		ActivateRMTOnSyncFallingEdge();
	}
	CurrentLine++;
}

void IRAM_ATTR SpotGeneratorInnerLoop()
{
	timer_idx_t timer_idx = TIMER_1;
	while (true)
	{
		TIMERG1.hw_timer[timer_idx].reload = 1;
		while ((GPIO.in & BIT(IN_COMPOSITE_SYNC)) != 0); // while sync is still happening
		TIMERG1.hw_timer[timer_idx].update = 1;
		// Don't really need the 64-bit time but only reading cnt_low seems to caused it to sometimes not update. Adding some nops also worked but not as reliably as this
		uint64_t Time = 2*((TIMERG1.hw_timer[timer_idx].cnt_high<<32) | TIMERG1.hw_timer[timer_idx].cnt_low); // Timer's clk is half APB hence 2x.
		while ((GPIO.in & BIT(IN_COMPOSITE_SYNC)) == 0); // while not sync
		if (Time > TIMING_VSYNC_THRESHOLD)
		{
			CurrentLine = 0;
		}
		else
		{
			CompositeSyncPositiveEdge();
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

	RMTPeripheralInit();
	rmt_item32_t RMTInitialValues[1];
	RMTInitialValues[0].level0 = 1;
	RMTInitialValues[0].duration0 = 1;
	RMTInitialValues[0].level1 = 1;
	RMTInitialValues[0].duration1 = 1;
	rmt_write_items(RMT_SCREEN_DIM_CHANNEL, RMTInitialValues, 1, false);	// Prime the RMT

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
	gpio_config_t TriggerPullOutput;
	TriggerPullOutput.intr_type = GPIO_INTR_DISABLE;
	TriggerPullOutput.pin_bit_mask = BIT(OUT_PLAYER1_TRIGGER_PULLED);
	TriggerPullOutput.mode = GPIO_MODE_OUTPUT;
	TriggerPullOutput.pull_up_en = GPIO_PULLUP_DISABLE;
	TriggerPullOutput.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&TriggerPullOutput);

	TriggerPullOutput.pin_bit_mask = BIT(OUT_PLAYER2_TRIGGER_PULLED);
	gpio_config(&TriggerPullOutput);
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
