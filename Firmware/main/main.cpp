#include <stdio.h>
#include <math.h>
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "nvs_flash.h"
};
#include "esp_wiimote.h"

#define OUT_SCREEN_DIM  (GPIO_NUM_16) // Controls drawing spot on screen
#define OUT_SCREEN_DIM_INV (GPIO_NUM_21) // Inverted version of above
#define OUT_PLAYER1_LED (GPIO_NUM_26) // ANDed with detected white level in HW
#define OUT_PLAYER2_LED (GPIO_NUM_27) // ANDed with detected white level in HW
#define OUT_PLAYER1_LED_OUT_SELECTION_REG (GPIO_FUNC26_OUT_SEL_CFG_REG) // Used to turn on and off player LED output (for supporting two player)
#define OUT_PLAYER2_LED_OUT_SELECTION_REG (GPIO_FUNC27_OUT_SEL_CFG_REG) // Used to turn on and off player LED output (for supporting two player)
#define OUT_PLAYER1_TRIGGER_PULLED (GPIO_NUM_13) // Used for Wiimote-only operation
#define OUT_PLAYER2_TRIGGER_PULLED (GPIO_NUM_14) // Used for Wiimote-only operation

#define IN_COMPOSITE_SYNC (GPIO_NUM_19) // Compsite sync input (If changed change also in asm loop)
#define IN_VERTICAL_SYNC (GPIO_NUM_18) // Vertical sync input

#define RMT_SCREEN_DIM_CHANNEL    RMT_CHANNEL_1     /*!< RMT channel for transmitter */

// Change these if using with NTSC
#define TIMING_RETICULE_WIDTH 75.0f // Generates a circle in PAL but might need adjusting for NTSC (In 80ths of a microsecond)
#define TIMING_BACK_PORCH 8*80		// In 80ths of a microsecond	(Should be about 6*80)
#define TIMING_LINE_DURATION  8*465 // In 80ths of a microsecond  (Should be about 52*80 but need to clip when off edge)
#define TIMING_BLANKED_LINES 24		// Should be about 16?
#define TIMING_VISIBLE_LINES 250	// Should be 288

#define ARRAY_NUM(x) (sizeof(x)/sizeof(x[0]))

static int CurrentLine = 0;
static int PlayerMask = 0; // Set to 1 for two player
static int CoopMask = 1; // Set to 0 for co-op play
static int ReticuleStartLineNum[2] = { 100,100 };
static int ReticuleXPosition[2] = { 320,320 };
static int ReticuleSizeLookup[14];

