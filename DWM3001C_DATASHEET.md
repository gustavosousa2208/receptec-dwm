DWM3001C
|                   |     |     | ®   |     |     | Fully Integrated UWB Transceiver Module  |     |     |     |
| ----------------- | --- | --- | --- | --- | --- | ---------------------------------------- | --- | --- | --- |
| Product Overview  |     |     |     |     |     |                                          |     |     |     |
The Qorvo® DWM3001C is a fully integrated UWB transceiver
| module based on the Qorvo DW3110 IC.  Integration  |                |       |         |      | of            | the  |     |     |     |
| -------------------------------------------------- | -------------- | ----- | ------- | ---- | ------------- | ---- | --- | --- | --- |
| DW3110                                             | IC,  nRF52833  | MCU,  | planar  | UWB  | antenna,      |      |     |     |     |
| accelerometer, power management and crystal        |                |       |         |      | simplify the  |      |     |     |     |
design cycle. The RF design is fully validated, tested, and
| calibrated. Low  | power     | consumption  | allows     |     | powering        | from  |     |     |     |
| ---------------- | --------- | ------------ | ---------- | --- | --------------- | ----- | --- | --- | --- |
| batteries  for   | extended  | periods,     | providing  |     | cost-effective  |       |     |     |     |
implementation of UWB solutions.
The DWM3001C can be used in two-way ranging and TDoA
applications.

The DWM3001C is designed to be compliant to the FiRa™ PHY
48 Pin 27.1 x 19.13 mm side castellated package
| and  MAC specifications   |     | enabling  | interoperability  |     | with  | other  |     |     |     |
| ------------------------- | --- | --------- | ----------------- | --- | ----- | ------ | --- | --- | --- |
| FiRa™ compliant devices.  |     |           |                   |     |       |        |     |     |     |
Key Features
•  IEEE 802.15.4-2015 and IEEE 802.15.4z BPRF compliant
•  Fully aligned with FiRa™ PHY, MAC and certification
|     |     |     |     |     |     |     | development                                     |     |     |
| --- | --- | --- | --- | --- | --- | --- | ----------------------------------------------- | --- | --- |
|     |     |     |     |     |     |     | •  Supports Channels 5 (6.5 GHz) and 9 (8 GHz)  |     |     |
Functional Block Diagram  •  FCC, ISED and ETSI certification
•  Nordic nRF52833 SoC with BLE transceiver
•  Supply voltage VCC: 2.5 V - 3.6 V

•  Bluetooth chip antenna
•  AES 128/256 Security block
|     | BLE  |     | UWB |     |     |     |     |     |     |
| --- | ---- | --- | --- | --- | --- | --- | --- | --- | --- |
•  Fully coherent receiver for maximum range and accuracy
|     | Antenna |     | Antenna |     |     |     |     |     |     |
| --- | ------- | --- | ------- | --- | --- | --- | --- | --- | --- |
•  Power consumption optimized for battery applications
•  Data rates of 850 kbps, 6.8 Mbps
| RESET |     |     |     |     |     |     | •  Maximum packet length of 1023 bytes for high data  |     |     |
| ----- | --- | --- | --- | --- | --- | --- | ----------------------------------------------------- | --- | --- |
Nordic nRF52833
| BT_WAKE_UP | MCU / BLE TRx |            |     |         |     |      | throughput applications              |     |     |
| ---------- | ------------- | ---------- | --- | ------- | --- | ---- | ------------------------------------ | --- | --- |
| READY      |               |            | SPI | DW3110  |     |      |                                      |     |     |
|            |               |            |     |         |     | GPIO | •  Integrated MAC support features   |     |     |
| UART[1:0]  |               |            |     | UWB TRx | 4   |      |                                      |     |     |
| SPI[3:0]   |               | 64 MHz ARM |     |         |     |      |                                      |     |     |
•  Backward compatible with the DWM1001C module
| USB[1:0] |     | Cortex M4 |     |     |     |     |                                      |     |     |
| -------- | --- | --------- | --- | --- | --- | --- | ------------------------------------ | --- | --- |
| I2C[1:0] |     |           |     |     |     |     | •  Temperature range -40°C to +85°C  |     |     |
1.6 V
Applications
IMU
DCDC
VCC
Converter
| GND |     | 12  |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
•  Precision real time location systems (RTLS) using two-way
ranging or TDoA schemes in a variety of markets.
•  Location aware wireless sensor networks (WSNs)
P GWS
OI D
|     |     | 1[  |     |     |     |     | Ordering Information  |     |     |
| --- | --- | --- | --- | --- | --- | --- | --------------------- | --- | --- |
]0:

|     |     |     |     |     |     |     | PART NUMBER |     | DESCRIPTION  |
| --- | --- | --- | --- | --- | --- | --- | ----------- | --- | ------------ |

|     |     |     |     |     |     |     | DWM3001CSR  |     | Module – 100 pcs reel  |
| --- | --- | --- | --- | --- | --- | --- | ----------- | --- | ---------------------- |

|     |     |     |     |     |     |     | DWM3001CTR13  |     | Module – 500 pcs on 13” reel  |
| --- | --- | --- | --- | --- | --- | --- | ------------- | --- | ----------------------------- |

|     |     |     |     |     |     |     | DWM3001CDK  |     | Design Kit - MOQ 20 Units   |
| --- | --- | --- | --- | --- | --- | --- | ----------- | --- | --------------------------- |

|     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  1 of 27  www.qorvo.com

DWM3001C
® Fully Integrated UWB Transceiver Module
Table of Contents
7.1  Application Board Layout Guidelines ................................16
1.  Overview ..............................................................3
|             |                     |                           |                                         |                                                 | 7.2  N o | te   o n  R a n g i n g |   P e r f o r m a n c e   | i n   H a r s h   M u l t i | p a t h   |
| ----------- | ------------------- | ------------------------- | --------------------------------------- | ----------------------------------------------- | -------- | ----------------------- | ------------------------- | --------------------------- | --------- |
| 1 . 1   D W | M 3 0 0 1 C   U W B |   T r a n s c e iv e r  . | . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . .   3   |          |                         |                           |                             |           |
    En v ir o n m e n t s  . . .. . ... . .. . . .. .. .. .. .. .. . . . . ... .. . .. . . ... .. . . . .. . . .. .............16
| 1 . 2 N ord | ic   n R F 5 2 8 33 |   P r o c e s s o r  .. . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . .   3 |     |     |     |     |     |
| ----------- | ------------------- | ------------------------- | ----------------------------------------- | --------------------------------------------- | --- | --- | --- | --- | --- |
1.3  ST LIS2DH12TR Accelerometer ........................................ 3  8.  Package Information .......................................... 17
1.4  Torex XC9282 DCDC Converter ........................................ 3  8.1  Module Drawings .............................................................17
8.2  Module Land Pattern ........................................................17
2.  DWM3001C Calibration .........................................4
|               |                            |                                    |                                           |                                                | 8 . 3   M o | d u l e   M a r k in g  I | n f o rm a ti o n  . . . . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . 1 8   |
| ------------- | -------------------------- | ---------------------------------- | ----------------------------------------- | ---------------------------------------------- | ----------- | ------------------------- | -------------------------- | ----------------------------------------- | --------------------------------------------- |
| 2 . 1   C r y | st a l  O s c i lla t o r  | T r i m  . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . .  4   |             |                           |                            |                                           |                                               |
    8 . 4   M o d u l e   So l d e r  P r o f ile  ... .. . .. .. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 1 8
