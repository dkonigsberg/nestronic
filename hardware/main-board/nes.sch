EESchema Schematic File Version 2
LIBS:nes
LIBS:ac-dc
LIBS:adc-dac
LIBS:Altera
LIBS:analog_devices
LIBS:analog_switches
LIBS:atmel
LIBS:audio
LIBS:Battery_Management
LIBS:bbd
LIBS:Bosch
LIBS:brooktre
LIBS:Connector
LIBS:contrib
LIBS:cypress
LIBS:dc-dc
LIBS:Decawave
LIBS:device
LIBS:digital-audio
LIBS:Diode
LIBS:Display
LIBS:driver_gate
LIBS:dsp
LIBS:DSP_Microchip_DSPIC33
LIBS:elec-unifil
LIBS:ESD_Protection
LIBS:Espressif
LIBS:FPGA_Actel
LIBS:ftdi
LIBS:gennum
LIBS:Graphic
LIBS:hc11
LIBS:infineon
LIBS:intel
LIBS:interface
LIBS:intersil
LIBS:ir
LIBS:Lattice
LIBS:LED
LIBS:LEM
LIBS:linear
LIBS:Logic_74xgxx
LIBS:Logic_74xx
LIBS:Logic_CMOS_4000
LIBS:Logic_CMOS_IEEE
LIBS:logic_programmable
LIBS:Logic_TTL_IEEE
LIBS:maxim
LIBS:MCU_Microchip_PIC10
LIBS:MCU_Microchip_PIC12
LIBS:MCU_Microchip_PIC16
LIBS:MCU_Microchip_PIC18
LIBS:MCU_Microchip_PIC24
LIBS:MCU_Microchip_PIC32
LIBS:MCU_NXP_Kinetis
LIBS:MCU_NXP_LPC
LIBS:MCU_NXP_S08
LIBS:MCU_Parallax
LIBS:MCU_ST_STM8
LIBS:MCU_ST_STM32
LIBS:MCU_Texas_MSP430
LIBS:Mechanical
LIBS:memory
LIBS:microchip
LIBS:microcontrollers
LIBS:modules
LIBS:Motor
LIBS:motor_drivers
LIBS:motorola
LIBS:nordicsemi
LIBS:nxp
LIBS:onsemi
LIBS:opto
LIBS:Oscillators
LIBS:philips
LIBS:power
LIBS:powerint
LIBS:Power_Management
LIBS:pspice
LIBS:references
LIBS:regul
LIBS:Relay
LIBS:RF_Bluetooth
LIBS:rfcom
LIBS:RFSolutions
LIBS:Sensor_Current
LIBS:Sensor_Humidity
LIBS:sensors
LIBS:silabs
LIBS:siliconi
LIBS:supertex
LIBS:Switch
LIBS:texas
LIBS:Transformer
LIBS:Transistor
LIBS:triac_thyristor
LIBS:Valve
LIBS:video
LIBS:wiznet
LIBS:Worldsemi
LIBS:Xicor
LIBS:xilinx
LIBS:xilinx-artix7
LIBS:xilinx-kintex7
LIBS:xilinx-spartan6
LIBS:xilinx-virtex5
LIBS:xilinx-virtex6
LIBS:xilinx-virtex7
LIBS:zetex
LIBS:Zilog
LIBS:ESP32-footprints-Shem-Lib
LIBS:nes-cache
EELAYER 25 0
EELAYER END
$Descr USLetter 11000 8500
encoding utf-8
Sheet 1 3
Title "Nestronic Game Music Synthesizer"
Date "2018-03-13"
Rev "A"
Comp "LogicProbe.org"
Comment1 "Derek Konigsberg"
Comment2 "Root Schematic"
Comment3 ""
Comment4 ""
$EndDescr
$Sheet
S 4600 950  1650 1100
U 5A639FBE
F0 "NES CPU" 60
F1 "nes-cpu.sch" 60
F2 "SDA" B L 4600 1200 60 
F3 "SCL" B L 4600 1350 60 
F4 "SPK+" O R 6250 1200 60 
F5 "SPK-" O R 6250 1300 60 
$EndSheet
$Sheet
S 2150 950  1650 1100
U 5A63B344
F0 "ESP32 Microcontroller" 60
F1 "nes-esp.sch" 60
F2 "SDA0" B R 3800 1200 60 
F3 "SCL0" B R 3800 1350 60 
F4 "SCL1" B L 2150 1350 60 
F5 "SDA1" B L 2150 1200 60 
F6 "I/O_INT" I L 2150 1500 60 
F7 "I/O_TP" I L 2150 1650 60 
$EndSheet
$Comp
L R R3
U 1 1 5A63C0AA
P 4100 950
F 0 "R3" V 4180 950 50  0000 C CNN
F 1 "2k" V 4100 950 50  0000 C CNN
F 2 "Resistor_SMD:R_0805_2012Metric" V 4030 950 50  0001 C CNN
F 3 "http://www.vishay.com/docs/20035/dcrcwe3.pdf" H 4100 950 50  0001 C CNN
F 4 "RES SMD 2K OHM 5% 1/8W 0805" H 4100 950 60  0001 C CNN "Description"
F 5 "Vishay Dale" H 4100 950 60  0001 C CNN "Manufacturer"
F 6 "CRCW08052K00JNEA" H 4100 950 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 4100 950 60  0001 C CNN "Supplier"
F 8 "541-2.0KACT-ND" H 4100 950 60  0001 C CNN "Supplier PN"
	1    4100 950 
	1    0    0    -1  
