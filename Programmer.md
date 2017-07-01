This version was designed to be a consumer product so, at least with the current version of the board, you need to construct a small programmer to upload firmware. The programming port is a 6 pin header on the board next to the ESP32. The programmer needs to be wired like this...

```
At board (from top to bottom)                      At serial adapter

5V      <----------------------------------------- 5V
TX      -----------------------------------------> Serial adapter RX
RX      <----------------470 Ohm------------------ Serial adapter TX
Reset   ----470 Ohm----Normally Open Switch -----> Ground
Program ----470 Ohm----Normally Open Switch -----> Ground
GND     -----------------------------------------> Ground
```

You'll need a serial adapter with 3.3V level output on the TX pin but most serial adapters designed for hobby electronics seem to do this.

Once you've set up the ESP32 toolchain (http://esp-idf.readthedocs.io/en/latest/get-started/) you'll need to set the correct serial port for your programmer. Do this with...

```
cd Firmware
make menuconfig
```

And change the Serial Flash Config -> Default serial port setting. Then to build and flash just run...

```
make flash
```

To put the board into programming mode, hold the switch attached to Program down while pressing the switch attached to Reset.

Most ESP32s seem to come with a WiFi updater pre-programmed so it might be possible to removing the programming requirement but I haven't had time to look into this yet.
