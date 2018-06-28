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
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_wiimote.h"

// Provides a minimal Bluetooth stack for communicating with Wiimotes on ESP32

#define WIIMOTE_VERBOSE 0

#if WIIMOTE_VERBOSE
#define VERBOSE_PRINT(...) printf(__VA_ARGS__)
#else
#define VERBOSE_PRINT(...) do {} while(0)
#endif

#define check(a) do { if (!(a)) printf("ERROR: Check failed: %s", #a); } while(0)

enum H4Type
{
	H4_TYPE_COMMAND = 1,
	H4_TYPE_ACL = 2,
	H4_TYPE_SCO = 3,
	H4_TYPE_EVENT = 4
};

enum L2CAPMessageCodes
{
	L2CAP_COMMAND_REJECT_RESPONSE = 1,
	L2CAP_CONNECTION_REQUEST = 2,
	L2CAP_CONNECTION_RESPONSE = 3,
	L2CAP_CONFIGURATION_REQUEST = 4,
	L2CAP_CONFIGURATION_RESPONSE = 5,
	L2CAP_DISCONNECTION_REQUEST = 6,
	L2CAP_DISCONNECTION_RESPONSE = 7,
	L2CAP_INFORMATION_REQUEST = 10,
	L2CAP_INFORMATION_RESPONSE = 11,
	L2CAP_COMMAND_COMPLETE = 14,
	L2CAP_COMMAND_STATUS = 15
};

enum L2CAPChannels
{
	L2CAP_SIGNALING_CHANNEL = 1
};

enum
{
	WIIMOTE_REPORT_SET_LEDS = 0x11,
	WIIMOTE_REPORT_REQUEST_REPORT = 0x12,
	WIIMOTE_REPORT_IR_ENABLE_1 = 0x13,
	WIIMOTE_REPORT_WRITE_MEMORY = 0x16,
	WIIMOTE_REPORT_IR_ENABLE_2 = 0x1A,
	WIIMOTE_REPORT_STATUS_INFORMATION = 0x20,
	WIIMOTE_REPORT_READ_MEMORY = 0x21,
	WIIMOTE_REPORT_ACKNOWLEDGE = 0x22,
	WIIMOTE_REPORT_CORE_BUTTONS = 0x30,
	WIIMOTE_REPORT_CORE_BUTTONS_ACC_IR12 = 0x33
};

class L2CAP;

/////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////

class GenericMessage
{
public:
	enum
	{
		kMaxMessageLength = 185 // To match MTU
	};

	GenericMessage()
	{
		Ptr = Buffer;
	}

	inline uint8_t* AddByte(uint8_t Byte)
	{
		*(Ptr++) = Byte;
		return Ptr - 1;
	}

	inline uint8_t* AddWord(uint16_t Word)
	{
		*(Ptr++) = (Word & 0xFF);
		*(Ptr++) = (Word >> 8);
		return Ptr - 2;
	}

	inline uint8_t* AddTri(uint32_t Tri)
	{
		*(Ptr++) = (Tri & 0xFF);
		*(Ptr++) = ((Tri >> 8) & 0xFF);
		*(Ptr++) = (Tri >> 16);
		return Ptr - 3;
	}

	inline uint8_t* AddTriBigEndian(uint32_t Tri)
	{
		*(Ptr++) = (Tri >> 16);
		*(Ptr++) = ((Tri >> 8) & 0xFF);
		*(Ptr++) = (Tri & 0xFF);
		return Ptr - 3;
	}

	inline uint8_t* SetWord(uint8_t *Start, uint16_t Word)
	{
		*(Start++) = (Word & 0xFF);
		*(Start++) = (Word >> 8);
		return Start - 2;
	}

	inline void AppendData(uint8_t *Data, int Size)
	{
		check((Ptr - Buffer) + Size <= kMaxMessageLength);
		memcpy(Ptr, Data, Size);
		Ptr += Size;
	}

	inline void AppendMessage(GenericMessage &SubMsg)
	{
		AppendData(SubMsg.Buffer, SubMsg.Ptr - SubMsg.Buffer);
	}

protected:
	uint8_t Buffer[kMaxMessageLength];
	uint8_t *Ptr;
};

class RingBuffer
{
	enum
	{
		kRingBufferSize = 1024 // Must be power of two
	};

public:
	RingBuffer()
	{
		Head = CallbackHead = Tail = 0;
		bPrintError = true;
	}

	inline void Put(uint8_t *Msg, uint16_t Length)
	{
		int Space = (sizeof(Data) - 1 + Tail - CallbackHead)&(sizeof(Data) - 1);
		if (Space < Length)
		{
			if (bPrintError)
			{
				printf("ERROR: Circular buffer couldn't fit message (%d/%d). Dropping.\n", Length, Space);
			}
			bPrintError = false;
			return;
		}
		bPrintError = true;
		WriteByte(Length >> 8);
		WriteByte(Length);
		for (int i = 0; i < Length; i++)
		{
			WriteByte(Msg[i]);
		}
		Head = CallbackHead;
	}

	inline uint16_t Get(uint8_t *Msg, int MaxLength)
	{
		if (Head != Tail)
		{
			uint16_t Length = ReadByte() << 8;
			Length |= ReadByte();
			if (Length <= MaxLength)
			{
				for (int i = 0; i < Length; i++)
				{
					Msg[i] = ReadByte();
				}
			}
			else
			{
				printf("ERROR: Receiving buffer is too small: %d/%d\n", Length, MaxLength);
				for (int i = 0; i < MaxLength; i++)
				{
					Msg[i] = ReadByte();
				}
				Tail = (Tail + Length - MaxLength) % (sizeof(Data) - 1);
			}
			return Length;
		}
		return 0;
	}

private:
	inline void WriteByte(uint8_t b)
	{
		Data[CallbackHead] = b;
		CallbackHead = (CallbackHead + 1)&(sizeof(Data) - 1);
	}

	inline uint8_t ReadByte()
	{
		int origTail = Tail;
		Tail = (Tail + 1)&(sizeof(Data) - 1);
		return Data[origTail];
	}

private:
	volatile int Head;	// Should be updated atomically
	int Tail;
	int CallbackHead;
	bool bPrintError;
	uint8_t Data[kRingBufferSize];
};