| 2 . 2 Tr a | n s m i tt e r  C a l ib | r a ti o n  . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . .   4 |     |     |     |     |     |
| ---------- | ------------------------ | ----------------------------------- | ----------------------------------------- | --------------------------------------------- | --- | --- | --- | --- | --- |
2.3  Antenna Delay Calibration ................................................. 4  9.  Tape and Reel Information ................................. 19
3.  DWM3001C Pin Connections ................................5  10.  Regulatory Information....................................... 20
3.1  Pin Numbering .................................................................. 5  10.1 European Union requirements ..........................................20
3.2  Pin Descriptions ................................................................ 6  10.1.1  Radio Equipment Directive ....................................... 20
4.  Electrical Specifications .......................................8  10.1.2  ETSI harmonised standards ..................................... 21
|     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
    1 0 . 2 U n i t e d   S t a t e s   ( F C C ) . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 2 2
4 . 1 N o m i n a l   O p e r a t i n g   C o n d i t i o n s   . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .   8
4 . 2   D C   C h a r a c te r i s t i c s  . .. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .   8   1 0 .2 . 1 R a d i o   a n d   T e l e v i s io n   I n t e r f e r e n c e   . . . . . . . . . . . . . . . . . . . . . . . . . . . .   2 2
|     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
4 . 3   R e c e i v e r   A C   C h a r a c t e r i s t i c s . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .   9   1 0 . 3 R e g u l a t o r y   H o s t   I n t e g r a t i o n   I n s t r u c t io n s   . . . . . . . . . . . . . . . . . . . . . . . . . . . 2 3
|             |                           |                          |                                        |                                                | 1 0 . 4   D e | v i c e   C l a s s i f i c a | t i o n s  . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . 2 3   |
| ----------- | ------------------------- | ------------------------ | -------------------------------------- | ---------------------------------------------- | ------------- | ----------------------------- | ------------------------------------ | ----------------------------------------- | --------------------------------------------- |
| 4 . 4   R e | f e r e n c e   C l o c k |  A C   C h a r a c t e r | is t i c s   . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . .. .   9   |               |                               |                                      |                                           |                                               |
    1 0 . 5   F C C   D e f i n i t i o n s   . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 2 3
| 4 .4. 1 | R e f e r e n c e   F | r e q u e n c y  . . . . . . | . . . . . . . . . . . . . . . . . . . . . | . . . . . . . . . . . . . . . . . . . . . .  9 |     |     |     |     |     |
| ------- | --------------------- | ---------------------------- | ----------------------------------------- | ---------------------------------------------- | --- | --- | --- | --- | --- |
    1 0 . 6   S i m u lt a n e o u s   T r a n s m i s s i o n   E v a l u a t i o n   . . . . . . . . . . . . . . . . . . . . . . . . . . . .2 3
4 . 5 T r a n s m itt e r  A C  C ha ra c t e r is t i c s  . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . 9
    1 0 . 7 O p e r a t i n g   R e q u i r e m e n t s   a n d   C o n d i t i o n s  . . . . . . . . . . . . . . . . . . . . . . . . . . 2 4
4 . 6 A b s o lu te   M a x im um  R a t i n g s  . . .. . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .1 0
|     |     |     |     |     | 1 0 . 8 M i s | c e l l a n e o u s   . . . | . . . . .. .. . . . .. . .. .. . . . .. | . . .. .. . . .. .. . . . . . . . . | . . . . . . . . . . . . . . . . . .. . 2 4 |
| --- | --- | --- | --- | --- | ------------- | --------------------------- | --------------------------------------- | ----------------------------------- | ------------------------------------------ |
5.  OTP Memory Map ............................................... 11
11.  Glossary ............................................................. 25
6.  Antenna Performance ......................................... 13
12.  Revision History ................................................. 26
7.  Application Information ...................................... 16
13.  Further Information ............................................ 26
List of Figures
Figure 1: DWM3001C Pin Diagram ................................5  Figure 6: Module Marking ........................................... 18
Figure 2: Antenna Radiation Planes ........................... 13  Figure 7: DWM3001C Module Solder Profile ............... 18
Figure 3: Board Layout............................................... 16  Figure 8: Tape and Reel Dimensions .......................... 19
Figure 4: Module Package Size (mm) ......................... 17  Figure 9: Tape Dimensions ........................................ 19
Figure 5: Module Land Pattern (mm) .......................... 17

List of Tables

Table 1: DWM3001C Calibrated Items ..................................... 4  Table 9: DWM3001C Absolute Maximum Ratings ................. 10
Table 2: DWM3001C Pin Functions ......................................... 6  Table 10: DWM3001C OTP map ........................................... 11
Table 3: Explanation of Abbreviations ...................................... 7  Table 11: Antenna characteristics .......................................... 13
Table 4: DWM3001C Operating Conditions ............................. 8  Table 12: Channel 5 ............................................................... 14
Table 5: DWM3001C DC Characteristics ................................. 8  Table 13: Channel 9 ............................................................... 15
Table 6: DWM3001C Receiver AC Characteristics .................. 9  Table 14: UWB RF Settings ................................................... 20
Table 7: Reference Clock AC Characteristics .......................... 9  Table 15: ETSI Standards ...................................................... 21
Table 8: DWM3001C Transmitter AC Characteristics .............. 9  Table 16: Glossary of Terms .................................................. 25
|     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  2 of 27  www.qorvo.com
|     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
3 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
1. Overview
The DWM3001C is a module that includes the DW3110 UWB transceiver, Nordic nRF52833 MCU with BLE, ST LIS2DH12
accelerometer and peripheral components. It is factory calibrated, FiRa™ compliant and certified to FCC, ETSI and IC regulations.
The following sections describe the main functional blocks within the DWM3001C. See the functional block diagram on Page 1 for
reference.
1.1 DWM3001C UWB Transceiver
The DW3110 on-board the DWM3001C is a fully integrated low-power, single chip CMOS RF transceiver IC. The DWM3001C module
requires no RF design as the antenna and associated analog and RF components are on the module.
The module contains an on-board 38.4 MHz reference crystal. The crystal has been trimmed in production to reduce the initial
frequency error to approximately 2 ppm, using the DW3110 IC’s internal on-chip crystal trimming circuit.
Always-On (AON) memory can be used to retain DWM3001C configuration data during the lowest power operational states when the on-chip
voltage regulators are disabled. This data is uploaded and downloaded automatically. Use of DWM3001C AON memory is configurable.
The on-chip voltage and temperature monitors allow the host to read the voltage on the VDD1 pin and the internal die temperature
information from the DW3110.
See the DW3000 datasheet for more detailed information on device functionality, electrical specifications, and typical performance.
1.2 Nordic nRF52833 Processor
The Nordic RF52833 is an ARM® Cortex®-M4 32-bit processor with 64 MHz clock speed that also integrates a BLE radio transceiver
operating in the 2.4 GHz band. It has advanced on-chip interfaces, such as USB 2.0 full speed (12 Mbps), high-speed 32 MHz SPI and
type 2 near field communication (NFC-A).
1.3 ST LIS2DH12TR Accelerometer
The LIS2DH12TR from ST Microelectronics is a low power three-axis linear accelerometer with I2C digital output. RTLS tags commonly
use accelerometers to initiate UWB ranging only when a tag moves so that battery life can be extended by staying in the lowest power
mode by default.
1.4 Torex XC9282 DCDC Converter
The Torex XC9282 buck converter IC is chosen to provide the DW3000 1.6 V supply from the VDD module voltage supplied externally.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
4 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
2. DWM3001C Calibration
Depending on the end-use applications and the system design, DWM3001C settings may need to be tuned. To help with this tuning a
number of built-in functions such as continuous wave TX and continuous frame transmission can be enabled. See the DW3000 User
Manual for further details.
An overview of factory calibrated items is shown in the table below:
Table 1: DWM3001C Calibrated Items
DW3000 Calibration Item Calibrated on DWM3001C
Crystal ✓
Transmit Power ✓
Antenna Delay ✓
2.1 Crystal Oscillator Trim
DWM3001C modules are calibrated at production at room temperature to minimise initial frequency error to reduce carrier frequency
offset between modules and thus improve receiver sensitivity. The calibration carried out at module production will trim the initial
frequency offset to less than 2 ppm, typically.
2.2 Transmitter Calibration
To maximize range, DWM3001C transmit power spectral density (PSD) should be set to the maximum allowable for the geographic
region in which it will be used. For most regions this is -41.3 dBm/MHz.
As the module contains an integrated antenna, the transmit power can only be measured over the air. The Effective Isotropic Radiated
Power (EIRP) must be measured, and the power level adjusted to ensure compliance with applicable regulations.
The DWM3001C provides the facility to adjust the transmit power in coarse and fine steps; 2 dB and 0.5 dB nominally. It also provides
the ability to adjust the spectral bandwidth. These adjustments can be used to maximize transmit power whilst meeting regulatory
spectral mask.
2.3 Antenna Delay Calibration
To measure range accurately, precise calculation of timestamps is required. To do this the antenna delay must be known. The
DWM3001C allows this delay to be calibrated and provides the facility to compensate for delays introduced by PCB, external
components, antenna and internal DWM3001C delays.
To calibrate the antenna delay, range is measured at a known distance using two DWM3001C systems. Antenna delay is adjusted until
the known distance and reported range agree. The antenna delay can be stored in OTP memory.*
*Note: For Engineering Samples DWM3001C.E1.0SR, DWM3001C.E1.0SQ, DWM3001C.E1.0SB and DWM3001CDKE1.0: do not
use the Channel 5 Antenna Delay in OTP – use default value 16390.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
5 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
3. DWM3001C Pin Connections
3.1 Pin Numbering
DWM3001C module pin assignments are as follows (viewed from top):
GND 1 48 GND
SWD_CLK 2 47 RESET
SWD_DIO 3 46 P0.02
P0.10 (NFC) 4 45 DW_GP2
P0.09 (NFC) 5 44 DW_GP3
P0.17 6 43 P0.30
P0.14 7 42 P0.31
P0.22 8 41 P0.27
P0.05 9 40 P0.07
P0.04 10 39 P0.23
GND 11 38 GND
VDD 12 37 P0.26
P0.12 13 36 DW_GP0
I2C0_SDA 14 35 DW_GP1
I2C0_SCL 15 34 P0.19
P0.21 16 33 P0.28
P0.20 17 32 P0.15
Figure 1: DWM3001C Pin Diagram