$EndComp
$Comp
L +3.3V #PWR01
U 1 1 5A63C486
P 4200 750
F 0 "#PWR01" H 4200 600 50  0001 C CNN
F 1 "+3.3V" H 4200 890 50  0000 C CNN
F 2 "" H 4200 750 50  0001 C CNN
F 3 "" H 4200 750 50  0001 C CNN
	1    4200 750 
	1    0    0    -1  
$EndComp
$Comp
L R R4
U 1 1 5A63E670
P 4300 950
F 0 "R4" V 4380 950 50  0000 C CNN
F 1 "2k" V 4300 950 50  0000 C CNN
F 2 "Resistor_SMD:R_0805_2012Metric" V 4230 950 50  0001 C CNN
F 3 "http://www.vishay.com/docs/20035/dcrcwe3.pdf" H 4300 950 50  0001 C CNN
F 4 "RES SMD 2K OHM 5% 1/8W 0805" H 4300 950 60  0001 C CNN "Description"
F 5 "Vishay Dale" H 4300 950 60  0001 C CNN "Manufacturer"
F 6 "CRCW08052K00JNEA" H 4300 950 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 4300 950 60  0001 C CNN "Supplier"
F 8 "541-2.0KACT-ND" H 4300 950 60  0001 C CNN "Supplier PN"
	1    4300 950 
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR02
U 1 1 5A6912F5
P 2900 2900
F 0 "#PWR02" H 2900 2650 50  0001 C CNN
F 1 "GND" H 2900 2750 50  0000 C CNN
F 2 "" H 2900 2900 50  0001 C CNN
F 3 "" H 2900 2900 50  0001 C CNN
	1    2900 2900
	1    0    0    -1  
$EndComp
$Comp
L +5V #PWR03
U 1 1 5A6912FB
P 2900 2600
F 0 "#PWR03" H 2900 2450 50  0001 C CNN
F 1 "+5V" H 2900 2740 50  0000 C CNN
F 2 "" H 2900 2600 50  0001 C CNN
F 3 "" H 2900 2600 50  0001 C CNN
	1    2900 2600
	1    0    0    -1  
$EndComp
$Comp
L +3.3V #PWR04
U 1 1 5A691301
P 2900 3350
F 0 "#PWR04" H 2900 3200 50  0001 C CNN
F 1 "+3.3V" H 2900 3490 50  0000 C CNN
F 2 "" H 2900 3350 50  0001 C CNN
F 3 "" H 2900 3350 50  0001 C CNN
	1    2900 3350
	1    0    0    -1  
$EndComp
$Comp
L R-78E5.0-0.5 U1
U 1 1 5A74066E
P 2050 2600
F 0 "U1" H 1900 2725 50  0000 C CNN
F 1 "R-78E5.0-0.5" H 2050 2725 50  0000 L CNN
F 2 "lib_fp:Converter_DCDC_RECOM_R-78E-0.5_THT" H 2100 2350 50  0001 L CIN
F 3 "https://www.recom-power.com/pdf/Innoline/R-78Exx-0.5.pdf" H 2050 2600 50  0001 C CNN
F 4 "CONV DC/DC 5V 500MA OUT THRU" H 2050 2600 60  0001 C CNN "Description"
F 5 "Recom Power" H 2050 2600 60  0001 C CNN "Manufacturer"
F 6 "R-78E5.0-0.5" H 2050 2600 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 2050 2600 60  0001 C CNN "Supplier"
F 8 "945-1648-5-ND" H 2050 2600 60  0001 C CNN "Supplier PN"
	1    2050 2600
	1    0    0    -1  