/////////////////////////////////////////////////////////////////////////////////////////////
// ESP32 Specific code
/////////////////////////////////////////////////////////////////////////////////////////////

class ESPBluetooth
{
private:
	static int ReceivePacket(uint8_t *Data, uint16_t Length)
	{
		VERBOSE_PRINT("Receiving:");
		for (int i = 0; i < Length; i++)
		{
			VERBOSE_PRINT(" %02x", Data[i]);
		}
		VERBOSE_PRINT("\n");

		if (Length > 1)
		{
			if (Data[0] == H4_TYPE_EVENT)
				EventBuffer.Put(Data + 1, Length - 1);
			else if (Data[0] == H4_TYPE_ACL)
				ACLBuffer.Put(Data + 1, Length - 1);
		}
		return 0;
	}

	static void SendReady()
	{
	}

public:
	static void InitBluetooth()
	{
#ifdef BT_CONTROLLER_INIT_CONFIG_DEFAULT
		esp_bt_controller_config_t BluetoothConfig = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
		esp_bt_controller_init(&BluetoothConfig);
		esp_bt_controller_enable(ESP_BT_MODE_BTDM);
#else
		esp_bt_controller_init();
#endif
		VHCICallbacks.notify_host_send_available = SendReady;
		VHCICallbacks.notify_host_recv = ReceivePacket;
		esp_vhci_host_register_callback(&VHCICallbacks);
	}
	
	static void DeInitBluetooth()
	{
		esp_bt_controller_disable();
		esp_bt_controller_deinit();
	}

	static void SendPacket(uint8_t *Data, uint16_t Length)
	{
		VERBOSE_PRINT("Sending:");
		for (uint16_t i = 0; i < Length; i++)
		{
			VERBOSE_PRINT(" %02x", Data[i]);
		}
		VERBOSE_PRINT("\n");
		while (!esp_vhci_host_check_send_available())
		{
			vTaskDelay(1);
		}
		esp_vhci_host_send_packet(Data, Length);
	}

	static uint16_t GetEventPacket(uint8_t *Data, uint16_t MaxLength)
	{
		return EventBuffer.Get(Data, MaxLength);
	}

	static uint16_t GetACLPacket(uint8_t *Data, uint16_t MaxLength)
	{
		return ACLBuffer.Get(Data, MaxLength);
	}

private:
	static RingBuffer EventBuffer;
	static RingBuffer ACLBuffer;
	static esp_vhci_host_callback_t VHCICallbacks;
};

RingBuffer ESPBluetooth::EventBuffer;
RingBuffer ESPBluetooth::ACLBuffer;
esp_vhci_host_callback_t ESPBluetooth::VHCICallbacks;

/////////////////////////////////////////////////////////////////////////////////////////////
// Message Builders
/////////////////////////////////////////////////////////////////////////////////////////////

class HCIMessage : public GenericMessage
{
public:
	HCIMessage(H4Type Type)
	{
		AddByte((uint8_t)Type);
	}

	void Send()
	{
		check((Ptr - Buffer) <= kMaxMessageLength);
		ESPBluetooth::SendPacket(Buffer, Ptr - Buffer);
	}
};

class HCICommand : public HCIMessage
{
public:
	HCICommand(uint16_t Cmd)
		: HCIMessage(H4_TYPE_COMMAND)
	{
		AddWord(Cmd);
		CMDLength = AddByte(0);
		CMDStart = Ptr;
	}

	void Send()
	{
		*CMDLength = (uint8_t)(Ptr - CMDStart);
		HCIMessage::Send();
	}

private:
	uint8_t *CMDStart;
	uint8_t *CMDLength;
};

class ACLMessage : public HCIMessage
{
public:
	ACLMessage(uint16_t Handle)
		: HCIMessage(H4_TYPE_ACL)
	{
		Handle |= (2 << 12); // Packet_Boundary_Flag: Automatically flushable
		Handle |= (0 << 14); // Broadcast_Flag: Point to point
		AddWord(Handle);
		ACLLength = AddWord(0);
		ACLStart = Ptr;
	}

	void Send()
	{
		SetWord(ACLLength, Ptr - ACLStart);
		HCIMessage::Send();
	}

private:
	uint8_t *ACLStart;
	uint8_t *ACLLength;
};

class L2CAPMessage : public ACLMessage
{
public:
	L2CAPMessage(uint16_t DestChannelID, uint16_t ACLHandle)
		: ACLMessage(ACLHandle)
	{
		L2CAPLength = AddWord(0);
		AddWord(DestChannelID);
		L2CAPStart = Ptr;
	}

	void Send()
	{
		SetWord(L2CAPLength, Ptr - L2CAPStart);
		ACLMessage::Send();
	}

private:
	uint8_t *L2CAPLength;
	uint8_t *L2CAPStart;
};

class L2CAPRequest : public L2CAPMessage
{
public:
	L2CAPRequest(uint8_t Code, uint16_t ACLHandle)
		: L2CAPMessage(L2CAP_SIGNALING_CHANNEL, ACLHandle)
	{
		AddByte(Code);
		AddByte(MsgId++);
		ReqLength = AddWord(0);
		ReqStart = Ptr;
	}

	L2CAPRequest(uint8_t Code, uint16_t ACLHandle, uint16_t OverrideMsgId)
		: L2CAPMessage(L2CAP_SIGNALING_CHANNEL, ACLHandle)
	{
		AddByte(Code);
		AddByte(OverrideMsgId);
		ReqLength = AddWord(0);
		ReqStart = Ptr;
		if (OverrideMsgId == MsgId)
			MsgId++;
	}

	void Send()
	{
		SetWord(ReqLength, Ptr - ReqStart);
		L2CAPMessage::Send();
	}

private:
	uint8_t *ReqStart;
	uint8_t *ReqLength;
	static uint8_t MsgId;
};

class WiimoteMessage : public L2CAPMessage
{
public:
	WiimoteMessage(uint8_t ReportNum, uint16_t DCID, uint16_t ACLHandle)
		: L2CAPMessage(DCID, ACLHandle)
	{
		AddByte(0xA2);
		AddByte(ReportNum);
	}

};