DWM3001C
|     |     | ® Fully Integrated UWB Transceiver Module  |     |     |
| --- | --- | ------------------------------------------ | --- | --- |
3.2  Pin Descriptions

Table 2: DWM3001C Pin Functions

I/O
| Signal Name  | Pin  |     | Description  | IC Pin Reference  |
| ------------ | ---- | --- | ------------ | ----------------- |
(Default)
Digital Interface
Serial wire debug clock input for debug and programming of  [N] SWDCLK
| SWD_CLK  | 2  DI  |     |     |     |
| -------- | ------ | --- | --- | --- |
nRF52833 processor.
Serial wire debug I/O for debug and programming of nRF52833  [N] SWDIO
| SWD_DIO  | 3  DIO  |     |     |     |
| -------- | ------- | --- | --- | --- |
processor.
|               |           | General purpose I/O for nRF52833 processor. NFC antenna  |     | [N] P0.10  |
| ------------- | --------- | -------------------------------------------------------- | --- | ---------- |
| P0.10 (NFC2)  | 4  Mixed  |                                                          |     |            |
connection.
|               |           | General purpose I/O for nRF52833 processor. NFC antenna  |     | [N] P0.09  |
| ------------- | --------- | -------------------------------------------------------- | --- | ---------- |
| P0.09 (NFC1)  | 5  Mixed  |                                                          |     |            |
connection.
P0.17  6  DIO  General purpose I/O for nRF52833 processor.  [N] P0.17
P0.14  7  DIO  General purpose I/O for nRF52833 processor.  [N] P0.14
P0.22  8  DIO  General purpose I/O for nRF52833 processor.  [N] P0.22
P0.05  9  DIO  General purpose I/O for nRF52833 processor.  [N] P0.05
P0.04  10  DIO  General purpose I/O for nRF52833 processor.  [N] P0.04
P0.12  13  DIO  General purpose I/O for nRF52833 processor.  [N] P0.12
I2C data signal of nRF52833 processor and LIS2DH12TR
| I2C0_SDA  | 14  DIO  |     |     | [N] P0.24  |
| --------- | -------- | --- | --- | ---------- |
accelerometer.
I2C clock signal of nRF52833 processor and LIS2DH12TR
| I2C0_SCL  | 15  DIO  |     |     | [N] P1.04  |
| --------- | -------- | --- | --- | ---------- |
accelerometer.
P0.21  16  DIO  General purpose I/O for nRF52833 processor.  [N] P0.21
P0.20  17  DIO  General purpose I/O for nRF52833 processor.  [N] P0.20
| USB_N  | 19  DIO  | USB D- I/O for nRF52833 processor.  |     | [N] D-  |
| ------ | -------- | ----------------------------------- | --- | ------- |
| USB_P  | 20  DIO  | USB D+ I/O for nRF52833 processor.  |     | [N] D+  |
P0.11  22  DIO  General purpose I/O for nRF52833 processor.  [N] P0.11
P1.08  23  DIO  General purpose I/O for nRF52833 processor.  [N] P1.08
P0.06  24  DIO  General purpose I/O for nRF52833 processor.  [N] P0.06
P1.01  25  DIO  General purpose I/O for nRF52833 processor.  [N] P1.01
P0.13  26  DIO  General purpose I/O for nRF52833 processor.  [N] P0.13
P1.09  27  DIO  General purpose I/O for nRF52833 processor.  [N] P1.09
P1.00  28  DIO  General purpose I/O for nRF52833 processor.  [N] P1.00
P1.05  29  DIO  General purpose I/O for nRF52833 processor.  [N] P1.05
DW_GP5  30  DIO  General purpose I/O for DW3110 transceiver.  [D] GPIO5
DW_GP6  31  DIO  General purpose I/O for DW3110 transceiver.  [D] GPIO6
P0.15  32  DIO  General purpose I/O for nRF52833 processor.  [N] P0.15
P0.28  33  DIO  General purpose I/O for nRF52833 processor.  [N] P0.28
P0.19  34  DIO  General purpose I/O for nRF52833 processor.  [N] P0.19
DW_GP1  35  DIO  General purpose I/O for DW3110 transceiver.  [D] GPIO1
DW_GP0  36  DIO  General purpose I/O for DW3110 transceiver.  [D] GPIO0
P0.26  37  DIO  General purpose I/O for nRF52833 processor.  [N] P0.26
DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  6 of 27  www.qorvo.com
|     |     |     |     |     |
| --- | --- | --- | --- | --- |

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
7 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
I/O
Signal Name Pin Description IC Pin Reference
(Default)
P0.23 39 DIO General purpose I/O for nRF52833 processor. [N] P0.23
P0.07 40 DIO General purpose I/O for nRF52833 processor. [N] P0.07
P0.27 41 DIO General purpose I/O for nRF52833 processor. [N] P0.27
P0.31 42 DIO General purpose I/O for nRF52833 processor. [N] P0.31
P0.30 43 DIO General purpose I/O for nRF52833 processor. [N] P0.30
DW_GPIO3 44 DIO General purpose I/O for DW3110 transceiver. [D] GPIO3
DW_GPIO2 45 DIO General purpose I/O for DW3110 transceiver. [D] GPIO2
P0.02 46 DIO General purpose I/O for nRF52833 processor. [N] P0.02
RESET [N] nRESET
47 DIO General purpose I/O for nRF52833 processor. Active-low reset input.
(P0.18)
Power Supplies
VDD 12 P 3 V supply pin.
Ground
1, 11,
GND 21, 38, G Common ground.
48
Table 3: Explanation of Abbreviations
Abbreviation Explanation
I Input
IO Input / Output
O Output
G Ground
P Power Supply
PD Power Decoupling
O-L Defaults to output, low level after reset
O-H Defaults to output, high level after reset
I Defaults to input.
Note: Any signal with the suffix ‘n’ indicates an active low signal.