$EndComp
$Comp
L R-78E3.3-0.5 U2
U 1 1 5A740787
P 2050 3350
F 0 "U2" H 1900 3475 50  0000 C CNN
F 1 "R-78E3.3-0.5" H 2050 3475 50  0000 L CNN
F 2 "lib_fp:Converter_DCDC_RECOM_R-78E-0.5_THT" H 2100 3100 50  0001 L CIN
F 3 "https://www.recom-power.com/pdf/Innoline/R-78Exx-0.5.pdf" H 2050 3350 50  0001 C CNN
F 4 "DC/DC CONVERTER 3.3V 500MA THRU" H 2050 3350 60  0001 C CNN "Description"
F 5 "Recom Power" H 2050 3350 60  0001 C CNN "Manufacturer"
F 6 "R-78E3.3-0.5" H 2050 3350 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 2050 3350 60  0001 C CNN "Supplier"
F 8 "945-1661-5-ND" H 2050 3350 60  0001 C CNN "Supplier PN"
	1    2050 3350
	1    0    0    -1  
$EndComp
$Comp
L Barrel_Jack J1
U 1 1 5A7408B8
P 850 2700
F 0 "J1" H 850 2910 50  0000 C CNN
F 1 "Power Jack" H 850 2525 50  0000 C CNN
F 2 "lib_fp:BarrelJack_CUI_PJ-058AH_Horizontal" H 900 2660 50  0001 C CNN
F 3 "http://www.cui.com/product/resource/digikeypdf/pj-058ah.pdf" H 900 2660 50  0001 C CNN
F 4 "CONN PWR JACK 2X5.5MM SOLDER" H 850 2700 60  0001 C CNN "Description"
F 5 "CUI Inc." H 850 2700 60  0001 C CNN "Manufacturer"
F 6 "PJ-058AH" H 850 2700 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 850 2700 60  0001 C CNN "Supplier"
F 8 "CP-058AH-ND" H 850 2700 60  0001 C CNN "Supplier PN"
	1    850  2700
	1    0    0    -1  
$EndComp
$Comp
L C C1
U 1 1 5A740D42
P 1600 2750
F 0 "C1" H 1625 2850 50  0000 L CNN
F 1 "10uF" H 1625 2650 50  0000 L CNN
F 2 "Capacitor_SMD:C_0805_2012Metric" H 1638 2600 50  0001 C CNN
F 3 "http://www.samsungsem.com/kr/support/product-search/mlcc/CL21A106KAFN3NE.jsp" H 1600 2750 50  0001 C CNN
F 4 "CAP CER 10UF 25V X5R 0805" H 1600 2750 60  0001 C CNN "Description"
F 5 "Samsung Electro-Mechanics" H 1600 2750 60  0001 C CNN "Manufacturer"
F 6 "CL21A106KAFN3NE" H 1600 2750 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 1600 2750 60  0001 C CNN "Supplier"
F 8 "1276-2890-1-ND" H 1600 2750 60  0001 C CNN "Supplier PN"
	1    1600 2750
	1    0    0    -1  
$EndComp
$Comp
L C C2
U 1 1 5A741046
P 1600 3500
F 0 "C2" H 1625 3600 50  0000 L CNN
F 1 "10uF" H 1625 3400 50  0000 L CNN
F 2 "Capacitor_SMD:C_0805_2012Metric" H 1638 3350 50  0001 C CNN
F 3 "http://www.samsungsem.com/kr/support/product-search/mlcc/CL21A106KAFN3NE.jsp" H 1600 3500 50  0001 C CNN
F 4 "CAP CER 10UF 25V X5R 0805" H 1600 3500 60  0001 C CNN "Description"
F 5 "Samsung Electro-Mechanics" H 1600 3500 60  0001 C CNN "Manufacturer"
F 6 "CL21A106KAFN3NE" H 1600 3500 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 1600 3500 60  0001 C CNN "Supplier"
F 8 "1276-2890-1-ND" H 1600 3500 60  0001 C CNN "Supplier PN"
	1    1600 3500
	1    0    0    -1  
