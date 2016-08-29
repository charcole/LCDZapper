#LCDZapper

Homebrew device for making lightgun games playable on modern LCD TVs.

Check out the video of it in action here:
https://youtu.be/DzIPGpKo3Ag

*Disclaimer: This is very cobbled together and in no way an easy to follow guide for how to make one. Just want to share the ideas though so other people can understand the ideas and hopefully replicate it themselves. If there's a lot of feedback then I'll consider tidying it up and making something easier to follow. This is also only been test on PAL systems so there will need to be tweaks to the Arduino code to get working with NTSC machines.*

The Device
----------

The main three parts are...
- An analogue board for separating sync and trimmable white level
- An 5V 16MHz Adafruit Trinket Pro to do the timings and to flash the LED at the right time
- A Raspberry Pi with Bluetooth dongle to interface with Wiimote

The analogue board uses a LM1881 to extract the composite sync from the input composite video. This was originally done using the spare channel of the LM319 but because difficult to get working reliably when using capacitive coupled video signals (like on the Dreamcast). White level is less critical and uses an LM319 to just detect it's above a certain level (settable via a trimmer). Before feeding the single into the LM319 it first goes through an AD817 to amplify it slightly but mainly to shift the input signal into the common mode range of the LM319. After the AD817 it also runs through a simple resistor/capacitor low pass filter to try and strip out the colour component.

The Trinket Pro runs the included sketch. It's job is to wait for the right point in the frame (determined using the now digital sync signal from the analogue board). It does this by a loop written in assembler that can do delays accurate to a single cycle. It then samples the digital "white" output coming from the analogue board and if set it will activate the LED, simulating the flash of a CRT.

The Raspberry Pi runs a modified version of the Wiiuse library and example code. It reads the current position from the Wiimote, remaps it based on the calibration and then sends it via serial to the Trinket. Because the Arduino only polls the serial port every once in a while (we can't use interrupts as it'd mess with our strict timing loops) we have a scheme that sends small one byte packets. 4 packets recieved from the same set can be reconstructed to two 10 bit values whatever order they are recieved in. The Wiiuse library was modified slightly to knock out the code that only sends an event when a value has changed as it didn't seem to update when the wiimote went offscreen otherwise.

Feedback for adding the reticule is done using a 2N7000. The source and drain connect the input video signal to the TV output. This is in parallel with a small value resistor. When the tranistor is one it allows the signal through almost untouched but when on the signal goes through the resistor, dimming the picture.

Using It
--------

Calibration is done by pressing the home button on the Wiimote and then pressing B when pointing at the top left, top right, bottom left and bottom right. The lights on the Wiimote come on to show what stage of the calibration you are doing. It's good to calibration the Wiimote using the on screen reticule and then use the in game calibration method if there is one to adjust the gun timing.

A small bit of legacy code is in the sketch to allow NES games to be played completely with a Wiimote. Instead of an LED attached I originally had a NES connector attached so the Zapper could be completely bypassed. This worked well for the NES but the LED made supporting all machines that use a light gun easier.

Pressing A toggles through three modes. First is reticule displayed all the time. This is useful during calibration or if you want to play the game Wii style. Next is the reticule is only displayed shortly after pressing B (this is legacy from when NES games were played completely from a Wiimote) mimicing the way you see your last shot position on more modern games.

Finally pressing 1 alters the size of the detection area. This needs adjusting per game. Some devices like the Zapper need a lot of light to trigger so tend to have to be big (also makes the games easier). Some more modern games sometimes require multiple lines to be hit before it registers a detection. So just play around with it.

Final adjustment which is only really needed for NES/Master System is tweaking the white level trimmer on the analogue board. Most of the later light gun games probably just need this to be set so it always flashes the LED but NES/Master System games will need it tweaked so it only activates when pointing at a white pixel.

