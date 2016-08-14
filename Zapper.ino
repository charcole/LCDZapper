#define MICROSECONDS_TO_CYCLES(x) (16*(x))

int led=0;
unsigned short pointerX=230;
unsigned short pointerY=120;
unsigned char pointerButton=0;
unsigned char difficulty=0;
unsigned char showPointer=1;
unsigned char fibbles[4];
unsigned char fibbleOdd=0;
unsigned char fibbleMask=0;
unsigned char fibbleSwitches=0;

void setup() {
  pinMode(0, INPUT); // Serial recieve
  pinMode(6, INPUT); // Sync
  pinMode(5, INPUT); // White
  pinMode(4, OUTPUT); // Photosensor
  pinMode(3, OUTPUT); // Dimmer
  pinMode(13, OUTPUT); // LED
  pinMode(17, INPUT_PULLUP); // Button
  pinMode(18, OUTPUT); // Trigger
  digitalWrite(3, HIGH);
  digitalWrite(18, HIGH);
  Serial.begin(9600);
  UCSR0B&=~((1<<7)|(1<<5)); // Clear RX complete + UDR empty interrupt
}

void ProcessLine(short delayValue, short offset)
{
  unsigned char counter=4; // Make sure sync has settled at high level
  unsigned char localdifficulty=difficulty-offset;
  unsigned char width=localdifficulty+1;
  delayValue-=9+(localdifficulty*5)/2; // 9 cycles showing cursor preamble + 2.5 cycles for half check interval
  if (delayValue<MICROSECONDS_TO_CYCLES(6)) // Don't go into back porch
    delayValue=MICROSECONDS_TO_CYCLES(6);
  else if (delayValue>MICROSECONDS_TO_CYCLES(6+52)-localdifficulty*5+18) // Don't go off right of screen
    delayValue=MICROSECONDS_TO_CYCLES(6+52)-localdifficulty*5+12;
  unsigned char lowBits=delayValue&3;
  unsigned char fours=(delayValue>>2)-1;
  unsigned char outdim=PORTD;
  unsigned char out=outdim|(1<<PORTD4);
  unsigned char dim=0;
  if (showPointer)
  {
    dim=(1<<PORTD3);
    outdim&=~(1<<PORTD3);
  }
  asm volatile
  (
    "      CLI\n"
    "LOOPA:\n"
    "      SBIS %6, %7\n"  // Wait for sync high
    "      RJMP LOOPA\n"
    "      DEC %2\n"
    "      BRNE LOOPA\n"
    "LOOPB:\n"
    "      SBIC %6, %7\n"  // Wait for sync low
    "      RJMP LOOPB\n"
    "      SBRS %0, 1\n"
    "      RJMP SKIP2\n"
    "      NOP\n"
    "      NOP\n"
    "      NOP\n"
    "SKIP2:\n"
    "      SBRC %0, 0\n"
    "      RJMP SKIP1\n"
    "SKIP1:\n"
    "      NOP\n"
    "      DEC %1\n"
    "      BRNE SKIP1\n"
    "      OUT %4, %9\n"      // Show left cursor
    "      ADD %9, %11\n"
    "      OUT %4, %9\n"
    "      NOP\n"             // Balance with right cursor (time to check==time from check)
    "      NOP\n"
    "      NOP\n"
    "      NOP\n"
    "      NOP\n"
    "      NOP\n"
    "RETESTA:\n"
    "      SBIC %6, %8\n"
    "      OUT %4, %10\n"
    "      DEC %3\n"
    "      BRNE RETESTA\n"
    "      IN %9, %4\n"      // Read back in case we set the light sensor pulse
    "      SUB %9, %11\n"
    "      OUT %4, %9\n"      // Show right cursor
    "      ADD %9, %11\n"
    "      OUT %4, %9\n"
    "      SEI\n"
    : "+r" (lowBits), "+r" (fours), "+r" (counter), "+r" (width)
    : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD4), "I" (_SFR_IO_ADDR(PIND)), "I" (PIND6), "I" (PIND5), "r" (outdim), "r" (out), "r" (dim)
  );
}