$EndComp
$Comp
L C C3
U 1 1 5A7410B1
P 2500 2750
F 0 "C3" H 2525 2850 50  0000 L CNN
F 1 "10uF" H 2525 2650 50  0000 L CNN
F 2 "Capacitor_SMD:C_0805_2012Metric" H 2538 2600 50  0001 C CNN
F 3 "http://www.samsungsem.com/kr/support/product-search/mlcc/CL21A106KAFN3NE.jsp" H 2500 2750 50  0001 C CNN
F 4 "CAP CER 10UF 25V X5R 0805" H 2500 2750 60  0001 C CNN "Description"
F 5 "Samsung Electro-Mechanics" H 2500 2750 60  0001 C CNN "Manufacturer"
F 6 "CL21A106KAFN3NE" H 2500 2750 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 2500 2750 60  0001 C CNN "Supplier"
F 8 "1276-2890-1-ND" H 2500 2750 60  0001 C CNN "Supplier PN"
	1    2500 2750
	1    0    0    -1  
$EndComp
$Comp
L C C4
U 1 1 5A74118F
P 2500 3500
F 0 "C4" H 2525 3600 50  0000 L CNN
F 1 "10uF" H 2525 3400 50  0000 L CNN
F 2 "Capacitor_SMD:C_0805_2012Metric" H 2538 3350 50  0001 C CNN
F 3 "http://www.samsungsem.com/kr/support/product-search/mlcc/CL21A106KAFN3NE.jsp" H 2500 3500 50  0001 C CNN
F 4 "CAP CER 10UF 25V X5R 0805" H 2500 3500 60  0001 C CNN "Description"
F 5 "Samsung Electro-Mechanics" H 2500 3500 60  0001 C CNN "Manufacturer"
F 6 "CL21A106KAFN3NE" H 2500 3500 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 2500 3500 60  0001 C CNN "Supplier"
F 8 "1276-2890-1-ND" H 2500 3500 60  0001 C CNN "Supplier PN"
	1    2500 3500
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR05
U 1 1 5A741B07
P 2900 3650
F 0 "#PWR05" H 2900 3400 50  0001 C CNN
F 1 "GND" H 2900 3500 50  0000 C CNN
F 2 "" H 2900 3650 50  0001 C CNN
F 3 "" H 2900 3650 50  0001 C CNN
	1    2900 3650
	1    0    0    -1  
$EndComp
Wire Wire Line
	3800 1200 4600 1200
Wire Wire Line
	3800 1350 4600 1350
Wire Wire Line
	4100 1100 4100 1200
Connection ~ 4100 1200
Wire Wire Line
	4300 1100 4300 1350
Connection ~ 4300 1350
Wire Wire Line
	6250 1200 6500 1200
Wire Wire Line
	6250 1300 6500 1300
Wire Wire Line
	1450 2600 1750 2600
Connection ~ 1600 2600
Connection ~ 1600 2900
Connection ~ 2050 2900
Wire Wire Line
	1450 3350 1750 3350
Wire Wire Line
	1250 3650 2900 3650
Connection ~ 2050 3650
Wire Wire Line
	2350 3350 2900 3350
Connection ~ 1600 3350
Connection ~ 1450 2600
Connection ~ 1600 3650
Connection ~ 1250 2900
Connection ~ 2500 2600
Connection ~ 2500 3350
Connection ~ 2500 2900
Wire Wire Line
	1250 2900 2900 2900
Wire Wire Line
	2350 2600 2900 2600
Connection ~ 2500 3650
Wire Wire Line
	1450 2600 1450 3350
$Comp
L PWR_FLAG #FLG06
U 1 1 5A74301E
P 1450 2600
F 0 "#FLG06" H 1450 2675 50  0001 C CNN
F 1 "PWR_FLAG" H 1450 2750 50  0001 C CNN
F 2 "" H 1450 2600 50  0001 C CNN
F 3 "" H 1450 2600 50  0001 C CNN
	1    1450 2600
	1    0    0    -1  