DWM3001C
® Fully Integrated UWB Transceiver Module
4.  Electrical Specifications
4.1  Nominal Operating Conditions
Table 4: DWM3001C Operating Conditions

|                          | Parameter  | Min.  | Typ.  | Max.  | Units  | Condition / Note  |
| ------------------------ | ---------- | ----- | ----- | ----- | ------ | ----------------- |
| Operating temperature    |            | -40   |       | +85   | ˚C     |                   |
| Supply voltage VDD       |            | 2.5   |       | 3.6   | V      |                   |
| Voltage on digital pins  |            |       |       | 3.6   | V      |                   |

Note: Unit operation is guaranteed by design when operating within these ranges

4.2  DC Characteristics
Tamb = 25 ˚C, all supplies centered on typical values

Table 5: DWM3001C DC Characteristics

|                                | Parameter  | Min.  | Typ.  | Max.  | Units                              | Condition / Note  |
| ------------------------------ | ---------- | ----- | ----- | ----- | ---------------------------------- | ----------------- |
| Supply current SLEEP mode      |            |       | 850   |       | nA                                 |                   |
| Supply current CH5 IDLE mode   |            |       | 18    |       | mA                                 |                   |
| Supply current CH9 IDLE mode   |            |       | 32    |       | mA                                 |                   |
| Supply current CH5 INIT mode   |            |       | 6     |       | mA                                 |                   |
| Supply current CH9 INIT mode   |            |       | 6     |       | mA                                 |                   |
| CH5 TX: VDD                    |            |       | 40    |       | mA  Continuous frame transmission  |                   |
| CH9 TX: VDD                    |            |       | 45    |       | mA  Continuous frame transmission  |                   |
Receiver active, no frames
| CH5 RX: VDD  |     |     | 40  |     | mA  |     |
| ------------ | --- | --- | --- | --- | --- | --- |
received
Receiver active, no frames
| CH9 RX: VDD   |     |     | 45  |     | mA  |     |
| ------------- | --- | --- | --- | --- | --- | --- |
received
| Digital input voltage high  |     | 0.7*VDD1  |     |           | V    |     |
| --------------------------- | --- | --------- | --- | --------- | ---- | --- |
| Digital input voltage low   |     |           |     | 0.3*VDD1  | V    |     |
Digital output voltage high  0.7*VDD1      V  Assumes 500 Ω load
Digital output voltage low      0.3*VDD1  V  Assumes 500 Ω load
| Digital Output Drive Current GPIOx, IRQ  |     |     |     | 0.8  | mA    |     |
| ---------------------------------------- | --- | --- | --- | ---- | ----- | --- |
| Digital Output Drive Current SPIMISO     |     | 8   | 10  |      | mA    |     |
| Digital Output Drive Current EXTON       |     | 3   | 4   |      | mA    |     |

DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  8 of 27  www.qorvo.com
|     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- |

DWM3001C
® Fully Integrated UWB Transceiver Module

4.3  Receiver AC Characteristics
Tamb = 25 ˚C, all supplies centered on nominal values

Table 6: DWM3001C Receiver AC Characteristics

|                       | Parameter  | Min.  Typ.  | Max.  | Units  | Condition / Note  |
| --------------------- | ---------- | ----------- | ----- | ------ | ----------------- |
| Centre Frequency CH5  |            |   6489.6    |       |        |                   |
| Centre Frequency CH9  |            |   7987.2    |       | MHz    |                   |

| Channel bandwidth  |     |   500  |     |     | Channel 5 and 9  |
| ------------------ | --- | ------ | --- | --- | ---------------- |

4.4  Reference Clock AC Characteristics
Tamb = 25 ˚C, all supplies centered on typical values

4.4.1  Reference Frequency

Table 7: Reference Clock AC Characteristics

|     | Parameter  | Min.  Typ.  | Max.  | Units  | Condition / Note  |
| --- | ---------- | ----------- | ----- | ------ | ----------------- |
On-board crystal oscillator reference frequency    38.4    MHz
| On-board crystal aging  |     |   ±1  |     | ppm in 1st year  |     |
| ----------------------- | --- | ----- | --- | ---------------- | --- |

4.5  Transmitter AC Characteristics
Tamb = 25 ˚C, all supplies centered on typical values

Table 8: DWM3001C Transmitter AC Characteristics

|                     | Parameter  | Min.  Typ.  | Max.  | Units  | Condition / Note  |
| ------------------- | ---------- | ----------- | ----- | ------ | ----------------- |
| Frequency range     |            | 6250        | 8250  | MHz    |                   |
| Channel bandwidths  |            |   500       |       | MHz    | Channel 5 and 9   |

DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  9 of 27  www.qorvo.com
|     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- |

DWM3001C
® Fully Integrated UWB Transceiver Module
4.6  Absolute Maximum Ratings

Table 9: DWM3001C Absolute Maximum Ratings

|                                      | Parameter  | Min.  | Max.  | Units  |
| ------------------------------------ | ---------- | ----- | ----- | ------ |
| Voltage, VDD                         |            | -0.3  | 4.0   | V      |
| Receiver Power                       |            |       | 14    | dBm    |
| Temperature - Storage temperature    |            | -40   | +125  | ˚C     |
| Temperature - Operating temperature  |            | -40   | +85   | ˚C     |

Stresses beyond those listed in this table may cause permanent damage to the device. This is a stress rating only; functional operation
of the device at these or any other conditions beyond those indicated in the operating conditions of the specification is not implied.
Exposure to the absolute maximum rating conditions for extended periods may affect device reliability.

DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  10 of 27  www.qorvo.com

DWM3001C
|     |     |     | ®   |     |     | Fully Integrated UWB Transceiver Module  |     |     |     |     |
| --- | --- | --- | --- | --- | --- | ---------------------------------------- | --- | --- | --- | --- |
5. OTP Memory Map
The DW3000 transceiver has one-time programmable (OTP) memory storage. See the DW3000 User Manual for more details. The
OTP memory map for the DWM3001C module is shown in Table 10. Calibrated items from the final module production test stage are
highlighted.
|     | OTP Item                                  |     |     |     |     |     | Cell Colour  |     |     |     |
| --- | ----------------------------------------- | --- | --- | --- | --- | --- | ------------ | --- | --- | --- |
|     | Calibrated and written to OTP             |     |     |     |     |     |              |     |     |     |
|     | Read back from DW3110 and written in OTP  |     |     |     |     |     |              |     |     |     |
|     | Fixed value written to OTP                |     |     |     |     |     |              |     |     |     |