uint8_t L2CAPRequest::MsgId = 0;

/////////////////////////////////////////////////////////////////////////////////////////////
// Message Readers
/////////////////////////////////////////////////////////////////////////////////////////////

class MessageParser
{
public:
	MessageParser(uint8_t *Msg, uint16_t InLength)
	{
		VERBOSE_PRINT("** Packet start **\n");
		Ptr = Msg;
		End = Ptr + InLength;
	}

	~MessageParser()
	{
		if (Ptr < End)
			VERBOSE_PRINT("Message parser left with %d bytes in it\n", End - Ptr);
		VERBOSE_PRINT("** Packet end **\n\n");
	}

	inline uint16_t ReadByte(const char *DebugText)
	{
		uint8_t Ret = *(Ptr++);
		VERBOSE_PRINT("B: %s = %x (%d)\n", DebugText, Ret, Ret);
		return Ret;
	}

	inline uint16_t ReadWord(const char *DebugText)
	{
		uint16_t Ret = *(Ptr++);
		Ret |= *(Ptr++) << 8;
		VERBOSE_PRINT("W: %s = %x (%d)\n", DebugText, Ret, Ret);
		return Ret;
	}

	inline uint32_t ReadTri(const char *DebugText)
	{
		uint32_t Ret = *(Ptr++);
		Ret |= *(Ptr++) << 8;
		Ret |= *(Ptr++) << 16;
		VERBOSE_PRINT("W: %s = %x (%d)\n", DebugText, Ret, Ret);
		return Ret;
	}

	inline uint32_t ReadQuad(const char *DebugText)
	{
		uint32_t Ret = *(Ptr++);
		Ret |= *(Ptr++) << 8;
		Ret |= *(Ptr++) << 16;
		Ret |= *(Ptr++) << 24;
		VERBOSE_PRINT("W: %s = %x (%d)\n", DebugText, Ret, Ret);
		return Ret;
	}

	inline void ReadData(const char *DebugText, uint8_t *Data, uint16_t Size)
	{
		memcpy(Data, Ptr, Size);
		Ptr += Size;
		VERBOSE_PRINT("D: %s (Size:%d) =", DebugText, Size);
		for (uint16_t i = 0; i < Size; i++)
		{
			VERBOSE_PRINT(" %02x", Data[i]);
		}
		VERBOSE_PRINT("\n");
	}

	void DumpRemaining(const char *DebugText)
	{
		int SizeLeft = End - Ptr;
		if (SizeLeft)
		{
			VERBOSE_PRINT("N: %s (Size:%d) =", DebugText, SizeLeft);
			for (uint16_t i = 0; i < SizeLeft; i++)
			{
				VERBOSE_PRINT(" %02x", Ptr[i]);
			}
			VERBOSE_PRINT("\n");
			Ptr = End;
		}
	}

	void ReadACLHeader()
	{
		ACLHandle = ReadWord("ACLHandle") & 0xFFF;
		ACLLength = ReadWord("ACLLength");
		check(ACLLength == End - Ptr);
	}

	void ReadL2CAPHeader()
	{
		L2CAPLength = ReadWord("L2CAPLength");
		L2CAPChannelId = ReadWord("L2CAPChannelId");
		check(L2CAPLength == ACLLength - 4);
	}

	void ReadL2CAPRequestHeader()
	{
		L2CAPCode = ReadByte("L2CAPCode");
		L2CAPMsgId = ReadByte("L2CAPMsgID");
		L2CAPRequestLength = ReadWord("RequestLength");
		check(L2CAPRequestLength == L2CAPLength - 4);
	}

public:
	uint16_t ACLHandle;
	uint16_t ACLLength;
	uint16_t L2CAPLength;
	uint16_t L2CAPChannelId;
	uint8_t  L2CAPCode;
	uint8_t  L2CAPMsgId;
	uint16_t L2CAPRequestLength;

private:
	uint8_t *Ptr;
	uint8_t *End;
};

/////////////////////////////////////////////////////////////////////////////////////////////
// HCI ACL Connection
/////////////////////////////////////////////////////////////////////////////////////////////

class ACLConnection
{
public:
	ACLConnection()
	{
		bConnected = false;
		bAllocated = false;
		bWantsConnection = false;
		Handle = 0;
	}

	bool IsAllocated()
	{
		return bAllocated;
	}

	bool IsConnected()
	{
		return bAllocated && bConnected;
	}

	bool IsAlive()
	{
		return bAllocated && !bConnected && !bWantsConnection;
	}

	bool WantsConnection()
	{
		return bAllocated && bWantsConnection;
	}

	void Allocate()
	{
		bConnected = false;
		bAllocated = true;
		bWantsConnection = true;
		Handle = 0;
	}

	void Free()
	{
		bConnected = false;
		bAllocated = false;
		bWantsConnection = false;
		Handle = 0;
	}

	void RegisterConnection(uint16_t ACLHandle)
	{
		bWantsConnection = false;
		bConnected = true;
		Handle = ACLHandle;
	}

	void RegisterDisconnection()
	{
		bConnected = false;
		Handle = 0;
	}

	uint16_t GetHandle()
	{
		return Handle;
	}

private:
	bool bConnected;
	bool bAllocated;
	bool bWantsConnection;
	uint16_t Handle;
};

class HCI
{
private:
	enum LinkControl
	{
		HCI_Inquiry = 1,
		HCI_Create_Connection = 5,
		HCI_Link_Control = (1 << 10), // FIXME: Check this
	};

	enum BasebandControl
	{
		HCI_Reset = 3,
		HCI_Set_Event_Filter = 5,
		HCI_Baseband_Control = (3 << 10), // FIXME: Check this
	};

	enum Events
	{
		HCI_Inquiry_Complete = 1,
		HCI_Inquiry_Result = 2,
		HCI_Connection_Complete = 3,
		HCI_Disconnection_Complete = 5,
		HCI_QoS_Setup_Complete = 13,
		HCI_Command_Complete = 14,
		HCI_Command_Status = 15,
		HCI_Num_Completed_Packets = 19,
		HCI_Data_Buffer_Overflow = 26
	};

	enum StateEnum
	{
		STATE_STARTUP,
		STATE_READY,
		STATE_INQUIRYING,
		STATE_CONNECTING
	};

