#define MICROSECONDS_TO_CYCLES(x) (16*(x))

int led=0;

void setup() {
  pinMode(6, INPUT); // Sync
  pinMode(5, INPUT); // White
  pinMode(4, OUTPUT); // Photosensor
  pinMode(13, OUTPUT); // LED
}

short ProcessLine(short delayValue)
{
  unsigned char low=delayValue&255;
  unsigned char high=delayValue>>8;
  unsigned char counter=4; // Make sure sync has settled at high level
  if (high==0)
  {
    asm volatile
    (
      "      CLI\n"
      "LOOPA:\n"
      "      SBIS %4, %5\n"  // Wait for sync high
      "      RJMP LOOPA\n"
      "      DEC %1\n"
      "      BRNE LOOPA\n"
      "LOOPB:\n"
      "      SBIC %4, %5\n"  // Wait for sync low
      "      RJMP LOOPB\n"
      "LOOPC:\n"
      "      DEC %0\n"   // 1 cycles
      "      BRNE LOOPC\n"    // 2 cycles when true
      "      SBIC %4, %6\n"
      "      SBI %2, %3\n"
      "      SEI\n"
      : "+r" (low), "+r" (counter)
      : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD4), "I" (_SFR_IO_ADDR(PIND)), "I" (PIND6), "I" (PIND5)
    );
  }
  else if (high==1)
  {
    unsigned char twofivefive=255;
    asm volatile
    (
      "      CLI\n"
      "LOOPX:\n"
      "      SBIS %5, %6\n"  // Wait for sync high
      "      RJMP LOOPX\n"
      "      DEC %2\n"
      "      BRNE LOOPX\n"
      "LOOPY:\n"
      "      SBIC %5, %6\n"  // Wait for sync low
      "      RJMP LOOPY\n"
      "LOOPZ:\n"
      "      DEC %1\n"   // 1 cycles
      "      BRNE LOOPZ\n"    // 2 cycles when true
      "LOOPW:\n"
      "      DEC %0\n"   // 1 cycles
      "      BRNE LOOPW\n"    // 2 cycles when true
      "      SBIC %5, %7\n"
      "      SBI %3, %4\n"
      "      SEI\n"
      : "+r" (low), "+r" (twofivefive), "+r" (counter)
      : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD4), "I" (_SFR_IO_ADDR(PIND)), "I" (PIND6), "I" (PIND5)
    );
  }
  delay(1); // Phosphor decay time
  digitalWrite(4, LOW);
  return low;
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

void WaitForVSync()
{
  short syncTime = 0;
  while (syncTime < MICROSECONDS_TO_CYCLES(15) / 5)
  {
    syncTime = GetSyncTime();
  }
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
  return (MICROSECONDS_TO_CYCLES(6) + MICROSECONDS_TO_CYCLES(x * 52 / 640)) / 3;
}

void loop()
{
  short x = 320, y = 10;
  // Read inputs, update x/y
  // update trigger button
  WaitForVSync();
  WaitForVSync();
  WaitForVSync();
  delayMicroseconds(20); // Make sure we ignore the first pulse (we can miss it due to interrupts)
  digitalWrite(13,led);
  short line = 0;
  while (true)
  {
    if (line == y)
    {
      ProcessLine(CalculateDelay(x));
      break;
    }
    else
    {
      WaitForHSync();
    }
    line++;
  }
  led=1-led;
}

