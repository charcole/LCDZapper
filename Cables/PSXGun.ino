// Need pullup on RX. Goes to any digital IO of LightGunVerter
// Everything else goes to PSX and shouldn't need pull ups/downs

#define IN_CMD MOSI
#define OUT_DATA MISO
#define OUT_ACK 7
#define IN_ATT SS
#define IN_CLK SCK
#define OUT_LED 2

#define CONTROLLER_DATA_SIZE 8  // From LightGunVerter
#define DATA_SIZE 4             // From PSX

#include <SPI.h>

uint8_t ReadMask[DATA_SIZE] = { 0xFF, 0xFF, 0, 0 };
uint8_t ReadExpect[DATA_SIZE] = { 0x01, 0x42, 0, 0 };
uint8_t Reply[DATA_SIZE] = { 0x41, 0x5A, 0xFF, 0xFF };

uint8_t ControllerReadIndex = 0;
uint8_t ControllerData[CONTROLLER_DATA_SIZE];
uint8_t DataIndex = 0;
bool bDataGood = true;

#define ReadAttention() (PINB&(1<<2))
#define WriteAckLow() (DDRD|=(1<<7))
#define WriteAckHigh() (DDRD&=~(1<<7))
#define WriteLEDLow() (PORTD&=~(1<<2))
#define WriteLEDHigh() (PORTD|=(1<<2))
#define DelayMicro() asm("NOP\nNOP\nNOP\nNOP\nNOP\nNOP\nNOP\nNOP\n")

inline int ReadCommand()
{
  return digitalRead(IN_CMD);
}

void setup()
{
  // LED for debugging
  pinMode(OUT_LED, OUTPUT);
  digitalWrite(OUT_LED, LOW);

  // Ack (open collector)
  pinMode(OUT_ACK, INPUT);
  digitalWrite(OUT_ACK, 0);

  // Set up SPI
  pinMode(MISO, OUTPUT);
  SPCR |= bit (SPE)|bit(DORD)|bit(CPOL)|bit(CPHA);
  SPI.attachInterrupt();

  // Set up LightGunVerter serial
  Serial.begin(9600);
}

ISR (SPI_STC_vect)
{
  uint8_t DataIn = SPDR;

  if (DataIndex < DATA_SIZE)  // Acknowledge
  {
    bDataGood &= ((DataIn & ReadMask[DataIndex]) == ReadExpect[DataIndex]);
    if (bDataGood)
    {
      SPDR = Reply[DataIndex];

      if (DataIndex == (DATA_SIZE - 1))
      {
        WriteLEDHigh();
      }
    
      WriteAckLow();
      DelayMicro();
      DelayMicro();
      DelayMicro();
      DelayMicro();
      WriteAckHigh();
    }
    else
    {
      SPDR = 0xFF;
    }
    DataIndex++;
  }
  else
  {
    SPDR = 0xFF;
  }
}

void loop()
{
  if (DataIndex != 0 && ReadAttention() != 0)
  {
    DataIndex = 0;
    bDataGood = true;
  }
  if (Serial.available())
  {
    uint8_t SerialData = Serial.read();
    if (SerialData == 0x80)
    {
      if (ControllerReadIndex == CONTROLLER_DATA_SIZE) // Last lot of serial data was good
      {
        if (ControllerData[0] == 0x80 && ControllerData[1] == 0x00) // Player 1 data
        {
          //digitalWrite(OUT_LED, (ControllerData[7]&0x1)?HIGH:LOW);
          Reply[2] = ~ControllerData[6];
          Reply[3] = ~ControllerData[7];
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