	enum
	{
		kMaxACLConnections = 16
	};

private:
	void SetState(StateEnum InState)
	{
		VERBOSE_PRINT("Changing HCI state to %d\n", InState);
		State = InState;
	}

	ACLConnection* FindConnection(uint16_t Handle)
	{
		for (int i = 0; i < kMaxACLConnections; i++)
		{
			if (Connections[i].IsConnected() && Handle == Connections[i].GetHandle())
			{
				return &Connections[i];
			}
		}
		printf("ERROR: HCIManager Can't find allocated connection with handle %x", Handle);
		return nullptr;
	}

	void Reset()
	{
		HCICommand Cmd(HCI_Reset | HCI_Baseband_Control);
		Cmd.Send();
	}

	void SetFilter()
	{
		HCICommand Cmd(HCI_Set_Event_Filter | HCI_Baseband_Control);
		Cmd.AddByte(1); // Filter inquiry results
		Cmd.AddByte(1); // Search for only this of class
		Cmd.AddTri(0x000500); // Class we want to look for
		Cmd.AddTri(~0x00200C); // Mask out bits which differ for Wiimote + Wiimote Plus
		Cmd.Send();
	}

	void Inquire()
	{
		HCICommand Cmd(HCI_Inquiry | HCI_Link_Control);
		Cmd.AddTri(0x9E8B33); // General Inquiry Access Code (GIAC) LAP
		Cmd.AddByte(8); // Time to search: N*1.28s
		Cmd.AddByte(1); // Num_Responses before halt
		Cmd.Send();
	}

	void Connect(uint8_t *BluetoothAddress)
	{
		HCICommand Cmd(HCI_Create_Connection | HCI_Link_Control);
		Cmd.AppendData(BluetoothAddress, 6);
		Cmd.AddWord(0xCC18); // Packet_Type: Allow all DH+DM
		Cmd.AddByte(1); // Page_Scan_Repetition_Mode: Optional Page Scan Mode I
		Cmd.AddByte(0); // Reserved
		Cmd.AddWord(0); // Clock_Offset: Invalid
		Cmd.AddByte(0); // Allow_Role_Switch: No
		Cmd.Send();
	}

	bool PumpMessages()
	{
		uint8_t Message[128];
		uint16_t Length = ESPBluetooth::GetEventPacket(Message, sizeof(Message));
		if (Length == 0)
			return false;
		MessageParser Parser(Message, Length); // Supplies debugging helpers
		int Code = Parser.ReadByte("HCIEvent");
		int Size = Parser.ReadByte("HCISize");
		check(Size == Length - 2);
		switch (Code)
		{
		case HCI_Inquiry_Complete:
		{
			check(State == STATE_INQUIRYING || State == STATE_CONNECTING);
			Parser.ReadByte("Status");
			if (State == STATE_INQUIRYING) // If we didn't find anything then just go around again
				SetState(STATE_READY);
			break;
		}

		case HCI_Inquiry_Result:
		{
			check(State == STATE_INQUIRYING);
			uint8_t BluetoothAddress[6];
			uint8_t NumResponses = Parser.ReadByte("Num_Responses");
			if (NumResponses > 0)
			{
				check(NumResponses == 1); // We requested only a maximum of 1
				Parser.ReadData("BD_ADDR", BluetoothAddress, sizeof(BluetoothAddress));
				Parser.ReadByte("RepetitionMode");
				Parser.ReadByte("Reserved");
				Parser.ReadByte("Reserved");
				Parser.ReadTri("Class");
				Parser.ReadWord("ClockOffset");
				Connect(BluetoothAddress);
				SetState(STATE_CONNECTING);
			}
			break;
		}

		case HCI_Connection_Complete:
		{
			check(State == STATE_CONNECTING);
			uint8_t BluetoothAddress[6];
			uint8_t Result = Parser.ReadByte("ErrorCode");
			uint16_t Handle = Parser.ReadWord("Handle");
			Parser.ReadData("BD_ADDR", BluetoothAddress, sizeof(BluetoothAddress));
			Parser.ReadByte("LinkType");
			Parser.ReadByte("Encryption");
			if (Result == 0)
			{
				for (int i = 0; i < kMaxACLConnections; i++)
				{
					if (Connections[i].WantsConnection())
					{
						Connections[i].RegisterConnection(Handle);
						break;
					}
				}
			}
			SetState(STATE_READY);
			break;
		}

		case HCI_Disconnection_Complete:
		{
			uint8_t Result = Parser.ReadByte("ErrorCode");
			uint16_t Handle = Parser.ReadWord("Handle");
			Parser.ReadByte("Reason");
			if (Result == 0)
			{
				ACLConnection *Connection = FindConnection(Handle);
				if (Connection) // Set handle as disconnected
				{
					Connection->RegisterDisconnection();
				}
			}
			break;
		}

		case HCI_QoS_Setup_Complete:
		{
			Parser.ReadByte("Status");
			Parser.ReadWord("Handle");
			Parser.ReadByte("Flags");
			Parser.ReadByte("Service_Type");
			Parser.ReadQuad("Token_Rate");
			Parser.ReadQuad("Peak_Bandwidth");
			Parser.ReadQuad("Latency");
			Parser.ReadQuad("Delay_Variation");
			break;
		}

		case HCI_Command_Complete:
		{
			Parser.ReadByte("NumPackets"); // Technically if zero we shouldn't send any more packets
			Parser.ReadWord("CommandOpCode");
			Parser.DumpRemaining("Return");
			break;
		}

		case HCI_Command_Status:
		{
			Parser.ReadByte("Status");
			Parser.ReadByte("NumPackets");
			Parser.ReadWord("CommandOpCode");
			break;
		}

		case HCI_Num_Completed_Packets:
		{
			uint8_t NumHandles = Parser.ReadByte("NumHandles");
			for (uint8_t i = 0; i < NumHandles; i++)
			{
				Parser.ReadWord("Handle");
				Parser.ReadWord("NumPackets");
			}
			break;
		}

		case HCI_Data_Buffer_Overflow:
		{
			Parser.ReadByte("LinkType");
			printf("ERROR: Data buffer overflowed\n");
		}

		default:
			printf("ERROR: Unknown Event: %x\n", Code);
			break;
		}
		return true;
	}

public:
	HCI()
	{
		SetState(STATE_STARTUP);
	}

