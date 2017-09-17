#LCDZapperPro

Device for making lightgun games playable on modern LCD TVs with original hardware. For example NES Duck Hunt but the system will work with almost all lightgun games/systems from the NES all the way up to the Dreamcast.

![Picture of prototype board](https://github.com/charcole/LCDZapper/blob/master/Images/LCDZapperPro.jpg "Picture of prototype board")

Version4
--------

**This branch is for the fourth version of the design. This has not been manufactured yet so not tested. Therefore I suggest if you want to build one yourself you stick with version 2 for now (which is currently the master branch for this repo).**

Version2
--------

This is the second version of the design. The original Heath Robinson design can still be found in the OriginalPrototype directory. The new version packages everything up into a single contained unit and is intended to eventually be commerical product.

I'm working on a video to showcase the new version but in the meantime the video showing the old version in action can still be found here...
https://youtu.be/DzIPGpKo3Ag

This release should be considered a beta. The functionality is there but there are hardware and firmware bugs that still need to be worked out. Hardware and firmware may also be out of sync while in this period so be cautious of that if building your own before everything is finalised. Check the open Issue for latest information.

**Nb. I would like to release this as a commerical product once completed, therefore commerical use of the design/firmware is prohibited (see LICENSE.txt). Please contact me if you want to discuss commerical licensing. Hobbiest are welcome to make their own based on this design as long as it's for personal use only.**

Usage
-----

To provide positional information a Wii Remote (Wiimote) is used. This need to be fixed to the lightgun and a Wii sensor bar set up (*Nb. sensor bar will be built into newer revisions*).

To hook up the unit, composite video from the console is connected to the VIDEO_IN port, then from the VIDEO_OUT port to the TV. The device also has a USB mini port for powering the unit. The output is an LED on a length of wire that needs to be fixed in the barrel of the lightgun (blu-tak works well).

On power on the message "1+2" should appear on the screen. Press the 1 and 2 buttons on the Wiimote down together to start the pairing process. Once paired this message will disappear and a cursor will appear on the screen.

To calibrate the system so that the aim down the gun matches the cursor on the screen press Down on the Wiimote's D-Pad. A target should appear on the screen along with a message. Point at the target and press A on the Wiimote. Repeat this for all targets that appear. Once happy with calibration you can turn off the on screen cursor by pressing Home.

To reset the calibration press Up on the Wiimote's D-Pad.

Wiimote-only Operation
----------------------

An alternative to the above is to play games with only the Wiimote. Instead of an LED being inserted into the barrel of the lightgun, a cable can be constructed that connects the unit to the control port of the video game system. The Wiimote button presses get sent to the console along with the lightgun hit detection signal. Therefore you can play the game with just a Wiimote as if it was a Wii game.

Two Player Operation
--------------------

A second Wiimote can be connected by pairing in the same way as before (holding 1+2 together). You should now get two reticules and both can be calibrated as before. A future revision of the hardware will enable a second LED output for the other player.

In Wiimote-only operation some simple NES games (like Duck Hunt) will allow Co-operative play. Once two players are connected press Plus to enable this mode (you will see a message on screen). Both players' triggers will cause a shot to happen and either spot will register hits (it works as if the lightgun was looking at two parts of the screen simultaneously). Future versions of the firmware should improve compatability in this mode.

To go back into regular "duel" mode press Minus.

