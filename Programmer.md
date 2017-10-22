This version has been designed to make programming easy. As such you should be able to use a standard USB to Serial adapter with the following pinout (this is a common one on eBay)...

GND
(NC)
3.3V
TX
RX
(NC)

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