	void Tick()
	{
		if (State == STATE_STARTUP)
		{
			Reset();
			SetState(STATE_READY);
			SetFilter();
		}
		else if (State == STATE_READY) // Just keep inquirying and trying to find new Wiimotes
		{
			bool bAnyWantsConnecting = false;
			for (int i = 0; i < kMaxACLConnections; i++)
			{
				bAnyWantsConnecting |= Connections[i].WantsConnection();
			}
			if (bAnyWantsConnecting)
			{
				Inquire();
				SetState(STATE_INQUIRYING);
			}
		}
		while (PumpMessages());
	}

	ACLConnection* AllocateConnection()
	{
		for (int i = 0; i < kMaxACLConnections; i++)
		{
			if (!Connections[i].IsAllocated())
			{
				Connections[i].Allocate();
				return &Connections[i];
			}
		}
		return nullptr;
	}

	void FreeConnection(ACLConnection *Connection)
	{
		if (Connection->IsConnected())
		{
			// TODO: Request disconnection
		}
		Connection->Free();
	}

private:
	StateEnum State;

	ACLConnection Connections[kMaxACLConnections];
};

HCI HCIManager;

/////////////////////////////////////////////////////////////////////////////////////////////
// L2CAP Connection
/////////////////////////////////////////////////////////////////////////////////////////////

class IL2CAPMessageListener
{
public:
	virtual void RecieveData(class L2CAPConnection *Connection, MessageParser &Parser) = 0;
};

class L2CAPConnection
{
protected:
	enum StateEnum
	{
		STATE_CLOSED,
		STATE_WAIT_CONNECT,
		STATE_CONFIG,
		STATE_OPEN,
		STATE_WAIT_DISCONNECT
	};

	void SendConnectRequest(uint16_t PSM)
	{
		L2CAPRequest Req(L2CAP_CONNECTION_REQUEST, ACL->GetHandle());
		Req.AddWord(PSM);
		Req.AddWord(SCID);
		Req.Send();
		SetState(STATE_WAIT_CONNECT);
	}

	void SendConfigurationRequest(bool bChangeMTU)
	{
		L2CAPRequest Req(L2CAP_CONFIGURATION_REQUEST, ACL->GetHandle());
		Req.AddWord(DCID);
		Req.AddWord(0); // Flags
		if (bChangeMTU)
		{
			Req.AddByte(1); // Configure MTU (Seems to be what the Wii does from captures)
			Req.AddByte(2); // Length 2
			Req.AddWord(185); // 185 bytes
		}
		Req.Send();
		SetState(STATE_CONFIG);
	}

	void SendConfigurationResponse(bool bChangeMTU, uint16_t MsgId)
	{
		L2CAPRequest Req(L2CAP_CONFIGURATION_RESPONSE, ACL->GetHandle(), MsgId);
		Req.AddWord(DCID);
		Req.AddWord(0); // Flags
		Req.AddWord(0); // Result: Success
		if (bChangeMTU)
		{
			Req.AddByte(1); // Configure MTU (Seems to be what the Wii does from captures)
			Req.AddByte(2); // Length 2
			Req.AddWord(185); // 185 bytes
		}
		Req.Send();
		SetState(STATE_OPEN);
	}

	void SendDisconnectRequest()
	{
		L2CAPRequest Req(L2CAP_DISCONNECTION_REQUEST, ACL->GetHandle());
		Req.AddWord(DCID);
		Req.AddWord(SCID);
		Req.Send();
		SetState(STATE_WAIT_DISCONNECT);
	}

	void SendDisconnectResponse(uint16_t MsgId)
	{
		L2CAPRequest Req(L2CAP_DISCONNECTION_RESPONSE, ACL->GetHandle(), MsgId);
		Req.AddWord(DCID);
		Req.AddWord(SCID);
		Req.Send();
		SetState(STATE_CLOSED);
	}

	void SetState(StateEnum InState)
	{
		VERBOSE_PRINT("Changing L2CAP state to %d\n", InState);
		State = InState;
	}

	void CheckState(StateEnum InState)
	{
		if (State != InState)
		{
			printf("ERROR: L2CAP Not in expected state %d instead of %d\n", State, InState);
		}
	}

	void CheckState(StateEnum InState, StateEnum OrState)
	{
		if (State != InState && State != OrState)
		{
			printf("ERROR: L2CAP Not in expected state %d instead of %d or %d\n", State, InState, OrState);
		}
	}

	void ReceiveConnectionResponse(uint16_t InDCID, uint16_t Result)
	{
		CheckState(STATE_WAIT_CONNECT);
		DCID = InDCID;
		if (Result == 0) // Success
			SendConfigurationRequest(true);
		else if (Result != 1) // !Pending
			SetState(STATE_CLOSED);
	}

	void ReceiveConfigurationRequest(uint8_t MsgId)
	{
		CheckState(STATE_CONFIG, STATE_OPEN);
		SendConfigurationResponse(false, MsgId); // Just accept whatever is proposed
	}

	void ReceiveConfigurationResponse(uint16_t Result)
	{
		CheckState(STATE_CONFIG);
		if (Result != 0) // Our config change was rejected so just disconnect
			SendDisconnectRequest();
	}

	void ReceiveDisconnectionRequest(uint8_t MsgId)
	{
		CheckState(STATE_OPEN, STATE_CONFIG);
		SendDisconnectResponse(MsgId);
		SetState(STATE_CLOSED);
	}

	void ReceiveDisconnectionResponse()
	{
		CheckState(STATE_WAIT_DISCONNECT);
		SetState(STATE_CLOSED);
	}

	void ReadDataPacket(MessageParser &Parser)
	{
		if (Listener)
		{
			Listener->RecieveData(this, Parser);
			Parser.DumpRemaining("ListenerLeftOverData");
		}
		else
		{
			Parser.DumpRemaining("NoListenerRegistered");
		}
	}

public:

	L2CAPConnection()
	{
		bAllocated = false;
		DCID = 0;
		SCID = 0;
		ACL = nullptr;
		Listener = nullptr;
		State = STATE_CLOSED;
	}

