GunCon2 Active Cables
---------------------

This directory has a work-in-progress GunCon2 cable code. This cable works well and as far as I can tell totally compatible with PS2 lightgun games, however **I certainly don't consider the code to be in a releaseable state**. However, I'm uploading it in the interests of transparency and sharing knowledge while this project is on hiatus.

The code is based around a USB framework called LUFA (http://www.fourwalledcubicle.com/LUFA.php) not standard Arduino so can be difficult to get working. This compiled under LUFA version 170418 and targetted cheap ATMEGA32U4 based development boards (search for Arduino Pro Micro). It was very much cobbled together based on a LUFA mouse example hence mouse.c etc and certainly could do with tinying up.

Serial data for position and button state of the report is read in via one serial line. The LightGunVerter's normal detection mechanism (the flashing LED when using a universal cable) and a per frame strobe is used to detect if the light gun should have registered a position. This is needed due to some game's calibration screens using this to verify the gun data is good. Optionally two inputs are available to add a hardware pedal and trigger if desired.
