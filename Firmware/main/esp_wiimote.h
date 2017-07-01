#ifndef __ESP_WIIMOTE_H__
#define __ESP_WIIMOTE_H__

class WiimoteBluetoothConnection;

struct WiimoteData
{
	enum
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

	struct Spot
	{
		uint16_t X;
		uint16_t Y;
		uint8_t Size;
	};

	uint16_t Buttons;
	uint8_t BatteryLevel;
	uint8_t LEDs;
	int32_t AccelX : 10;
	int32_t AccelY : 10;
	int32_t AccelZ : 10;
	int32_t FrameNumber;
	Spot IRSpot[4];
};

class IWiimote
{
public:
	virtual void SetPlayerLEDs(uint8_t LEDs) = 0;
	virtual WiimoteData *GetData() = 0;
};

class WiimoteManager
{
private:
	enum
	{
		kMaxWiimotes = 8
	};

public:
	WiimoteManager();

	void Init();
	void Tick();

	IWiimote* CreateNewWiimote();

private:
	WiimoteBluetoothConnection* Wiimotes[kMaxWiimotes];
};

extern WiimoteManager GWiimoteManager;

#endif // __ESP_WIIMOTE_H__