	~L2CAPConnection()
	{
		CheckState(STATE_CLOSED);
	}

	bool IsConnected()
	{
		return bAllocated && State == STATE_OPEN && ACL->IsConnected();
	}

	bool IsAlive()
	{
		return bAllocated && State != STATE_CLOSED && ACL->IsAlive();
	}

	void Allocate(ACLConnection* InACL, uint16_t PSM, uint16_t InSCID)
	{
		bAllocated = true;
		SCID = InSCID;
		ACL = InACL;
		CheckState(STATE_CLOSED);
		SendConnectRequest(PSM);
	}

	void SetListener(IL2CAPMessageListener *MessageListener)
	{
		Listener = MessageListener;
	}

	void Free()
	{
		if (State != STATE_CLOSED)
		{
			SendDisconnectRequest();
		}
		Listener = nullptr;
		bAllocated = false;
	}

	uint16_t GetDCID()
	{
		return DCID;
	}

protected:
	bool bAllocated;
	StateEnum State;
	uint16_t DCID;
	uint16_t SCID;
	ACLConnection *ACL;
	IL2CAPMessageListener *Listener;

	friend L2CAP;
};

class L2CAP
{
private:
	enum
	{
		kMaxNumConnections = 32
	};

	L2CAPConnection* FindConnection(uint16_t Handle, uint16_t SCID)
	{
		for (int i = 0; i < kMaxNumConnections; i++)
		{
			if (Connections[i].bAllocated && Connections[i].ACL->GetHandle() == Handle && Connections[i].SCID == SCID)
			{
				return &Connections[i];
			}
		}
		printf("ERROR: Failed to find SCID: 0x%x with handle 0x%x", SCID, Handle);
		return nullptr;
	}

	void SendInformationResponse(uint16_t InfoType, uint16_t MsgId, uint16_t ACLHandle)
	{
		L2CAPRequest Req(L2CAP_INFORMATION_RESPONSE, ACLHandle, MsgId);
		Req.AddWord(InfoType);
		Req.AddWord(1); // Not supported (Wii returns it supports bi-directional QoS)
		Req.Send();
	}

	bool PumpMessages()
	{
		uint8_t Message[128];
		uint16_t Length = ESPBluetooth::GetACLPacket(Message, sizeof(Message));
		if (Length == 0)
			return false;
		MessageParser Parser(Message, Length); // Supplies debugging helpers
		Parser.ReadACLHeader();
		Parser.ReadL2CAPHeader();
		if (Parser.L2CAPChannelId == L2CAP_SIGNALING_CHANNEL)
		{
			Parser.ReadL2CAPRequestHeader();
			switch (Parser.L2CAPCode)
			{
			case L2CAP_COMMAND_REJECT_RESPONSE:
			{
				Parser.ReadWord("Reason");
				break;
			}

			case L2CAP_CONNECTION_RESPONSE:
			{
				uint16_t DCID = Parser.ReadWord("DestCID");
				uint16_t SCID = Parser.ReadWord("SourceCID");
				uint16_t Result = Parser.ReadWord("Result");
				Parser.ReadWord("Status");
				L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, SCID);
				if (Connection)
					Connection->ReceiveConnectionResponse(DCID, Result);
				break;
			}

			case L2CAP_CONFIGURATION_REQUEST:
			{
				uint16_t SCID = Parser.ReadWord("DestCID");
				Parser.ReadWord("Flags");
				for (int i = 0; i < (Parser.L2CAPRequestLength - 4) / 4; i++)
					Parser.ReadQuad("Options");
				L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, SCID);
				if (Connection)
					Connection->ReceiveConfigurationRequest(Parser.L2CAPMsgId);
				break;
			}

			case L2CAP_CONFIGURATION_RESPONSE:
			{
				uint16_t SCID = Parser.ReadWord("SrcCID");
				Parser.ReadWord("Flags");
				uint16_t Result = Parser.ReadWord("Result");
				for (int i = 0; i < (Parser.L2CAPRequestLength - 6) / 4; i++)
					Parser.ReadQuad("Options");
				L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, SCID);
				if (Connection)
					Connection->ReceiveConfigurationResponse(Result);
				break;
			}

			case L2CAP_DISCONNECTION_REQUEST:
			{
				uint16_t SCID = Parser.ReadWord("DestCID");
				Parser.ReadWord("SrcCID");
				L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, SCID);
				if (Connection)
					Connection->ReceiveDisconnectionRequest(Parser.L2CAPMsgId);
				break;
			}

			case L2CAP_DISCONNECTION_RESPONSE:
			{
				Parser.ReadWord("DestCID");
				uint16_t SCID = Parser.ReadWord("SrcCID");
				L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, SCID);
				if (Connection)
					Connection->ReceiveDisconnectionResponse();
				break;
			}

			case L2CAP_INFORMATION_REQUEST:
			{
				uint16_t InfoType = Parser.ReadWord("InfoType");
				SendInformationResponse(InfoType, Parser.L2CAPMsgId, Parser.ACLHandle);
				break;
			}

			default:
				printf("ERROR: Unknown Packet: %x\n", Parser.L2CAPCode);
				break;
			}
		}
		else
		{
			L2CAPConnection *Connection = FindConnection(Parser.ACLHandle, Parser.L2CAPChannelId);
			if (Connection)
				Connection->ReadDataPacket(Parser);
		}
		return true;
	}

public:

	L2CAP()
	{
	}

	void Tick()
	{
		while (PumpMessages());
	}

	L2CAPConnection* AllocateChannel(ACLConnection* ACL, uint16_t PSM, uint16_t SCID)
	{
		check(ACL->IsConnected());
		for (int i = 0; i < kMaxNumConnections; i++)
		{
			if (!Connections[i].bAllocated)
			{
				Connections[i].Allocate(ACL, PSM, SCID);
				return &Connections[i];
			}
		}
		printf("ERROR: L2CAPManager No free channels left\n");
		return nullptr;
	}

	void FreeChannel(L2CAPConnection *Connection)
	{
		Connection->Free();
	}

private:
	L2CAPConnection Connections[kMaxNumConnections];
};

L2CAP L2CAPManager;