$EndComp
$Comp
L PWR_FLAG #FLG07
U 1 1 5A743127
P 1250 3650
F 0 "#FLG07" H 1250 3725 50  0001 C CNN
F 1 "PWR_FLAG" H 1250 3800 50  0001 C CNN
F 2 "" H 1250 3650 50  0001 C CNN
F 3 "" H 1250 3650 50  0001 C CNN
	1    1250 3650
	-1   0    0    1   
$EndComp
Text Notes 650  3850 0    60   ~ 0
Power Supply
Wire Notes Line
	600  2400 600  3900
Wire Notes Line
	600  3900 3050 3900
Wire Notes Line
	3050 3900 3050 2400
Wire Notes Line
	3050 2400 600  2400
$Comp
L R R1
U 1 1 5A80A048
P 1800 950
F 0 "R1" V 1880 950 50  0000 C CNN
F 1 "2k" V 1800 950 50  0000 C CNN
F 2 "Resistor_SMD:R_0805_2012Metric" V 1730 950 50  0001 C CNN
F 3 "http://www.vishay.com/docs/20035/dcrcwe3.pdf" H 1800 950 50  0001 C CNN
F 4 "RES SMD 2K OHM 5% 1/8W 0805" H 1800 950 60  0001 C CNN "Description"
F 5 "Vishay Dale" H 1800 950 60  0001 C CNN "Manufacturer"
F 6 "CRCW08052K00JNEA" H 1800 950 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 1800 950 60  0001 C CNN "Supplier"
F 8 "541-2.0KACT-ND" H 1800 950 60  0001 C CNN "Supplier PN"
	1    1800 950 
	1    0    0    -1  
$EndComp
$Comp
L R R2
U 1 1 5A80AA72
P 2000 950
F 0 "R2" V 2080 950 50  0000 C CNN
F 1 "2k" V 2000 950 50  0000 C CNN
F 2 "Resistor_SMD:R_0805_2012Metric" V 1930 950 50  0001 C CNN
F 3 "http://www.vishay.com/docs/20035/dcrcwe3.pdf" H 2000 950 50  0001 C CNN
F 4 "RES SMD 2K OHM 5% 1/8W 0805" H 2000 950 60  0001 C CNN "Description"
F 5 "Vishay Dale" H 2000 950 60  0001 C CNN "Manufacturer"
F 6 "CRCW08052K00JNEA" H 2000 950 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 2000 950 60  0001 C CNN "Supplier"
F 8 "541-2.0KACT-ND" H 2000 950 60  0001 C CNN "Supplier PN"
	1    2000 950 
	1    0    0    -1  
$EndComp
$Comp
L +3.3V #PWR08
U 1 1 5A80B9FD
P 1900 750
F 0 "#PWR08" H 1900 600 50  0001 C CNN
F 1 "+3.3V" H 1900 890 50  0000 C CNN
F 2 "" H 1900 750 50  0001 C CNN
F 3 "" H 1900 750 50  0001 C CNN
	1    1900 750 
	1    0    0    -1  
$EndComp
Wire Wire Line
	1800 1100 1800 1200
Connection ~ 1800 1200
Wire Wire Line
	2000 1100 2000 1350
Connection ~ 2000 1350
$Comp
L +3.3V #PWR09
U 1 1 5A811092
P 1450 1050
F 0 "#PWR09" H 1450 900 50  0001 C CNN
F 1 "+3.3V" H 1450 1190 50  0000 C CNN
F 2 "" H 1450 1050 50  0001 C CNN
F 3 "" H 1450 1050 50  0001 C CNN
	1    1450 1050
	1    0    0    -1  
$EndComp
$Comp
L GND #PWR010
U 1 1 5A812BE6
P 1450 1650
F 0 "#PWR010" H 1450 1400 50  0001 C CNN
F 1 "GND" H 1450 1500 50  0000 C CNN
F 2 "" H 1450 1650 50  0001 C CNN
F 3 "" H 1450 1650 50  0001 C CNN
	1    1450 1650
	1    0    0    -1  
$EndComp
Wire Wire Line
	4100 800  4100 750 
Wire Wire Line
	4100 750  4300 750 
Wire Wire Line
	4300 750  4300 800 
Connection ~ 4200 750 
Wire Wire Line
	1800 800  1800 750 
Wire Wire Line
	1800 750  2000 750 
Wire Wire Line
	2000 750  2000 800 
Connection ~ 1900 750 
Wire Wire Line
	1250 2800 1250 3650
