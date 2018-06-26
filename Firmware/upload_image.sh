#!/bin/bash

# Create and upload firmware

make && ./create_image.sh && networksetup -setairportnetwork en0 LightGunVerter && sleep 1 && curl --http1.0 -F myfile=@firmware.bin http://192.168.4.1/