/////////////////////////////////////////////////////////////////////////////////////////////
// Wiimote Specific
/////////////////////////////////////////////////////////////////////////////////////////////

class WiimoteBluetoothConnection : public IL2CAPMessageListener, public IWiimote
{
private:
	enum
	{
		CONTROL_PSM = 0x11,
		DATA_PSM = 0x13,
		SRC_CONTROL_CID = 0x48, // From Wii capture
		SRC_DATA_CID = 0x49 // From Wii capture
	};

	enum StateEnum
	{
		STATE_CLOSED,
		STATE_WAITING_FOR_ACL,
		STATE_WAITING_FOR_L2CAP,
		STATE_SET_SENSITIVITY_1,
		STATE_SET_SENSITIVITY_2,
		STATE_SET_IR_MODE,
		STATE_CAMERA_ENABLE,
		STATE_SET_REPORT_MODE,
		STATE_OPEN
	};

public:
	WiimoteBluetoothConnection()
	{
		Reset();
	}

	void Open(uint8_t LEDs)
	{
		check(State == STATE_CLOSED);
		ACL = HCIManager.AllocateConnection();
		StartingLEDs = LEDs;
		SetState(STATE_WAITING_FOR_ACL);
	}

	void SetState(StateEnum InState)
	{
		VERBOSE_PRINT("Changing Wiimote connection state to %d\n", InState);
		State = InState;
	}

	void Tick()
	{
		switch (State)
		{
		case STATE_CLOSED:
			break;
		case STATE_WAITING_FOR_ACL:
			if (ACL->IsConnected())
			{
				ControlPipe = L2CAPManager.AllocateChannel(ACL, CONTROL_PSM, SRC_CONTROL_CID);
				DataPipe = L2CAPManager.AllocateChannel(ACL, DATA_PSM, SRC_DATA_CID);
				DataPipe->SetListener(this);
				SetState(STATE_WAITING_FOR_L2CAP);
			}
			break;
		case STATE_WAITING_FOR_L2CAP:
			if (ControlPipe->IsConnected() && DataPipe->IsConnected())
			{
				uint8_t CameraEnable = 0x08;
				SetPlayerLEDs(StartingLEDs);
				WriteSingleByteReport(WIIMOTE_REPORT_IR_ENABLE_1, 0x04);
				WriteSingleByteReport(WIIMOTE_REPORT_IR_ENABLE_2, 0x04);
				WriteToRegister(0xB00030, &CameraEnable, 1);
				SetState(STATE_SET_SENSITIVITY_1);
			}
			break;
		case STATE_SET_SENSITIVITY_1:
			if (WriteAck == WriteReq)
			{
				uint8_t SensitivityBlock1[] = { 0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xAA, 0x00, 0x64 };
				WriteToRegister(0xB00000, SensitivityBlock1, sizeof(SensitivityBlock1));
				SetState(STATE_SET_SENSITIVITY_2);
			}
			break;
		case STATE_SET_SENSITIVITY_2:
			if (WriteAck == WriteReq)
			{
				uint8_t SensitivityBlock2[] = { 0x63, 0x03 };
				WriteToRegister(0xB0001A, SensitivityBlock2, sizeof(SensitivityBlock2));
				SetState(STATE_SET_IR_MODE);
			}
			break;
		case STATE_SET_IR_MODE:
			if (WriteAck == WriteReq)
			{
				uint8_t ExtendedMode = 0x03;
				WriteToRegister(0xB00033, &ExtendedMode, 1);
				SetState(STATE_CAMERA_ENABLE);
			}
			break;
		case STATE_CAMERA_ENABLE:
			if (WriteAck == WriteReq)
			{
				uint8_t CameraEnable = 0x08;
				WriteToRegister(0xB00030, &CameraEnable, 1);
				SetState(STATE_SET_REPORT_MODE);
			}
			break;
		case STATE_SET_REPORT_MODE:
			if (WriteAck == WriteReq)
			{
				RequestReportMode(WIIMOTE_REPORT_CORE_BUTTONS_ACC_IR12);
				SetState(STATE_OPEN);
			}
			break;
		case STATE_OPEN:
			break;
		}
	}

	void Close()
	{
		if (ControlPipe)
			L2CAPManager.FreeChannel(ControlPipe);
		if (DataPipe)
			L2CAPManager.FreeChannel(DataPipe);
		if (ACL)
			HCIManager.FreeConnection(ACL);
		Reset();
	}

	bool IsConnected()
	{
		if (ControlPipe && DataPipe)
		{
			if (ControlPipe->IsConnected() && DataPipe->IsConnected())
			{
				return State == STATE_OPEN;
			}
		}
		return false;
	}

	bool IsAlive()
	{
		if (ControlPipe && DataPipe)
		{
			return (ControlPipe->IsAlive() && DataPipe->IsAlive());
		}
		else
		{
			return ACL->IsAlive();
		}
	}

	virtual WiimoteData* GetData()
	{
		return &Data;
	}

	virtual void SetPlayerLEDs(uint8_t LEDs)
	{
		WriteSingleByteReport(WIIMOTE_REPORT_SET_LEDS, (uint8_t)(LEDs << 4));
	}

