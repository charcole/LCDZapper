EESchema Schematic File Version 2
LIBS:power
LIBS:device
LIBS:transistors
LIBS:conn
LIBS:linear
LIBS:regul
LIBS:74xx
LIBS:cmos4000
LIBS:adc-dac
LIBS:memory
LIBS:xilinx
LIBS:microcontrollers
LIBS:dsp
LIBS:microchip
LIBS:analog_switches
LIBS:motorola
LIBS:texas
LIBS:intel
LIBS:audio
LIBS:interface
LIBS:digital-audio
LIBS:philips
LIBS:display
LIBS:cypress
LIBS:siliconi
LIBS:opto
LIBS:atmel
LIBS:contrib
LIBS:valves
LIBS:rca_switch
LIBS:SCARTBreakout-cache
EELAYER 25 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L SCART-F J3
U 1 1 597325F7
P 2150 2300
F 0 "J3" H 2150 3500 50  0000 C CNN
F 1 "SCART_IN" H 2190 1100 50  0000 C CNN
F 2 "w_conn_av:SCART" H 2150 2300 50  0001 C CNN
F 3 "" H 2150 2300 50  0001 C CNN
	1    2150 2300
	1    0    0    -1  
$EndComp
$Comp
L SCART-F J4
U 1 1 59732649
P 5200 2300
F 0 "J4" H 5200 3500 50  0000 C CNN
F 1 "SCART_OUT" H 5240 1100 50  0000 C CNN
F 2 "w_conn_av:SCART" H 5200 2300 50  0001 C CNN
F 3 "" H 5200 2300 50  0001 C CNN
	1    5200 2300
	1    0    0    -1  
$EndComp
$Comp
L RCA_Switch J6
U 1 1 59733A54
P 4100 2450
F 0 "J6" H 4100 2315 60  0000 C CNN
F 1 "GREEN_OUT" H 4120 2585 60  0000 C CNN
F 2 "rca_switch_footprint:rca_yellow" H 4150 2500 60  0001 C CNN
F 3 "" H 4150 2500 60  0001 C CNN
	1    4100 2450
	-1   0    0    -1  
$EndComp
$Comp
L RCA_Switch J5
U 1 1 59733FC8
P 3250 2450
F 0 "J5" H 3250 2315 60  0000 C CNN
F 1 "GREEN_OUT" H 3270 2585 60  0000 C CNN
F 2 "rca_switch_footprint:rca_yellow" H 3300 2500 60  0001 C CNN
F 3 "" H 3300 2500 60  0001 C CNN
	1    3250 2450
	1    0    0    -1  
$EndComp
$Comp
L RCA_Switch J2
U 1 1 597343FA
P 4100 950
F 0 "J2" H 4100 800 60  0000 C CNN
F 1 "CSYNC_IN" H 4100 1100 60  0000 C CNN
F 2 "rca_switch_footprint:rca_yellow" H 4150 1000 60  0001 C CNN
F 3 "" H 4150 1000 60  0001 C CNN
	1    4100 950 
	-1   0    0    -1  
$EndComp
$Comp
L RCA_Switch J1
U 1 1 59734571
P 3250 950
F 0 "J1" H 3250 800 60  0000 C CNN
F 1 "CSYNC_OUT" H 3250 1100 60  0000 C CNN
F 2 "rca_switch_footprint:rca_yellow" H 3300 1000 60  0001 C CNN
F 3 "" H 3300 1000 60  0001 C CNN
	1    3250 950 
	1    0    0    -1  
$EndComp
Wire Wire Line
	2750 1650 5800 1650
Wire Wire Line
	2750 1850 5800 1850
Wire Wire Line
	2750 2050 5800 2050
Wire Wire Line
	2750 2250 5800 2250
Wire Wire Line
	2750 2650 5800 2650
Wire Wire Line
	2750 3050 5800 3050
Wire Wire Line
	1550 2950 4600 2950
Wire Wire Line
	4600 2750 1550 2750
Wire Wire Line
	1550 2150 4600 2150
Wire Wire Line
	1550 1950 4600 1950
Wire Wire Line
	4600 1350 1550 1350
Wire Wire Line
	1550 3350 1550 3600
Wire Wire Line
	1550 3600 5800 3600
Wire Wire Line
	5800 3600 5800 3250
Wire Wire Line
	4600 3350 3700 3350
Wire Wire Line
	3700 3350 3700 3250
Wire Wire Line
	3700 3250 2750 3250
Wire Wire Line
	1550 3150 1400 3150
Wire Wire Line
	1400 3150 1400 3750
Wire Wire Line
	1400 3750 5950 3750
Wire Wire Line
	5950 3750 5950 2850
Wire Wire Line
	5950 2850 5800 2850
Wire Wire Line
	2750 2850 3700 2850
Wire Wire Line
	3700 2850 3700 3150
Wire Wire Line
	3700 3150 4600 3150
Wire Wire Line
	1550 1750 4600 1750
Wire Wire Line
	1550 1550 3700 1550
Wire Wire Line
	3700 1550 3700 1450
Wire Wire Line
	3700 1450 5800 1450
Wire Wire Line
	4400 2450 4400 2350
Wire Wire Line
	4400 2350 4600 2350
Wire Wire Line
	4400 2550 6100 2550
Wire Wire Line
	1550 2350 2950 2350
Wire Wire Line
	2950 2300 2950 2450
Wire Wire Line
	1300 2550 2950 2550
Wire Wire Line
	3550 2500 3800 2500
Wire Wire Line
	1300 2550 1300 3850
Wire Wire Line
	1300 3850 6100 3850
Wire Wire Line
	6100 3850 6100 2550
Connection ~ 4600 2550
Connection ~ 1550 2550
Wire Wire Line
	2750 2450 1200 2450
Wire Wire Line
	1200 2450 1200 3950
Wire Wire Line
	1200 3950 6250 3950
Wire Wire Line
	6250 3950 6250 2450
Wire Wire Line
	6250 2450 5800 2450
Wire Wire Line
	5950 950  5950 1550
Wire Wire Line
	5950 1550 4600 1550
Wire Wire Line
	5950 950  4400 950 
Wire Wire Line
	3800 1000 3550 1000
Wire Wire Line
	2950 950  1350 950 
Wire Wire Line
	1350 950  1350 1450
Wire Wire Line
	1350 1450 2750 1450
Wire Wire Line
	2750 950  2750 650 
Wire Wire Line
	2750 650  3650 650 
Wire Wire Line
	3650 650  3650 1000
Connection ~ 3650 1000
Connection ~ 2750 950 
Wire Wire Line
	2950 2300 3650 2300
Wire Wire Line
	3650 2300 3650 2500
Connection ~ 3650 2500
Connection ~ 2950 2350
Wire Wire Line
	2950 1050 2850 1050
Wire Wire Line
	2850 1050 2850 1200
Wire Wire Line
	2850 1200 4500 1200
Wire Wire Line
	4500 1200 4500 1050
Wire Wire Line
	4500 1050 4400 1050
Wire Wire Line
	3500 1200 3500 1750
Connection ~ 3500 1750
Connection ~ 3500 1200
$EndSCHEMATC