short GetSyncTime()
{
  short syncTime = 0;
  asm volatile
  (
    "      CLI\n"
    "LOOP1:\n" // Wait for sync high
    "      SBIS %1, %2\n"
    "      RJMP LOOP1\n"
    "LOOP2:\n" // Wait for sync low
    "      ADIW %0, 1\n"   // 2 cycles
    "      SBIC %1, %2\n"  // 1 cycle when false
    "      RJMP LOOP2\n" // 2 cycles
    "      SEI\n"
    : "+r" (syncTime)
    : "I" (_SFR_IO_ADDR(PIND)), "I" (PIND6)
  );
  return syncTime;
}

unsigned char ReadFromSerial()
{
  return UDR0;
}

void PollSerial()
{
  if (UCSR0A&(1<<7)) // New serial data available
  {
    unsigned char serial=ReadFromSerial(); // Read serial data
    unsigned char num=serial>>6;
    unsigned char odd=(serial>>5)&1;
    fibbles[num]=serial&0x1F;
    if (odd!=fibbleOdd)
    {
      fibbleOdd=odd;
      fibbleMask=0;
      fibbleSwitches=0;
    }
    fibbleMask|=(1<<num);
    if (fibbleMask==0xF)
    {
      unsigned short x=fibbles[0]+(fibbles[1]<<5);
      if (x==960) // Special code for toggle display
      {
        if (!fibbleSwitches)
        {
          showPointer=!showPointer;
          fibbleSwitches=1;
        }
      }
      else if (x==961)
      {
        if (!fibbleSwitches)
        {
          difficulty++;
          if (difficulty>16)
            difficulty=0;
          fibbleSwitches=1;
        }
      }
      else
      {
        pointerX=x;
      }
      pointerY=fibbles[2]+(fibbles[3]<<5);
      pointerButton=0;
      if (pointerY>=512)
      {
        pointerButton=1;
        pointerY-=512;
      }
      fibbleMask=0; // Save some time by not worry about it for a bit more
    }
  }
}

void WaitForVSync()
{
  short syncTime = 0;
  while (syncTime < MICROSECONDS_TO_CYCLES(15) / 5)
  {
    syncTime = GetSyncTime();
    PollSerial();
  }
  PollSerial();
}

void WaitForHSync()
{
  short syncTime = 0;
  while (syncTime < MICROSECONDS_TO_CYCLES(3) / 5 || syncTime > MICROSECONDS_TO_CYCLES(15) / 5)
  {
    syncTime = GetSyncTime();
  }
}

short CalculateDelay(short x)
{
  long cycles=x;
  cycles=MICROSECONDS_TO_CYCLES(cycles*52);
  cycles/=640;
  cycles+=MICROSECONDS_TO_CYCLES(6);
  return (short)cycles;
}

void loop()
{
  unsigned short x,y;
  unsigned short line = 0;
  bool trigger;
  WaitForVSync();
  WaitForVSync();
  WaitForVSync();
  delayMicroseconds(20); // Make sure we ignore the first pulse (we can miss it due to interrupts)
  x = pointerX;
  y = pointerY;
  trigger = !pointerButton;
  digitalWrite(18, trigger);
  y+=35; // Ignore blank lines
  x=(x<20)?20:(x>600)?600:x;
  y=(y<40)?40:(y>275)?275:y;
  digitalWrite(13,led);
  while (true)
  {
    if (line>=y-difficulty && line<=y+difficulty)
    {
      ProcessLine(CalculateDelay(x), abs((short)y-(short)line));
    }
    else
    {
      WaitForHSync();
      PollSerial();
    }
    if (line>=y+difficulty)
    {
      if (PORTD&(1<<PORTD4))
      {
        delay(1); // phosphor decay time
        digitalWrite(4, LOW);
      }
      led=1-led;
      break;
    }
    line++;
  }
}