	virtual void RecieveData(L2CAPConnection *Connection, MessageParser &Parser)
	{
		check(Connection == DataPipe);
		uint8_t Magic = Parser.ReadByte("WiimoteMagic (Should be 0xA1)");
		if (Magic != 0xA1)
			return;
		uint8_t ReportCode = Parser.ReadByte("ReportCode");
		switch (ReportCode)
		{
		case WIIMOTE_REPORT_STATUS_INFORMATION:
		{
			uint16_t Buttons = Parser.ReadWord("Buttons");
			uint8_t LEDAndFlags = Parser.ReadByte("LEDAndFlags");
			Parser.ReadWord("Reserved? (Should be 0)");
			uint8_t BatteryLevel = Parser.ReadByte("BatteryLevel");
			Data.Buttons = Buttons & ~0x6060; // Remove acceleration lower bits
			Data.BatteryLevel = BatteryLevel;
			Data.LEDs = LEDAndFlags >> 4;
			if (State == STATE_OPEN)
				State = STATE_SET_REPORT_MODE;
			break;
		}
		case WIIMOTE_REPORT_READ_MEMORY:
		{
			uint16_t Buttons = Parser.ReadWord("Buttons");
			uint8_t SizeAndErrorFlags = Parser.ReadByte("SizeAndErrorFlags");
			uint8_t MemoryData[16];
			Parser.ReadWord("MemoryOffset");
			Parser.ReadData("MemoryData", MemoryData, (SizeAndErrorFlags >> 4) + 1);
			Data.Buttons = Buttons & ~0x6060; // Remove acceleration lower bits
			break;
		}
		case WIIMOTE_REPORT_ACKNOWLEDGE:
		{
			uint16_t Buttons = Parser.ReadWord("Buttons");
			uint8_t ReportAck = Parser.ReadByte("ReportNum");
			uint8_t ErrorCode = Parser.ReadByte("ErrorCode");
			Data.Buttons = Buttons & ~0x6060; // Remove acceleration lower bits
			if (ReportAck == WIIMOTE_REPORT_WRITE_MEMORY)
				WriteAck++;
			if (ErrorCode != 0)
				printf("ERROR: From sent report: %x (Error=%x)\n", ReportAck, ErrorCode);
			break;
		}
		case WIIMOTE_REPORT_CORE_BUTTONS:
		{
			uint16_t Buttons = Parser.ReadWord("Buttons");
			Data.Buttons = Buttons & ~0x6060; // Remove acceleration lower bits
			break;
		}
		case WIIMOTE_REPORT_CORE_BUTTONS_ACC_IR12:
		{
			uint16_t Buttons = Parser.ReadWord("Buttons");
			uint8_t AccelX = Parser.ReadByte("AccelX");
			uint8_t AccelY = Parser.ReadByte("AccelY");
			uint8_t AccelZ = Parser.ReadByte("AccelZ");
			uint32_t Spot[4];
			Spot[0] = Parser.ReadTri("Spot0");
			Spot[1] = Parser.ReadTri("Spot1");
			Spot[2] = Parser.ReadTri("Spot2");
			Spot[3] = Parser.ReadTri("Spot3");
			Data.Buttons = Buttons & ~0x6060; // Remove acceleration lower bits
			Data.AccelX = (AccelX << 2) + ((Buttons >> 5) & 3);
			Data.AccelY = (AccelY << 2) + ((Buttons >> 12) & 2);
			Data.AccelZ = (AccelZ << 2) + ((Buttons >> 13) & 2);
			for (int i = 0; i < 4; i++)
			{
				Data.IRSpot[0].X = ((Spot[0] >> 0) & 0xFF) + ((Spot[0] >> 12) & 0x300);
				Data.IRSpot[0].Y = ((Spot[0] >> 8) & 0xFF) + ((Spot[0] >> 14) & 0x300);
				Data.IRSpot[0].Size = ((Spot[0] >> 16) & 0xF);
				Data.IRSpot[0].Size |= (Data.IRSpot[0].Size << 4); // Extend to 8-bit
			}
			Data.FrameNumber++;
			break;
		}
		default:
		{
			printf("ERROR: Unhandled report: %x\n", ReportCode);
			Parser.DumpRemaining("UnkownReportCode");
			break;
		}
		}
	}

protected:
	void Reset()
	{
		ACL = nullptr;
		ControlPipe = nullptr;
		DataPipe = nullptr;
		memset(&Data, 0, sizeof(Data));
		WriteReq = 0;
		WriteAck = 0;
		State = STATE_CLOSED;
	}

	void WriteSingleByteReport(uint8_t ReportNum, uint8_t Data)
	{
		WiimoteMessage Msg(ReportNum, DataPipe->GetDCID(), ACL->GetHandle());
		Msg.AddByte(Data);
		Msg.Send();
	}

	void RequestReportMode(uint8_t ReportMode)
	{
		WiimoteMessage Msg(WIIMOTE_REPORT_REQUEST_REPORT, DataPipe->GetDCID(), ACL->GetHandle());
		Msg.AddByte(0x00); // Non-continuous
		Msg.AddByte(ReportMode);
		Msg.Send();
	}

	void WriteToRegister(uint32_t RegisterNum, uint8_t *Data, uint8_t DataSize)
	{
		if (DataSize > 16)
		{
			printf("ERROR: Incorrect size for write %d\n", DataSize);
			return;
		}
		WiimoteMessage Msg(WIIMOTE_REPORT_WRITE_MEMORY, DataPipe->GetDCID(), ACL->GetHandle());
		Msg.AddByte(0x04); // Control registers
		Msg.AddTriBigEndian(RegisterNum); // Offset
		Msg.AddByte(DataSize); // Size
		Msg.AppendData(Data, DataSize); // Data
		for (uint8_t i = DataSize; i < 16; i++)
		{
			Msg.AddByte(0x00); // Padding
		}
		Msg.Send();
		WriteReq++;
	}

protected:
	StateEnum State;
	WiimoteData Data;
	ACLConnection* ACL;
	L2CAPConnection* ControlPipe;
	L2CAPConnection* DataPipe;
	uint32_t WriteReq;
	uint32_t WriteAck;
	uint8_t StartingLEDs;
};

/////////////////////////////////////////////////////////////////////////////////////////////
// Public interfrace
/////////////////////////////////////////////////////////////////////////////////////////////

WiimoteManager::WiimoteManager()
{
}

void WiimoteManager::Init()
{
	ESPBluetooth::InitBluetooth();
}

void WiimoteManager::DeInit()
{
	ESPBluetooth::DeInitBluetooth();
}

IWiimote* WiimoteManager::CreateNewWiimote()
{
	for (int i = 0; i < kMaxWiimotes; i++)
	{
		if (!Wiimotes[i])
		{
			Wiimotes[i] = new WiimoteBluetoothConnection();
			Wiimotes[i]->Open(1<<i);
			return Wiimotes[i];
		}
	}
	return nullptr;
}

void WiimoteManager::Tick()
{
	HCIManager.Tick();
	L2CAPManager.Tick();

	for (int i = 0; i < kMaxWiimotes; i++)
	{
		if (Wiimotes[i])
		{
			Wiimotes[i]->Tick();
		}
	}
}

WiimoteManager GWiimoteManager;

