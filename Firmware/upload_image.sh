#!/bin/bash

# Create and upload firmware

networksetup -setairportnetwork en0 LightGunVerter && make && ./create_image.sh && sleep 2 && curl --http1.0 -F myfile=@firmware.bin http://192.168.4.1/