Table 10: DWM3001C OTP map
| OTP      | Size   |     |         |     |         |     |         |     |         | Programmed  |
| -------- | ------ | --- | ------- | --- | ------- | --- | ------- | --- | ------- | ----------- |
|          | (Used  |     | Byte_3  |     | Byte_2  |     | Byte_1  |     | Byte_0  |             |
| Address  |        |     |         |     |         |     |         |     |         | by          |
Bytes)
| 0x000  | 4   |     |     |     |                          |     |     |     |     |           |
| ------ | --- | --- | --- | --- | ------------------------ | --- | --- | --- | --- | --------- |
|        |     |     |     |     | 64-bit EUID              |     |     |     |     | Customer  |
| 0x001  | 4   |     |     |     |                          |     |     |     |     |           |
| 0x002  | 4   |     |     |     | Alternative 64-bit EUID  |     |     |     |     |           |
Customer
| 0x003  | 4   |     |     |     | (Selected via reg/SR register)  |     |     |     |     |                |
| ------ | --- | --- | --- | --- | ------------------------------- | --- | --- | --- | --- | -------------- |
| 0x004  | 4   |     |     |     |                                 |     |     |     |     |                |
|        |     |     |     |     | LDO Tune                        |     |     |     |     | IC Prod. Test  |
| 0x005  | 4   |     |     |     |                                 |     |     |     |     |                |
0x006  4  {“0001,0000,0001”, “CHIP ID 5 nibbles (20 bits)”}  IC Prod. Test
0x007  4  {“0001”, “LOT ID – 7 nibbles (28 bits)”}  IC Prod. Test
0x008  3    VBAT@3.0V  VBAT@3.62V  VBAT@1.62V  IC Prod. Test
|        |     |     |     |     |            |     |     |                |     |                |
| ------ | --- | --- | --- | --- | ---------- | --- | --- | -------------- | --- | -------------- |
| 0x009  | 1   |     |     |     |            |     |     | Temp @ 22±2°C  |     | IC Prod. Test  |
| 0x00A  | 4   |     |     |     | Bias Tune  |     |     |                |     | IC Prod. Test  |
0x00B  4  Antenna Delay - Rx RF Loop  Antenna Delay - Tx RF Loop  IC Prod. Test
|        |     | PDoA Iso. Ch9  |     | PDoA Iso. Ch9  |     | PDoA Iso. Ch5  |     | PDoA Iso. Ch5  |     |                |
| ------ | --- | -------------- | --- | -------------- | --- | -------------- | --- | -------------- | --- | -------------- |
| 0x00C  | 4   |                |     |                |     |                |     |                |     | IC Prod. Test  |
|        |     | RF2 → RF1      |     | RF1 → RF2      |     | RF2 → RF1      |     | RF1 → RF2      |     |                |
0x00D  4  W.S. Lot ID [3]  W.S. Lot ID [2]  W.S. Lot ID [1]  W.S. Lot ID [0]  IC Prod. Test
|        |     |     |     |     |     |                  |     |                  |     |                |
| ------ | --- | --- | --- | --- | --- | ---------------- | --- | ---------------- | --- | -------------- |
| 0x00E  | 2   |     |     |     |     | W.S. Lot ID [5]  |     | W.S. Lot ID [4]  |     | IC Prod. Test  |

0x00F  3  W.S. Wafer #  W.S. Y Loc  W.S. X Loc  IC Prod. Test
| 0x010  | 4   |     |            |     | Ch5 Tx Power Level - PRF16  |     |            |     |     | Customer           |
| ------ | --- | --- | ---------- | --- | --------------------------- | --- | ---------- | --- | --- | ------------------ |
| 0x011  | 4   |     |            |     | Ch5 Tx Power Level - PRF64  |     |            |     |     | Module Prod. Test  |
| 0x012  | 4   |     |            |     | Ch9 Tx Power Level - PRF16  |     |            |     |     | Customer           |
| 0x013  | 4   |     |            |     | Ch9 Tx Power Level - PRF64  |     |            |     |     | Module Prod. Test  |
|        |     |     |            |     |                             |     |            |     |     |                    |
| 0x014  | 4   |     |            |     |                             |     |            |     |     | Customer           |
| 0x015  | 4   |     |            |     |                             |     |            |     |     | Customer           |
| 0x016  | 4   |     |            |     |                             |     |            |     |     | Customer           |
|        |     |     |            |     |                             |     |            |     |     |                    |
| 0x017  | 4   |     |            |     |                             |     |            |     |     | Customer           |
| 0x018  | 4   |     | Ch5_PGCNT  |     |                             |     | Ch9_PGCNT  |     |     | Module Prod. Test  |
|        |     |     |            |     |                             |     |            |     |     |                    |
| 0x019  | 4   |     |            |     |                             |     |            |     |     | Customer           |
0x01A  4  CH5 Rx Antenna Delay - PRF64  CH5 Tx Antenna Delay - PRF64  Module Prod. Test
0x01B  4  CH5 Rx Antenna Delay - PRF16  CH5 Tx Antenna Delay - PRF16  Customer
DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  11 of 27  www.qorvo.com