$Comp
L D D1
U 1 1 5A84EA68
P 1300 2600
F 0 "D1" H 1300 2700 50  0000 C CNN
F 1 "D" H 1300 2500 50  0000 C CNN
F 2 "Diode_SMD:D_SMB" H 1300 2600 50  0001 C CNN
F 3 "http://www.onsemi.com/pub/Collateral/MURS120T3-D.PDF" H 1300 2600 50  0001 C CNN
F 4 "DIODE GEN PURP 100V 2A SMB" H 1300 2600 60  0001 C CNN "Description"
F 5 "ON Semiconductor" H 1300 2600 60  0001 C CNN "Manufacturer"
F 6 "ON Semiconductor" H 1300 2600 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 1300 2600 60  0001 C CNN "Supplier"
F 8 "MURS110T3GOSCT-ND" H 1300 2600 60  0001 C CNN "Supplier PN"
	1    1300 2600
	-1   0    0    1   
$EndComp
Wire Wire Line
	1150 2800 1250 2800
Wire Wire Line
	1150 2700 1150 2800
Text Notes 650  2000 0    60   ~ 0
Input Board Connector
Wire Notes Line
	600  2050 600  850 
Wire Notes Line
	600  850  1700 850 
Wire Notes Line
	1700 850  1700 2050
Wire Notes Line
	1700 2050 600  2050
$Comp
L Conn_01x02 J3
U 1 1 5AA09BF7
P 6700 1200
F 0 "J3" H 6700 1300 50  0000 C CNN
F 1 "Speaker" H 6700 1000 50  0000 C CNN
F 2 "Connector_JST:JST_PH_B2B-PH-SM4-TB_1x02_P2.00mm_Vertical" H 6700 1200 50  0001 C CNN
F 3 "http://www.jst-mfg.com/product/pdf/eng/ePH.pdf" H 6700 1200 50  0001 C CNN
F 4 "CONN HEADER PH TOP 2POS 2MM SMD" H 6700 1200 60  0001 C CNN "Description"
F 5 "JST Sales America Inc." H 6700 1200 60  0001 C CNN "Manufacturer"
F 6 "B2B-PH-SM4-TB(LF)(SN)" H 6700 1200 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 6700 1200 60  0001 C CNN "Supplier"
F 8 "455-1734-1-ND" H 6700 1200 60  0001 C CNN "Supplier PN"
	1    6700 1200
	1    0    0    -1  
$EndComp
$Comp
L Conn_01x06 J2
U 1 1 5AA0BE5E
P 1100 1300
F 0 "J2" H 1100 1600 50  0000 C CNN
F 1 "Input Board" H 1100 900 50  0000 C CNN
F 2 "Connector_JST:JST_SH_BM06B-SRSS-TB_1x06_P1.00mm_Vertical" H 1100 1300 50  0001 C CNN
F 3 "http://www.jst-mfg.com/product/pdf/eng/eSH.pdf" H 1100 1300 50  0001 C CNN
F 4 "CONN HEADER SH 6POS TOP 1MM TIN" H 1100 1300 60  0001 C CNN "Description"
F 5 "JST Sales America Inc." H 1100 1300 60  0001 C CNN "Manufacturer"
F 6 "BM06B-SRSS-TB(LF)(SN)" H 1100 1300 60  0001 C CNN "Manufacturer PN"
F 7 "Digi-Key" H 1100 1300 60  0001 C CNN "Supplier"
F 8 "455-1792-1-ND" H 1100 1300 60  0001 C CNN "Supplier PN"
	1    1100 1300
	-1   0    0    -1  
$EndComp
Wire Wire Line
	1300 1600 1450 1600
Wire Wire Line
	1450 1600 1450 1650
Wire Wire Line
	1300 1100 1450 1100
Wire Wire Line
	1450 1100 1450 1050
Wire Wire Line
	1300 1200 2150 1200
Wire Wire Line
	1900 1350 2150 1350
Wire Wire Line
	1900 1350 1900 1300
Wire Wire Line
	1900 1300 1300 1300
Wire Wire Line
	2150 1500 1850 1500
Wire Wire Line
	1850 1500 1850 1400
Wire Wire Line
	1850 1400 1300 1400
Wire Wire Line
	2150 1650 1800 1650
Wire Wire Line
	1800 1650 1800 1500
Wire Wire Line
	1800 1500 1300 1500
$EndSCHEMATC