void WiimoteTask(void *pvParameters)
{
	printf("WiimoteTask running on core %d\n", xPortGetCoreID());
	GWiimoteManager.Init();
	bool ShowPointer = true;
	bool HomeWasPressed[2] = {false, false};
	bool ButtonPressed[2] = {false, false};
	int FrameNumber[2]={0,0};
	IWiimote *Player[2];
	Player[0] = GWiimoteManager.CreateNewWiimote();
	Player[1] = GWiimoteManager.CreateNewWiimote();
	while (true)
	{
		GWiimoteManager.Tick();
		for (int PlayerIdx=0; PlayerIdx<2; PlayerIdx++)
		{
			const WiimoteData *Data = Player[PlayerIdx]->GetData();
			if (Data->FrameNumber != FrameNumber[PlayerIdx])
			{
				FrameNumber[PlayerIdx] = Data->FrameNumber;
				bool HomePressed = (Data->Buttons & WiimoteData::kButton_Home);
				if (HomePressed && !HomeWasPressed[PlayerIdx])
				{
					ShowPointer = !ShowPointer;
					gpio_matrix_out(OUT_SCREEN_DIM, ShowPointer ? RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL : SIG_GPIO_OUT_IDX, false, false);
					gpio_matrix_out(OUT_SCREEN_DIM_INV, ShowPointer ? RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL : SIG_GPIO_OUT_IDX, true, false);
				}
				HomeWasPressed[PlayerIdx] = HomePressed;

				ButtonPressed[PlayerIdx] = ((Data->Buttons & (WiimoteData::kButton_B | WiimoteData::kButton_A)) != 0);

				if (Data->Buttons & WiimoteData::kButton_Plus)
					CoopMask = 0;	// Enable co-op

				if (Data->Buttons & WiimoteData::kButton_Minus)
					CoopMask = 1;	// Disable co-op

				if (Data->IRSpot[0].X != 0x3FF || Data->IRSpot[0].Y != 0x3FF)
				{
					ReticuleXPosition[PlayerIdx] = TIMING_BACK_PORCH + (TIMING_LINE_DURATION*(1023 - Data->IRSpot[0].X)) / 1024;
					ReticuleStartLineNum[PlayerIdx] = TIMING_BLANKED_LINES + (TIMING_VISIBLE_LINES*(Data->IRSpot[0].Y + Data->IRSpot[0].Y / 3)) / 1024;
				}
				else
				{
					ReticuleStartLineNum[PlayerIdx] = 1000; // Don't draw
				}
				if (PlayerIdx == 1)
				{
					PlayerMask = 1; // If second Wiimote connected enable two player mode
				}
			}
		}
		if (CoopMask == 0)
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, ButtonPressed[0] || ButtonPressed[1]);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, false);
		}
		else
		{
			gpio_set_level(OUT_PLAYER1_TRIGGER_PULLED, ButtonPressed[0]);
			gpio_set_level(OUT_PLAYER2_TRIGGER_PULLED, ButtonPressed[1]);
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

inline void IRAM_ATTR ActiveRMTOnSyncFallingEdge(void)
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

void IRAM_ATTR CompositeSyncInterrupt(void* arg)
{
	if (GPIO.status & BIT(IN_COMPOSITE_SYNC)) // Check composite sync caused interrupt
	{
		if ((GPIO.in & BIT(IN_VERTICAL_SYNC)) == 0)
		{
			int CurrentPlayer = (CurrentLine & 1)&PlayerMask;
			int StartingLine = ReticuleStartLineNum[CurrentPlayer];
			int XCoordinate = ReticuleXPosition[CurrentPlayer];
			if (CurrentLine >= StartingLine && CurrentLine < StartingLine + ARRAY_NUM(ReticuleSizeLookup))
			{
				int OutputSelect = CurrentPlayer&CoopMask; // In co-op always output to player 1's gun otherwise select which gun to go to
				WRITE_PERI_REG(OutputSelect ? OUT_PLAYER1_LED_OUT_SELECTION_REG : OUT_PLAYER2_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | (SIG_GPIO_OUT_IDX << GPIO_FUNC0_OUT_SEL_S)); // Keep this line high (not used)
				WRITE_PERI_REG(OutputSelect ? OUT_PLAYER2_LED_OUT_SELECTION_REG : OUT_PLAYER1_LED_OUT_SELECTION_REG, GPIO_FUNC0_OUT_INV_SEL | ((RMT_SIG_OUT0_IDX + RMT_SCREEN_DIM_CHANNEL) << GPIO_FUNC0_OUT_SEL_S)); // Output pulse
				rmt_item32_t HorizontalPulse;
				HorizontalPulse.level0 = 1;
				HorizontalPulse.duration0 = XCoordinate - ReticuleSizeLookup[CurrentLine - StartingLine];
				HorizontalPulse.level1 = 0;
				HorizontalPulse.duration1 = 2 * ReticuleSizeLookup[CurrentLine - StartingLine];
				RMTMEM.chan[RMT_SCREEN_DIM_CHANNEL].data32[0].val = HorizontalPulse.val;
				ActiveRMTOnSyncFallingEdge();
			}
			CurrentLine++;
		}
		else
		{
			CurrentLine = 0;
		}
	}
	// Acknowledge we handled it
	GPIO.status_w1tc = ~0;
	GPIO.status1_w1tc.intr_st = 0xFF;
}

void SetupCompositeSyncInterrupt()
{
	intr_handle_t Handle;
	esp_intr_alloc(ETS_GPIO_INTR_SOURCE, ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_EDGE, CompositeSyncInterrupt, NULL, &Handle);

	gpio_config_t CSyncGPIOConfig;
	CSyncGPIOConfig.intr_type = GPIO_INTR_POSEDGE;
	CSyncGPIOConfig.pin_bit_mask = BIT(IN_COMPOSITE_SYNC);
	CSyncGPIOConfig.mode = GPIO_MODE_INPUT;
	CSyncGPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
	CSyncGPIOConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_config(&CSyncGPIOConfig);
}

void SpotGeneratorTask(void *pvParameters)
{
	printf("SpotGeneratorTask starting on core %d\n", xPortGetCoreID());

	ESP_ERROR_CHECK(gpio_set_direction(IN_COMPOSITE_SYNC, GPIO_MODE_INPUT));
	ESP_ERROR_CHECK(gpio_set_direction(IN_VERTICAL_SYNC, GPIO_MODE_INPUT));
	ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT));

	int ReticuleNumLines = ARRAY_NUM(ReticuleSizeLookup);
	float ReticuleHalfSize = ReticuleNumLines / 2.0f;
	float ReticuleMiddle = (ReticuleNumLines - 1) / 2.0f;
	for (int i = 0; i < ReticuleNumLines; i++)
	{
		float y = (i - ReticuleMiddle) / ReticuleHalfSize;
		float x = sqrtf(1.0f - y*y);
		ReticuleSizeLookup[i] = TIMING_RETICULE_WIDTH*x;
	}

	RMTPeripheralInit();
	rmt_item32_t RMTInitialValues[1];
	RMTInitialValues[0].level0 = 1;
	RMTInitialValues[0].duration0 = 1;
	RMTInitialValues[0].level1 = 1;
	RMTInitialValues[0].duration1 = 1;
	rmt_write_items(RMT_SCREEN_DIM_CHANNEL, RMTInitialValues, 1, false);	// Prime the RMT

	SetupCompositeSyncInterrupt();

	while (true)
	{
		// Now running completely on interrupts but might be better to spin in here but I think this caused problems for Bluetooth
		vTaskDelay(1);
	}
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
