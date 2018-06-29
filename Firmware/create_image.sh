#!/bin/bash

# Creates a firmware image called firmware.bin that can be
# used with the WiFi update procedure

echo -n LGV_FIRM > firmware.bin
cat build/app-template.bin >> firmware.bin