DWM3001C
|     |     | ®   |     | Fully Integrated UWB Transceiver Module  |     |     |
| --- | --- | --- | --- | ---------------------------------------- | --- | --- |
0x01C  4  CH9 Rx Antenna Delay - PRF64  CH9 Tx Antenna Delay - PRF64  Module Prod. Test
0x01D  4  CH9 Rx Antenna Delay - PRF16  CH9 Tx Antenna Delay - PRF16  Customer
0x01E  1  Frame Duration - us    Xtal_Trim [6:0]  Module Prod. Test
| 0x01F  | 1   | Platform ID  |                               | Cal Rev  | OTP Rev.  | Module Prod. Test  |
| ------ | --- | ------------ | ----------------------------- | -------- | --------- | ------------------ |
| 0x020  | 4   |              |                               |          |           |                    |
|        |     |              | Rx_Tune_Cal: DGS_CFG0         |          |           | IC Prod. Test      |
| 0x021  | 4   |              | Rx_Tune_Cal: DGS_CFG1         |          |           | IC Prod. Test      |
| 0x022  | 4   |              | Rx_Tune_Cal: DGS_CFG2         |          |           | IC Prod. Test      |
| 0x023  | 4   |              | Rx_Tune_Cal: DGS_CFG3         |          |           | IC Prod. Test      |
| 0x024  | 4   |              | Rx_Tune_Cal: DGS_CFG4         |          |           | IC Prod. Test      |
| 0x025  | 4   |              |                               |          |           |                    |
|        |     |              | Rx_Tune_Cal: DGS_CFG5         |          |           | IC Prod. Test      |
| 0x026  | 4   |              | Rx_Tune_Cal: DGS_CFG6         |          |           | IC Prod. Test      |
| 0x027  | 4   |              | Rx_Tune_Cal: DGC_LUT_0 - CH5  |          |           | IC Prod. Test      |
| 0x028  | 4   |              | Rx_Tune_Cal: DGC_LUT_1 - CH5  |          |           | IC Prod. Test      |
| 0x029  | 4   |              | Rx_Tune_Cal: DGC_LUT_2 - CH5  |          |           | IC Prod. Test      |
| 0x02A  | 4   |              | Rx_Tune_Cal: DGC_LUT_3 - CH5  |          |           | IC Prod. Test      |
| 0x02B  | 4   |              | Rx_Tune_Cal: DGC_LUT_4 - CH5  |          |           | IC Prod. Test      |
| 0x02C  | 4   |              | Rx_Tune_Cal: DGC_LUT_5 - CH5  |          |           | IC Prod. Test      |
| 0x02D  | 4   |              | Rx_Tune_Cal: DGC_LUT_6 - CH5  |          |           | IC Prod. Test      |
| 0x02E  | 4   |              | Rx_Tune_Cal: DGC_LUT_0 - CH9  |          |           | IC Prod. Test      |
| 0x02F  | 4   |              |                               |          |           |                    |
|        |     |              | Rx_Tune_Cal: DGC_LUT_1 - CH9  |          |           | IC Prod. Test      |
| 0x030  | 4   |              | Rx_Tune_Cal: DGC_LUT_2 - CH9  |          |           | IC Prod. Test      |
| 0x031  | 4   |              | Rx_Tune_Cal: DGC_LUT_3 - CH9  |          |           | IC Prod. Test      |
| 0x032  | 4   |              | Rx_Tune_Cal: DGC_LUT_4 - CH9  |          |           | IC Prod. Test      |
| 0x033  | 4   |              | Rx_Tune_Cal: DGC_LUT_5 - CH9  |          |           | IC Prod. Test      |
| 0x034  | 4   |              | Rx_Tune_Cal: DGC_LUT_6 - CH9  |          |           | IC Prod. Test      |
| 0x035  | 4   |              | PLL_Lock_Code                 |          |           | IC Prod. Test      |
0x036 -
|     | 4   |     | Unallocated  |     |     | Customer  |
| --- | --- | --- | ------------ | --- | --- | --------- |
0x05F
0x060  4  QSR Register (Special function register)  Reserved
| 0x061  | 1   |     |     |     | Q_RR Register  | Reserved  |
| ------ | --- | --- | --- | --- | -------------- | --------- |
0x062 -
|     | 4   |     | Unallocated  |     |     | Customer  |
| --- | --- | --- | ------------ | --- | --- | --------- |
0x077
| 0x078  | 4   |     | AES_Key [127:96] (Big endian order)   |     |     | Customer  |
| ------ | --- | --- | ------------------------------------- | --- | --- | --------- |
| 0x079  | 4   |     | AES_Key [95:64] (Big endian order)    |     |     | Customer  |
| 0x07A  | 4   |     | AES_Key [63:32] (Big endian order)    |     |     | Customer  |
| 0x07B  | 4   |     | AES_Key [31:0] (Big endian order)     |     |     | Customer  |
| 0x07C  | 4   |     | AES_Key [255:224] (Big endian order)  |     |     | Customer  |
| 0x07D  | 4   |     | AES_Key [223:192] (Big endian order)  |     |     | Customer  |
| 0x07E  | 4   |     | AES_Key [191:160] (Big endian order)  |     |     | Customer  |
| 0x07F  | 4   |     | AES_Key [159:128] (Big endian order)  |     |     | Customer  |
DWM3001C Data Sheet Rev F, Oct 2025
Subject to change without notice | All rights reserved  12 of 27  www.qorvo.com

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
13 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
6. Antenna Performance
This section details antenna radiation patterns for the DWM3001CDKE1.0 Design Kit board. Figure 2 presents a view of the
measurement planes considered in this document.
Table 12 and Table 13 show antenna radiation patterns for the DWM3001C module mounted on the DWM3001CDKE1.0 carrier board
at Channels 5 and 9 respectively. Three planes in the spherical space about the center of the board are measured, with V and H plots
representing perpendicular vertical and horizontal components.
The DWM3001C antenna is vertically polarized, meaning that the module is intended to be positioned vertically upright when used in an
RTLS system. An omnidirectional radiation pattern is seen in the XZ plane when observed by another antenna which is also vertically
polarized. This is shown in the XZ plane antenna patterns, where the vertically polarized plot (V) has a circular, or omnidirectional
shape.
If the antennas are oriented perpendicular relative to each other, then the polarization changes. In this case, the horizontally polarized
pattern (H) applies and there are nulls at certain angles which can limit range and introduce location inaccuracy. Table 11 presents the
key characteristics of the DWM3001C’s antenna.
Table 11: Antenna characteristics
Antenna Model WB007 Dual-Hoe
Antenna Type PCB Trace Monopole Antenna
Peak Gain Ch5: 2.5dBi
(Measured on DWM3001CDK) Ch9: 2.0dBi
Frequency Range 4000 – 9000MHz
Figure 2: Antenna Radiation Planes

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
14 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
Table 12: Channel 5
Channel 5 Planes: Antenna Gain (dBi) vs. Angle (°)
XZ Plane:
180°
270°
V
90°
0° H
XY Plane:
90°
180°
V
H
0°
270°
YZ Plane:
180°
90°
V
270°
H
0°

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
15 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
Table 13: Channel 9
Channel 9 Planes: Antenna Gain (dBi) vs. Angle (°)
XZ Plane:
180°
270°
V
90°
0° H
XY Plane:
90°
180°
V
H
0°
270°
YZ Plane:
180°
90°
V
270°
H
0°

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
16 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
7. Application Information
7.1 Application Board Layout Guidelines
When designing the PCB onto which the DWM3001C will be soldered, the proximity of the DWM3001C on-board antenna to metal and
other non-RF transparent materials needs to be considered carefully. Two suggested placement schemes are shown below.
For best RF performance, ground copper should be flooded in all areas of the application board, except in the areas marked
“Keep-Out Area”, where there should be no metal either side, above or below (e.g. do not place battery under antenna).
The two placement schemes below show an application board with no metallic material in the keep-out area. The diagram on the right
is an application board with the antenna projecting off of the board so that the keep out area is in free space. The diagram on the left
shows an application board which does not have the module in free space but has the PCB copper removed on either side (and behind)
the module antenna. (Note: the rectangular area above the shield on the module is the antenna area)
It is also important to note that the ground plane on the application board affects the antenna radiation pattern. There must be a
minimum spacing of 10 mm (d) without metal either side of the module antenna.
d d
d d
Keep-Out Keep-Out
Area Area
Application Board Application Board
Figure 3: Board Layout
7.2 Note on Ranging Performance in Harsh Multipath Environments
As can be seen from the radiation patterns in Table 12 and Table 13, the DWM3001C antenna is cross polarized in some regions, i.e.
the horizontally polarized gain is roughly equal to the vertically polarized gain. This is of benefit in maintaining a high link budget as the
orientation of the module changes.
It can be found, however, that this polarization diversity can lead to variation in ranging accuracy as tags move in harsh multipath
environments, such as where there are many reflections in confined indoor spaces, e.g. narrow corridors. In these applications, it is
suggested to optimize the placement of the module on the carrier PCB. Avoid placing the module too close, i.e. less than 1 cm, to the
edge of the PCB. This has the effect of reducing the horizontally polarized radiation component and improving resilience to multipath
fading.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
17 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
8. Package Information
8.1 Module Drawings
Figure 4: Module Package Size (mm)
8.2 Module Land Pattern
The diagram below shows the DWM3001C module land pattern.
7.22
Figure 5: Module Land Pattern (mm)

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
18 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
8.3 Module Marking Information
Each module has a label on the shield with a serial number in the following format:
YY WW 4 SSSSS
Where:
YY indicates the year
WW indicates the week of the year
4 indicates the DWM3001C module
SSSSS indicates the module manufacturing number
Figure 6: Module Marking
8.4 Module Solder Profile
Figure 7: DWM3001C Module Solder Profile

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
19 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
9. Tape and Reel Information
Figure 8: Tape and Reel Dimensions
Figure 9: Tape Dimensions

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
20 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
10. Regulatory Information
The information below is valid for the DWM3001C module only.
US FCC ID: 2A35D-DWM3001, Canada ISED/IC: 23794-DWM3001
FCC and ETSI Test reports available to download at: https://www.qorvo.com/products/p/DWM3001C
DWM3001C certification approval is valid only for the following UWB RF settings:
Table 14: UWB RF Settings
UWB Parameters Description Notes
UWB Channel Channel 5 (6.5 GHz) Fixed
UWB Channel Channel 9 (8 GHz) Fixed
Data Rate 6.81 Mbps Fixed
PRF 64 MHz Fixed
Preamble Length 64 Fixed
Preamble Code 9 Fixed
PHR 850 Fixed
10.1 European Union requirements
10.1.1 Radio Equipment Directive
The DWM3001C Module has been certified for use in European Union countries. A copy of the EU Declaration of Conformity is
available for download on our website.
If these modules are incorporated into a product, the manufacturer must ensure continuing compliance of the final product to the Radio
Equipment Directive 2014/53/EU. The manufacturer must then draw up a new written EU Declaration of Conformity as per RED Article
18.
Furthermore, the manufacturer must ensure the final product does not exceed the specified power ratings, antenna specifications,
and/or installation requirements as specified in this document. If any of these specifications are exceeded in the final product, the
manufacturer must assess whether additional compliance testing is required.
IMPORTANT: The “CE” marking must be in compliance with the RED Article 19. It must be affixed to a visible location on the OEM
product. The CE mark shall have a height of at least 5 mm except where this is not possible on account of the nature of the apparatus.
The CE marking must be affixed visibly, legibly, and indelibly.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
21 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
10.1.2 ETSI harmonised standards
The DWM3001C module conforms with the requirements of the following ETSI standards:
Table 15: ETSI Standards
RED Standard No Title Edition /
reference Date
Article 3.1a EN 62479 Assessment of the compliance of low-power electronic and electrical 2010
equipment with the basic restrictions related to human exposure to
electromagnetic fields (10 MHz to 300 GHz)
Article 3.1b EN 301 489-1 ElectroMagnetic Compatibility (EMC) sta ndard for radio equipment and services; 2019-11
Part 1: Common technical requirements; Harmonised Standard for V2.2.3
ElectroMagnetic Compatibility
Article 3.1b EN 301 489- ElectroMagnetic Compatibility (EMC) standard for radio equipment and services; 2020-09
(for BLE) 17 Part 17: Specific conditions for Broadband Data Transmission Systems; V3.2.4
Harmonised Standard for ElectroMagnetic Compatibility
Article 3.1b EN 301 489- ElectroMagnetic compatibility (EMC) standard for radio equipment and services; 2019-04
(for UWB) 33 Part 33: Specific conditions for Ultra-WideBand (UWB) devices; Harmonised V2.2.1
Standard covering the essential requirements of article 3.1(b) of Directive
2014/53/EU
Article 3.2 EN 300 328 Wideband transmission systems; Data transmission equipment operating in the 2019-07
(for BLE) 2.4GHz band; Harmonised Standard for access to radio spectrum V2.2.2
Article 3.2 (for EN 302 065-1 Short Range Devices (SRD) using Ultra Wide Band technology (UWB); Part 2: 2016-11
UWB) Requirements for UWB location tracking; Harmonised Standard covering the V2.1.1
essential requirements of article 3.2 of the Directive 2014/53/EU
Please refer to Qorvo’s website for DWM3001C ETSI tests reports:
https://www.qorvo.com/products/p/DWM3001C

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
22 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
10.2 United States (FCC)
This device complies with Part 15 of the FCC Rules:
Operation is subject to the following conditions:
1. This device may not cause harmful interference, and
2. This device must accept any interference received, including interference that may cause undesired operation
Changes and Modifications not expressly approved by Qorvo Ltd. can void your authority to operate this equipment under Federal
Communications Commission rules.
Warning: Changes or modifications to this unit not expressly approved by the party responsible for compliance could void the user’s
authority to operate the equipment.
To fulfil FCC Certification requirements, an OEM manufacturer must comply with the following regulations:
1. The DWM3001C modular transmitter must be labelled with its own FCC ID number, and, if the FCC ID is not visible when the module
is installed inside another device, then the outside of the device into which the module is installed must also display a label referring to
the enclosed module. This exterior label can use wording such as the following:
IMPORTANT : Contains FCC ID: 2A35D-DWM3001. This equipment complies with Part 15 of the FCC Rules. Operation is subject to
the following two conditions: (1) this device may not cause harmful interference, and (2) this device must accept any interference
received, including interference that may cause undesired operation (FCC 15.19).
Please refer to Qorvo’s website for DWM3001C FCC tests reports: https://www.Qorvo.com/product/dwm3001-module
10.2.1 Radio and Television Interference
This equipment has been tested and found to comply with the limits for a Class B digital device, pursuant to Part 15 of the FCC rules.
These limits are designed to provide reasonable protection against harmful interference in a residential installation. This equipment
generates, uses, and can radiate radio frequency energy and, if not installed and used in accordance with the instructions, may cause
harmful interference to radio communications. However, there is no guarantee that interference will not occur in a particular installation.
If this equipment does cause harmful interference to radio or television reception, which can be determined by turning the equipment off
and on, the user is encouraged to try to correct the interference by one or more of the following measures:
- Reorient or relocate the receiving antenna.
- Increase the separation between the equipment and the receiver.
- Connect the equipment into an outlet on a circuit different from that to which the receiver is connected.
- Consult the dealer or an experienced radio/TV technician for help.
You may also find helpful the following booklet, prepared by the FCC: "How to Identify and Resolve Radio-TV Interference Problems."
This booklet is available from the U.S. Government Printing Office, Washington D.C. 20402.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
23 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
10.3 Regulatory Host Integration Instructions
This module has been granted modular approval for portable applications. OEM integrators for host products may use the module in
their final products without additional FCC/ISED certification if they meet the following conditions. Otherwise, additional FCC/ISED
approvals must be obtained.
• The host product with the module installed must be evaluated for simultaneous transmission requirements. The host integrator
installing this module into their product must ensure that the final composite product complies with the FCC requirements by a
technical assessment or evaluation to the FCC rules, including the transmitter operation. When configuring the module, PC or
Linux console applications such as Tera Term or PuTTY may be used, for more details, contact Qorvo Technical Support.
• Note that antennas mounted on outdoor structures, such as antennas mounted on the outside of a building or on a telephone
pole, or any fixed outdoors Infrastructure are prohibited for use with this device.
• The user’s manual for the host product must clearly indicate the operating requirements and conditions that must be observed
to ensure compliance with current FCC/ISED RF exposure guidelines.
• A label must be affixed to the outside of the host product with the following statements:
• This device contains FCC ID: 2A35D-DWM3001
• This equipment contains equipment certified under IC: 23794-DWM3001
The final host / module combination may also need to be evaluated against the FCC Part 15B criteria for unintentional radiators in order
to be properly authorized for operation as a Part 15 digital device.
If the final host / module combination is intended for use as a portable device (see classifications below) the host manufacturer is
responsible for separate evaluations/approvals for the SAR requirements from FCC Part 2.1093 and RSS-102.
10.4 Device Classifications
Since host devices vary widely with design features and configurations module integrators shall follow the guidelines below regarding
device classification and simultaneous transmission and seek guidance from their preferred regulatory test lab to determine how
regulatory guidelines will impact the device compliance. Proactive management of the regulatory process will minimize unexpected
schedule delays and costs due to unplanned testing activities.
The module integrator must determine the minimum distance required between their host device and the user’s body. The FCC
provides device classification definitions to assist in making the correct determination. Note that these classifications are guidelines
only; strict adherence to a device classification may not satisfy the regulatory requirement as near-body device design details may vary
widely. Your preferred test lab will be able to assist in determining the appropriate device category for your host product and if a KDB or
PBA must be submitted to the FCC.
10.5 FCC Definitions
Portable: (§2.1093) — A portable device is defined as a transmitting device designed to be used so that the radiating structure(s) of the
device is / are within 20 centimeters of the body of the user.
10.6 Simultaneous Transmission Evaluation
This module has not been evaluated or approved for simultaneous transmission as it is impossible to determine the exact multi-
transmission scenario that a host manufacturer may choose. Any simultaneous transmission condition established through module
integration into a host product must be evaluated per the requirements in KDB447498D01(8) and KDB616217D01,D03 (for laptop,
notebook, netbook, and tablet applications).
As mentioned above, the DWM3001C has been granted modular approval as a “Portable” device. However, end-customer Portable
applications may require further RF exposure (SAR) evaluations. It is also possible that the host / module combination will need to
undergo further testing for FCC Part 15 compliance regardless of the device classification. Your preferred test lab will be able to assist
in determining the exact tests which are required on the host / module combination.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
24 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
10.7 Operating Requirements and Conditions
The design of DWM3001 complies with U.S. Federal Communications Commission (FCC) guidelines respecting safety levels of radio
frequency (RF) exposure for Portable devices.
10.8 Miscellaneous
Except as otherwise described above, the DWM3001C BLE and UWB transmitters meet all requirements for SAR or RF Exposure,
intended or unintended, under FCC, ISED and ETSI rules. The BLE radio falls well below the limit determined for <5 mm separation (a
“body-worn” device) and, for a UWB radio operating above 6 GHz, there are currently no exposure limits specified.
All products developed by the user incorporating the DWM3001C must be approved by the relevant authority governing radio emissions
in any given jurisdiction prior to the marketing or sale of such products in that jurisdiction and user bears all responsibility for obtaining
such approval as needed from the appropriate authorities.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
25 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
11. Glossary
Table 16: Glossary of Terms
Abbreviation Full Title Explanation
The amount of power that a theoretical isotropic antenna (which evenly distributes
Equivalent Isotropic
EIRP power in all directions) would emit to produce the peak power density observed in the
Radiated Power
direction of maximum gain of the antenna being used.
European
Regulatory body in the EU charged with the management of the radio spectrum and the
ETSI Telecommunication
setting of regulations for devices that use it.
Standards Institute
Federal
Regulatory body in the USA charged with the management of the radio spectrum and
FCC Communications
the setting of regulations for devices that use it.
Commission
General Purpose Pin of an IC that can be configured as an input or output under software control and has
GPIO
Input / Output no specifically identified function.
Institute of Electrical Is the world’s largest technical professional society. It is designed to serve professionals
IEEE and Electronic involved in all aspects of the electrical, electronic, and computing fields and related
Engineers areas of science and technology.
Innovation, Science
Regulatory body in Canada charged with the management of the radio spectrum and
ISED and Economic
the setting of regulations for devices that use it.
Development Canada
Circuit designed to generate a signal at a particular frequency whose phase is related to
PLL Phase Locked Loop
an incoming “reference” signal.
Used to quantify very small relative proportions. Just as 1% is one out of a hundred,
PPM Parts Per Million
1 ppm is one part in a million.
Generally used to refer to signals in the range of 3 kHz to 300 GHz. In the context of a
RF Radio Frequency radio receiver, the term is generally used to refer to circuits in a receiver before
down-conversion takes place and in a transmitter after up-conversion takes place.
Real Time Location
RTLS System intended to provide information on the location of various items in real-time.
System
Start of Frame
SFD Defined in the context of the IEEE 802.15.4-2011 standard.
Delimiter
Serial Peripheral An industry standard method for interfacing between IC’s using a synchronous serial
SPI
Interface scheme first introduced by Motorola.
Chinese State Radio
The radio regulation authority of the People's Republic of China with responsibilities
SRRC Regulatory
including spectrum management and frequency allocation.
Commission
Method of measuring the physical distance between two radio units by exchanging
TWR Two Way Ranging messages between the units and noting the times of transmission and reception. Refer
to Qorvo’s website for further information.
Method of deriving information on the location of a transmitter. The time of arrival of a
Time Difference of transmission at two physically different locations whose clocks are synchronized is
TDoA
Arrival noted and the difference in the arrival times provides information on the location of the
transmitter.
UWB Ultra-Wideband A radio scheme employing channel bandwidths of, or in excess of, 500 MHz.
Wireless Sensor A network of wireless nodes intended to enable the monitoring and control of the
WSN
Network physical environment.

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
26 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
12. Revision History
Revision Change Description
A Initial Release (Preliminary)
Revised to add Regulatory Information, ESD and RoHS compliance, E1.0 Ch5 ant del note, antenna radiation patterns, solder
B
profile and T&R information
C Added FiRaTM certified logo
D Updated ordering information, corrected ST part number to LIS2DH12TR
E Updated radio certification information
F Updated radio certification information. Fix to FCC test report web link.
13. Further Information
Qorvo develops semiconductors solutions, software, modules, reference designs - that enable real-time, ultra-accurate, ultra-reliable
local area micro-location services. Qorvo’s technology enables an entirely new class of easy to implement, highly secure, intelligent
location functionality and services for IoT and smart consumer products and applications.
For further information on this or any other Qorvo product, please refer to our website www.qorvo.com.
Handling Precautions
Parameter Rating Standard
ESD - Charged Device Model (CDM) 1000V ANSI/ESDA/JEDEC JS-002 Caution!
ESD-Sensitive Device
ESD – Human Body Model (HBM) 2000V ANSI/ESDA/JEDEC JS-001
MSL – Moisture Sensitivity Level Level 3 IPC/JEDEC J-STD-020
RoHS Compliance
This part is compliant with 2011/65/EU RoHS directive (Restrictions on the Use of Certain Hazardous Substances in Electrical and
Electronic Equipment) as amended by Directive 2015/863/EU.
This product also has the following attributes:
• Antimony Free
• TBBP-A (C15H12Br402) Free
• SVHC Free

DWM3001C
Fully Integrated UWB Transceiver Module
DWM3001C Data Sheet Rev F, Oct 2025
27 of 27 www.qorvo.com
Subject to change without notice | All rights reserved
®
Contact Information
For the latest specifications, additional product information, worldwide sales, and distribution locations:
Web: www.qorvo.com
Tel: 1-844-890-8163
Email: customer.support@qorvo.com
Important Notice
The information contained in this Data Sheet and any associated documents (“Data Sheet Information”) is believed to be reliable; however, Qorvo makes no warranties
regarding the Data Sheet Information and assumes no responsibility or liability whatsoever for the use of said information. All Data Sheet Information is subject to change
without notice. Customers should obtain and verify the latest relevant Data Sheet Information before placing orders for Qorvo® products. Data Sheet Information or the use
thereof does not grant, explicitly, implicitly, or otherwise any rights or licenses to any third party with respect to patents or any other intellectual property whether with regard to
such Data Sheet Information itself or anything described by such information.
DATA SHEET INFORMATION DOES NOT CONSTITUTE A WARRANTY WITH RESPECT TO THE PRODUCTS DESCRIBED HEREIN, AND QORVO HEREBY
DISCLAIMS ANY AND ALL WARRANTIES WITH RESPECT TO SUCH PRODUCTS WHETHER EXPRESS OR IMPLIED BY LAW, COURSE OF DEALING, COURSE OF
PERFORMANCE, USAGE OF TRADE OR OTHERWISE, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE. Without limiting the generality of the foregoing, Qorvo® products are not warranted or authorized for use as critical components in medical, lifesaving, or life-
sustaining applications, or other applications where a failure would reasonably be expected to cause severe personal injury or death. Applications described in the Data Sheet
Information are for illustrative purposes only. Customers are responsible for validating that a particular product described in the Data Sheet Information is suitable for use in a
particular application.
FiRa, FiRa Consortium, the FiRa logo, the FiRa Certified logo, and FiRa tagline are trademarks or registered trademarks of FiRa Consortium or its licensor(s)/ supplier(s) in
the US and other countries and may not be used without permission. All other trademarks, service marks, and product or service names are trademarks or registered
trademarks of their respective owners.
© 2021-2025 Qorvo US, Inc. All rights reserved. This document is subject to copyright laws in various jurisdictions worldwide and may not be reproduced or distributed, in
whole or in part, without the express written consent of Qorvo US, Inc. | QORVO® is a registered trademark of Qorvo US, Inc.
