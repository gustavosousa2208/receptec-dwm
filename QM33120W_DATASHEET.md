QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
1 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Product Overview
The QM33120W is a single-chip Ultra-Wideband (UWB) low-
power low-cost transceiver device compliant to IEEE Std.
802.15.4‐2020 and IEEE Std. 802.15.4z‐2020 (BPRF mode).
The device can be used in TWR, TDoA, and AoA systems to WLCSP 52 3.1 mm x 3.5 mm
locate assets to an accuracy of 10 cm. They also provide a
precision location and data transfer simultaneously. Key Benefits
Key Features
• Asset location to an accuracy of 10 cm
• High multipath fading immunity
• IEEE 802.15.4-2020 UWB IEEE 802.15.4z-2020 (BPRF • Secure ranging/distance measurement using STS
mode) (Scrambled-Timestamp)
• Supports channels 5 & 9 (6489.6 MHz & 7987.2 MHz) • Supports high tag densities in real-time location systems (RTLS)
• Supports 2-way ranging, TDoA and AoA location schemes • Low-cost precision location
• Low external component count • Suitable for coin cell applications
• Supports enhanced Time-of-Flight security modes
• Integrated HW AES 256 Applications
• Low power consumption
• Data rates of 850 kbps, 6.8 Mbps
• Precision real-time location systems (RTLS) using TWR,
• Packet length from zero to 1023 bytes
TDoA, or AoA schemes in a variety of mobile, consumer,
• Custom modes to support ultra-short frame lengths
and industrial applications
• Integrated MAC support features
• Location-aware wireless sensor networks
• SPI interface to host MCU supports rates up to 32 MHz
• Geo-fencing safety
• Supports Japan and Korea regulatory compliance
• Secure access/payments
• Low latency wireless data comms
• Location-aware controls
Ordering Information
Part No. Description Quantity
QM33120WSR UWB Transceiver WLCSP dual RF port 100pc
QM33120WTR13 UWB Transceiver WLCSP dual RF port 3000pc
Functional Block Diagram
ANALOG RECEIVER POWER MANAGEMENT
PLL / CLOCK GENERATOR
REVIECSNAR
HOST INTERFACE / SPI TO HOST
T
LATIG
ID
ANALOG TRANSMITTER STATE CONTROLLER
PDoA Option
QM33120W
High-Level Block Diagram

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
2 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Table of Contents
5.14 REFERENCE CRYSTAL OSCILLATOR ....................... 29
IC DESCRIPTION ..............................................................4
5.14.1 Calculation of External Capacitor Values for
1.1 BACKWARD COMPATIBILITY WITH DW1000 AND Frequency Trim ............................................................ 29
DW3000 .............................................................................. 5
6 OPERATIONAL STATES ..................................... 31
2 PIN CONFIGURATION AND DESCRIPTIONS .....6
6.1 OVERVIEW ............................................................... 31
3 ELECTRICAL CHARACTERISTICS ................... 10 6.2 OPERATING STATE TRANSITIONS ........................... 32
3.1 ABSOLUTE MAXIMUM RATINGS ............................... 10 7 POWERING QM33120W ...................................... 33
3.2 NOMINAL OPERATING CONDITIONS ........................ 10
7.1 LOWEST BILL OF MATERIALS (BOM) POWERING
3.3 DC CHARACTERISTICS ............................................ 11
SCHEME .............................................................................. 35
3.4 RECEIVER AC CHARACTERISTICS .......................... 12
7.2 HIGHEST EFFICIENCY POWERING SCHEME ............. 35
3.5 RECEIVER SENSITIVITY CHARACTERISTICS ............ 13
7.3 PMIC POWERING SCHEME ..................................... 36
3.6 REFERENCE CLOCK AC CHARACTERISTICS ........... 14
7.4 TYPICAL TRANSMIT AND RECEIVE POWER PROFILES
3.7 TRANSMITTER AC CHARACTERISTICS .................... 15
37
3.8 LINK BUDGET ........................................................... 16
7.4.1 TX current profile for the minimal BOM ...... 37
3.9 TEMPERATURE AND VOLTAGE MONITOR
7.4.2 TX current profile for high efficiency modes
CHARACTERISTICS ............................................................. 16
38
3.10 LOCATION ACCURACY CHARACTERISTICS .............. 17
7.4.3 RX current profile for minimal BOM ............ 39
4 TYPICAL PERFORMANCE .................................. 18 7.4.4 RX Current Profile for High Efficiency BOMs
40
4.1 TRANSMIT SPECTRUM ............................................. 18
7.5 INTERNAL POWER SUPPLY DISTRIBUTION.............. 41
4.2 TRANSMIT POWER ADJUSTMENT ............................ 19
4.3 RECEIVER BLOCKING .............................................. 20 8 APPLICATION INFORMATION ........................... 42
4.4 RANGING .................................................................. 21
8.1 APPLICATION CIRCUIT DIAGRAM............................. 42
4.5 PDOA ...................................................................... 22
8.2 RECOMMENDED COMPONENTS .............................. 42
5 FUNCTIONAL DESCRIPTION ............................. 23 8.3 RECOMMENDED PCB LAYOUT AND STACKUP ....... 43
5.1 PHYSICAL LAYER MODES ........................................ 23 9 PACKAGING & ORDERING INFORMATION .... 44
5.2 SUPPORTED CHANNELS AND BANDWIDTHS ............ 23 9.1 52-BALL WLCSP .................................................... 44
5.3 SUPPORTED BIT RATES AND PULSE REPETITION 9.1.1 Package Dimensions .................................... 44
FREQUENCIES (PRF) ......................................................... 23
9.1.2 Recommended PCB Footprint ..................... 45
5.4 FRAME FORMAT IEEE STD. 802.15.4-2015, IEEE
9.1.3 Tape and Reel Packaging Information ....... 46
STD. 802.15.4™‐2020 ..................................................... 24
9.1.4 Device Package Marking .............................. 47
5.5 FRAME FORMATS OF IEEE STD. 802.15.4Z™‐2020
24 10 GLOSSARY ........................................................... 48
5.6 PROPRIETARY LONG FRAMES ................................. 25
11 REFERENCES ....................................................... 49
5.7 NO DATA FRAMES ................................................... 25
5.8 HOST CONTROLLER INTERFACE ............................. 25 12 REVISION HISTORY ............................................. 50
5.9 SPI FUNCTIONAL DESCRIPTION .............................. 25
13 HANDLING PRECAUTIONS ................................ 50
5.10 SPI TIMING PARAMETERS ....................................... 26
5.11 SPI OPERATING MODES ......................................... 27 14 SOLDERABILITY .................................................. 50
5.12 SPI TRANSACTION FORMATTING ............................ 28
15 FURTHER INFORMATION ................................... 51
5.13 GPIO AND SPI I/O INTERNAL PULL UP / DOWN .... 29

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
3 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
List of Figures
Figure 1: QM33120W Block Diagram .................................................. 4 Figure 19: Current Profile when Transmitting a Frame (6.8 Mbps) in
Figure 2: WLSCP Ball Configuration .................................................... 6 Lowest BOM Use Case ............................................................. 37
Figure 3: IEEE 802.15.4-2015 PPDU Structure ................................. 24 Figure 20: Current Consumption During TX for High Efficiency
Figure 4: IEEE Std. 802.15.4z™‐2020 HRP UWB PHY PPDU Formats Powering Modes ....................................................................... 38
.................................................................................................. 24 Figure 21: Typical current profiles when receiving a frame for different
Figure 5: SPI Timing Diagram............................................................ 26 types of applications .................................................................. 39
Figure 6: SPI Detailed Timing Diagram .............................................. 26 Figure 22: Current Consumption During RX for High Efficiency
Figure 7: QM33120W SPIPHA=0 Transfer Protocol .......................... 27 Powering Modes ....................................................................... 40
Figure 8: QM33120W SPIPHA=1 Transfer Protocol .......................... 27 Figure 23: Internal Power Distribution ............................................... 41
Figure 9: SPI Command Formatting .................................................. 28 Figure 24: QM33120W Application Circuit ......................................... 42
Figure 10: SPI and GPIO Pull Up / Down........................................... 29 Figure 25: Recommended PCB Layout and Stackup ......................... 43
Figure 11: Crystal Model.................................................................... 30 Figure 26: QM33120W Package Dimensions .................................... 44
Figure 12: Crystal Trim Plot ............................................................... 30 Figure 27: Recommended PCB Footprint .......................................... 45
Figure 13: Operating State Transitions .............................................. 32 Figure 28: Tape orientation and dimensions ...................................... 46
Figure 14: Timing Diagram for Cold Start POR .................................. 33 Figure 29: Reel Information – 330 mm Reel (13”) .............................. 46
Figure 15: Timing Diagram for Warm Start ........................................ 34 Figure 30: Device Package Marking .................................................. 47
Figure 16: Lowest BOM powering option ........................................... 35 Figure 31: Reflow Profile of the WLCSP package.............................. 50
Figure 17: Single O/P Buck SMPS option .......................................... 35
Figure 18: Mobile Option ................................................................... 36
List of Tables
Table 16: QM33120W Transmit Spectrum ........................................ 18
Table 1: QM33120W Pin functions ...................................................... 6 Table 17: QM33120W Transmit Power Adjustment ........................... 19
Table 2: Description of Abbreviations .................................................. 9 Table 18: QM33120W Receiver Blocking .......................................... 20
Table 3: QM33120W Absolute Maximum Ratings .............................. 10 Table 19: QM33120W Ranging ......................................................... 21
Table 4: QM33120W Nominal Operating Conditions .......................... 10 Table 20: QM33120W PDoA ............................................................. 22
Table 5: QM33120W DC Characteristics ........................................... 11 Table 21: QM33120W Supported Channels and Bandwidth .............. 23
Table 6: QM33120W AC Characteristics ........................................... 12 Table 22: QM33120W Supported Bit Rates and Pulse Repetition
Table 7: Test conditions of the Rx sensitivity measurements ............. 13 Frequencies .............................................................................. 23
Table 8: Rx Sensitivity Characteristics (Channel 5)............................ 13 Table 23: SPI Timing Parameters...................................................... 26
Table 9: Rx Sensitivity Characteristics (Channel 9)............................ 13 Table 24: Operating states ................................................................ 31
Table 10: QM33120W Reference Clock AC Characteristics .............. 14 Table 25: QM33120W Recommended Components ......................... 42
Table 11: QM33120W Transmitter AC Characteristics ....................... 15 Table 26: Glossary of Terms ............................................................. 48
Table 12: Link Budget ........................................................................ 16 Table 27: Document History .............................................................. 50
Table 13: Temperature and Voltage Monitor Characteristics ............. 16 Table 28: Critical Parameters for Lead Free Solder of the WLCSP
Table 14: QM33120W Ranging Accuracy Characteristics .................. 17 package .................................................................................... 51
Table 15: QM33120W PDoA/AoA Accuracy and Standard Deviation
(Std) .......................................................................................... 17

 QM33120W Datasheet
|     |     | ®   |     |     |     | UWB Low-Power Transceiver  |     |     |     |
| --- | --- | --- | --- | --- | --- | -------------------------- | --- | --- | --- |
IC Description
The QM33120W IC is a fully integrated low-power, single chip CMOS RF 6.5 GHz-8 GHz IR-UWB transceiver device compliant with the
IEEE 802.15.4-2020 (HRP UWB PHY), IEEE 802.15.4z–2020, and IEEE 802.15.8 standards.
QM33120W device consist of an analog front-end containing a receiver and a transmitter and a digital backend that interfaces to an off-
chip host processor. A TX/RX switch is used to connect the receiver or transmitter to the antenna port. Temperature and voltage monitors
are provided on-chip.
The receiver consists of an RF front-end which amplifies the received signal in a low-noise amplifier before down-converting it directly to
the baseband. The receiver is optimized for wide bandwidth, linearity, and noise figure. This allows each of the supported UWB channels
to be down-converted with minimum additional noise and distortion. The baseband signal is demodulated, and the resulting received data
is made available to the host controller via SPI.
The transmit pulse train is generated by applying digitally encoded transmit data to the analog pulse generator. The pulse train is up-
converted to a carrier generated by the synthesizer and centered on one of the permitted UWB channels. The modulated RF waveform
is amplified before transmission from the external antenna.
The device has two RF antenna ports and can be used for Angle of Arrival (AoA) applications. In this case, the receiver switches between
antenna ports to enable a PDoA measurement.
The QM33120W has an on-chip One-Time Programmable (OTP) memory. This memory can be used to store calibration data such as
TX power level and crystal initial frequency error adjustment. These adjustment values can be automatically retrieved when needed.
DIGITAL RX
|     |     |     |     | ADC |                | Carrier/      |       | SECDED/                                     |       |
| --- | --- | --- | --- | --- | -------------- | ------------- | ----- | ------------------------------------------- | ----- |
|     |     |     |     |     | Digital Filter | T im in g     | D     | e - V i te r b i  R e e d-                  |       |
|     |     |     |     |     |                | R ec ov e r y | spr e | a der D e c o d e r S D o e l c o o m d o e | n r   |
ANT1
|     |     |     | Rx Analog  |     |     |                |     |                 | SPICSn               |
| --- | --- | --- | ---------- | --- | --- | -------------- | --- | --------------- | -------------------- |
|     |     |     | Baseband   |     | CIA | Configuration  |     | Host Interface  | SPICLK SPI interface |
|     |     |     |            |     |     | Retention      |     | H/ W   SPI      | S P IM O S I         |
| RF1 |     |     |            |     |     |                |     | M A C           | S P I M IS O         |
IRQ
| ANT2 |     | RF RX |     |     |                 | OTP          |     | Timers    |            |
| ---- | --- | ----- | --- | --- | --------------- | ------------ | --- | --------- | ---------- |
|      |     |       |     |     | IF Gain Control |              |     | Register  |            |
|      |     |       |     |     |                 | Digital AON  |     | File AES  | GPIO[0..7] |
|      |     |       |     |     |                 |              | I/F | 256       |            |
RF2
|     | PDOA |     |     | Pulse Generator | To all digital blocks |     |     |     |     |
| --- | ---- | --- | --- | --------------- | --------------------- | --- | --- | --- | --- |
option
R e e d -
|     |     |       |     |     | B u r s t  Co | n v o lu t io n a S | o l o m o n   SECDED | T ra n s m it  |      |
| --- | --- | ----- | --- | --- | ------------- | ------------------- | -------------------- | -------------- | ---- |
|     |     |       |     |     | C o n t ro l  | l  E n c o d e r E  | n c o d e r          | C o n tr ol    |      |
|     |     | RF TX |     |     | DIGITAL TX    |                     |                      |                |      |
|     |     |       |     |     |               |                     | To all               | AON            | VDD1 |
circuits
|     |     |     |                  |               |     | DIGIT A L   C | o ntrol      | C A S                                  |     |
| --- | --- | --- | ---------------- | ------------- | --- | ------------- | ------------ | -------------------------------------- | --- |
|     |     |     |                  | L o o p       |     |               | M a n        | P a o g w e m e r e   n t  M e m o ry  |     |
|     |     |     |                  | C ir c u it s |     | T x  /   R    | x            | A r ra y                               |     |
|     |     |     | RF PLL / Clk Gen |               |     | Calibration   | a n Control  | d   S t a t e                          |     |
(PMSC)
| VDD2a |     |     |     |     |     |     |     | 20kHz  |     |
| ----- | --- | --- | --- | --- | --- | --- | --- | ------ | --- |
Osc
| VDD2b | On-Chip    | To all   |      | To all              |     |               |       |             |     |
| ----- | ---------- | -------- | ---- | ------------------- | --- | ------------- | ----- | ----------- | --- |
|       | Regulators | circuits | Bias | circuits Oscillator |     | Te m p e r a  | tu re |             |     |
| VDD3  |            |          |      |                     |     | /  V o lt a g | e     | P B O O R D |     |
monitor
|     |     |         |     | TI O |     |     |     | PUEKAW | N   |
| --- | --- | ------- | --- | ---- | --- | --- | --- | ------ | --- |
|     |     | D_XTV D |     | X TX |     |     |     | nTSR   | O   |
|     |     | _O      |     |      |     |     |     |        | TXE |
VI

Figure 1: QM33120W Block Diagram

© 2025 Qorvo US, Inc. | Rev. D
4 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
5 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
The Always-On (AON) memory is 256 bytes and can be used to retain QM33120W configuration data during the lowest power operational
states. The AON can operate directly from the battery. This data is downloaded during crystal startup automatically.
The QM33120W contains a phase-locked-loop (PLL) with integrated loop filters. This PLL provides the RF local oscillator signals for the
Rx Mixer and the Tx RF frequency carrier to the Tx mixer. The channel information signal defines the output channel frequency as follows;
channel 5 = 6489.6 MHz, channel 9 = 7987.2 MHz.
The QM33120W has various debug and test options (RF loopback, event counters, test modes, and more) and gives access to internal
signals for on-the-bench debugging and to simplify production tests.
The QM33120W incorporates timestamp system security features to prevent all known hacking-type attacks such as ‘imposter’, ‘cicada’,
‘parasite’ ‘record & replay’ attacks, etc. MAC features implemented include CRC generation, CRC checking, and receive frame filtering.
1.1 Backward Compatibility with DW1000 and DW3000
The QM33120W can be configured to be backward compatible over-the-air with DW1000 on channel 5 and for data rates of 6.8 Mbps
and 850 kbps. It is also compatible with the DW3000 product line with some differences primarily related to GPIO functionality, the SPI
boot mode cannot be configured by GPIO states at power on, the OTP method can still be used.

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
6 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
2 Pin Configuration and Descriptions
The QM33120W is supplied in WLSCP (52 balls) package. The pin assignments are illustrated below, and the description is given in
the following table.
Figure 2: WLSCP Ball Configuration
Table 1: QM33120W Pin functions
CSP Signal I/O Type
Description
Ball # Name (Default)
Interrupt Request output from the QM33120W to the host processor. By default, IRQ is an
active-high output but may be configured to be active-low if required. For correct operation in
SLEEP and DEEPSLEEP modes, it should be configured for active-high operation. This pin
will float in SLEEP and DEEPSLEEP states and may cause spurious interrupts unless
B1 IRQ/GPIO8 DIO (O-L) pulled low.
When the IRQ functionality is not being used the pin may be reconfigured as a general
purpose I/O line, GPIO8.
General purpose I/O pin. After power-up, the pin will default to a General Purpose I/O
pin. It may be configured EXTSWRX, the pin will go high at the start of RX. When it’s
GPIO6
configured as PDoA_SW2, the pin will go high when the device is receiving or
C4 /EXTSWRX/ DIO (I)
transmitting from RF port 1, used for AoA switching (within a frame), switches within 1us
PDoA_SW2
STS gap.
General purpose I/O pin. After power-up, the software should change its function to the
RESV/
desired user mode. It may be configured for use as an RXOKLED driving pin can be
C2 RXOKLED/ DIO(I)
used to indicate the reception of a good frame.
GPIO0

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
7 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
CSP Signal I/O Type
Description
Ball # Name (Default)
General purpose I/O pin. After power-up, the software should change its function to the
RESV/
desired user mode. It may be configured for use as an SFDLED driving pin that can be
D3 SFDLED/ DIO (O-L)
used to indicate when SFD (Start Frame Delimiter) is found by the receiver. It can also
GPIO1
be configured as a GPIO.
GPIO5/COEX General purpose I/O pin, after power-up, the pin will default to a General Purpose I/O
OUT/ pin. It may be configured for use as COEX_OUT, an output flag that can be configured
D1 DIO (I)
EXTRXE via software to indicate an RX, TX, or both. It can also be configured as PDoA_SW1, in
(PDoA_SW1) this mode, it will go high 100ns before the start of RX.
RESV/ General purpose I/O pin. After power-up, the software should change its function to the
E4 RXLED/ DIO (I) desired user mode. It may be configured for use as an RXLED driving pin that can be
GPIO2 used to indicate receive mode. It can also be configured as a GPIO.
RESV/ General purpose I/O pin. After power-up, the software should change its function to the
E2 TXLED/ DIO (I) desired user mode. It may be configured for use as a TXLED driving pin that can be
GPIO3 used to indicate a transmission. It can also be configured as a GPIO.
General purpose I/O pin, after power-up, the pin will default to a General Purpose I/O
GPIO4/
pin. It may be configured for use as COEXIN, an input used to abort any ongoing TX or
COEXIN/
F1 DIO (I) RX RF operations. Can be SW configured for TX, RX, or both. It may be configured
EXTTXE
PDoA_SW0, it will stay on during TX frame, turning on approx. 10us before the first TX
(PDoA_SW0)
pulse.
The SYNC input pin is used for external synchronization. When the SYNC input
functionality is not being used this pin may be reconfigured as a general purpose I/O
SYNC/GPIO7
G2 DIO (I) pin, GPIO7. When it’s configured as PDoA_SW3, the pin will go high when the device is
/PDoA_SW3
receiving or transmitting from RF port 2. Used for AoA switching (within a frame),
switches within 1us STS gap.
E6
E10
GND G RF ground pin *.
E12
F7
RF port for antenna 2 (50 Ω single-ended RF connection). When in use for PDoA
variant, a 2 pF is required on the pin. When not in use for PDoA variant, a 2pF is
H5 RF2 AIO
required on the pin with 50 Ω termination. In non PDoA chip variants, the 2 pF is not
required, but the pin should be grounded with 50 Ω.
F9
G6
G10 GND G RF ground pin *.
H1
H3
H11 RF1 AIO RF port for antenna 1 (50 Ω single-ended connection). A 2 pF is required on the pin.
H7
H9 GND G RF ground pin *.
H13
H15 XTI AI Reference crystal input or external reference overdrive pin.
G14 XTO AI Reference crystal output.
Voltage Supply (2.4 V to 3.6 V) *
F15 VDD2a P
A ferrite bead must be placed between B15 and F15 for isolation.
Voltage Supply (2.4 V to 3.6 V) *
B15 VDD2b P
A ferrite bead must be placed between B15 and F15 for isolation.

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
8 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
CSP Signal I/O Type
Description
Ball # Name (Default)
D11
VSS2 G Ground return for VDD2.
E14
C12
VSS3 G Ground return for VDD3.
D13
D15 VDD3 P Voltage Supply (1.5 V to 3.6 V).
C14 VTX_D PD TX supply decoupling. Requires external capacitor to ground *.
Main power supply (1.62V – 3.6V). Should be Always ON *. The following I/Os are
B13 VDD1 P supplied by this pin: WAKEUP, EXTON, RSTn, SPICLK, SPICSn, SPICDI, and
SPICDO.
A14
B11
C8 VSS1 G Ground return for VDD1, also PSUB (substrate) connection.
C10
D9
External device enable. Asserted during the wake-up process and held active until the
device enters sleep mode. Can be used to control external DC-DC converters or other
A12 EXTON DO (O-L)
circuits that are not required when the device is in sleep mode to minimize power
consumption.
When asserted into its active high state, the WAKEUP pin brings the QM33120W out of
RESV/
B9 DI SLEEP or DEEPSLEEP states into operational mode. This should be connected to the
WAKEUP
ground if not used. When this pin is pulled low, the D3 pin will drive as an output.
Reset pin. Active-Low Input-Output.
May be pulled low by the external open-drain driver to reset the QM33110W. Must not
A10 RSTn DIO (O-H) be pulled high by the external source.
When power is supplied to the device, RSTn is driven high by the Power-On Reset
(POR) circuit. Refer to section 2.5 in the user manual for more details.
A8 SPICLK DI SPI peripheral clock input.
SPICDO
B7 DI SPI peripheral data input (Controller data output).
(SPIMOSI)
SPICDI
A6 D0 (O-L) SPI peripheral data output (Controller data input).
(SPIMISO)
SPI chip select. This is an active-low enable input. The high-to-low transition on
B5 SPICSn DI SPICSn signals the start of a new SPI transaction. SPICSn can also act as a wake-up
signal to bring QM33120W out of either SLEEP or DEEPSLEEP states.
IO supply decoupling. Internally connected to the VDD1 with a switch to allow
A4 VIO_D PD
disconnect from VDD1 for ultra-low-power consumption mode.
A2
B3
C6 VSS G Ground return for internal digital supply *.
D5
D7
(*) Refer to design guide schematics and layout.
Note: GPIO pins are not suitable to drive LEDs directly. See Table 6 for details of the maximum current limit. Any signal with the suffix ‘n’ indicates an active low signal.

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
9 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Table 2: Description of Abbreviations
Abbreviation Description
AI Analog Input.
AIO Analog Input / Output.
AO Analog Output.
DI Digital Input.
DIO Digital Input / Output.
DO Digital Output.
G Ground.
P Power Supply.
PD Power Decoupling.
NC No Connect.
O-L Defaults to output, low level after reset.
O-H Defaults to output, high level after reset.
I Defaults to input.
Reserved mode, Defaults to input. Software should switch to preferred mode
RESV
on start up.

 QM33120W Datasheet
® UWB Low-Power Transceiver
3  Electrical Characteristics

3.1   Absolute Maximum Ratings

Table 3: QM33120W Absolute Maximum Ratings
|                         | Parameter  |     | Min.  | Max.  | Units  |
| ----------------------- | ---------- | --- | ----- | ----- | ------ |
| Supply voltage          |            |     | -0.3  | 4.0   | V      |
| Receiver power          |            |     |       | 14    | dBm    |
| Storage temperature     |            |     | -65   | +150  | ˚C     |
| Operating temperature   |            |     | -40   | +85   | ˚C     |

3.2  Nominal Operating Conditions
Table 4: QM33120W Nominal Operating Conditions
|                                 | Parameter    | Min.  Typ.  | Max.  Units  | Condition / Note  |     |
| ------------------------------- | ------------ | ----------- | ------------ | ----------------- | --- |
| Receiver power                  |              |             | 14  dBm      |                   |     |
| Supply voltage VDD1             |              | 1.62  3.0   | 3.6  V       |                   |     |
| Supply voltage VDD2a and VDD2b  |              | 2.4  3.0    | 3.6  V       |                   |     |
| Supply voltage VDD3             |              | 1.5  3.0    | 3.6  V       |                   |     |
Voltage on GPIO0-5, WAKEUP, RSTn, SPICSn,  VDD1 +  Note that 3.6 V is the max voltage
|                 |     |       | V     |                                        |     |
| --------------- | --- | ----- | ----- | -------------------------------------- | --- |
| SPICDO, SPICLK  |     |       | 0.3V  | that should be applied to these pins.  |     |
Note: Unit operation is guaranteed by design when operating within these ranges.
Sufficient headroom for any power supply voltage ripple should be considered in system designs.

© 2025 Qorvo US, Inc. | Rev. D
|     |     | 10 of 53  |     |     | www.qorvo.com   |
| --- | --- | --------- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     | ®   |     |     |     | UWB Low-Power Transceiver  |     |
| --- | --- | --- | --- | --- | -------------------------- | --- |
3.3  DC Characteristics
Conditions Tamb = 25 ˚C, all supplies at 3.0 V unless otherwise stated.
Table 5: QM33120W DC Characteristics
|     | Parameter  | Min.  | Typ.  | Max.  Unit |     | Condition / Note  |
| --- | ---------- | ----- | ----- | ---------- | --- | ----------------- |
s
| Supply current DEEP SLEEP mode    |     |     | 210  |   nA  |     |     |
| --------------------------------- | --- | --- | ---- | ----- | --- | --- |
| Supply current SLEEP mode         |     |     | 810  |   nA  |     |     |
| Supply current IDLE_PLL mode CH5  |     |     | 16   |   mA  |     |     |
| Supply current IDLE_PLL mode CH9  |     |     | 29   |   mA  |     |     |
| Supply current IDLE_RC mode       |     |     | 6    |   mA  |     |     |
| Supply current INIT mode          |     |     | 4    |   mA  |     |     |
Current single frame Tx/Rx with 47uF capacitor
| TX CH5  |     |     | 14  |   mA  |     |     |
| ------- | --- | --- | --- | ----- | --- | --- |
Refer to section 7.4.1 for details of single TX
| TX CH9  |     |     | 17  |   mA  | frame configuration.  |     |
| ------- | --- | --- | --- | ----- | --------------------- | --- |
| RX CH5  |     |     | 16  |   mA  |                       |     |
Refer to section 7.4.3 for details of single
| RX CH9  |     |     | 19  |   mA  | RX frame configuration.  |     |
| ------- | --- | --- | --- | ----- | ------------------------ | --- |
Peak current continuous Tx/Rx
TX CH5
|     |     | 32  | 40  | 53  mA  |     |     |
| --- | --- | --- | --- | ------- | --- | --- |
(nominal power -41.3 dBm/MHz)
TX CH5
-40°C to 85°C
|     |     | 40  | 52  | 67  mA  |     |     |
| --- | --- | --- | --- | ------- | --- | --- |
(max power -32.3 dBm/MHz)  Continuous TX is only used as test mode. In
a typical operation, TX is powered up for
TX CH9
|     |     | 43  | 50  | 62  mA  |     |     |
| --- | --- | --- | --- | ------- | --- | --- |
(nominal power -41.3 dBm/MHz)  frame transmission then powered down.
TX CH9
|     |     | 51  | 62  | 76  mA  |     |     |
| --- | --- | --- | --- | ------- | --- | --- |
(max power -32.3 dBm/MHz)
| RX CH5  |     | 47  | 68  | 89  mA   |     | -40°C to 85°C.  |
| ------- | --- | --- | --- | -------- | --- | --------------- |
| RX CH9  |     | 61  | 82  | 107  mA  |     | -40°C to 85°C.  |
0.7 *
| Digital input voltage high  |     |     |     |    V  |     |     |
| --------------------------- | --- | --- | --- | ----- | --- | --- |
VDD1
0.3 *
| Digital input voltage low   |     |     |     | V   |     |     |
| --------------------------- | --- | --- | --- | --- | --- | --- |
VDD1
0.7 *
| Digital output voltage high  |     |     |     |    V  |     | Assumes 500 Ω load.  |
| ---------------------------- | --- | --- | --- | ----- | --- | -------------------- |
VDD1
0.3 *
| Digital output voltage low  |     |     |     | V   |     | Assumes 500 Ω load.  |
| --------------------------- | --- | --- | --- | --- | --- | -------------------- |
VDD1
Digital Output Drive Current
| GPIOx, IRQ  |     | 4   | 6   |   mA  |     |     |
| ----------- | --- | --- | --- | ----- | --- | --- |
| SPICDI      |     | 8   | 10  |   mA  |     |     |
| EXTON       |     | 3   | 4   |   mA  |     |     |

|     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
11 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

 QM33120W Datasheet
® UWB Low-Power Transceiver
3.4  Receiver AC Characteristics

Conditions Tamb = 25 ˚C, all supplies at 3.0 V unless otherwise stated.
Table 6: QM33120W AC Characteristics

|                               | Parameter    | Min.  Typ.  | Max.  Units  | Condition / Note  |     |
| ----------------------------- | ------------ | ----------- | ------------ | ----------------- | --- |
| Center Frequency - Channel 5  |              |   6489.6    |   MHz        |                   |     |

| Center Frequency - Channel 9  |     |   7987.2  |   MHz  |     |     |
| ----------------------------- | --- | --------- | ------ | --- | --- |

| Channel bandwidth  |     |   499.2  |   MHz  | Programmable.  |     |
| ------------------ | --- | -------- | ------ | -------------- | --- |
Input P1dB compression point
|     |     |   -25.4  |   dBm  |     |     |
| --- | --- | -------- | ------ | --- | --- |
Channel 5
Input P1dB compression point.
|     |     |   -27.1  |   dBm  |     |     |
| --- | --- | -------- | ------ | --- | --- |
Channel 9
Chip-referred (power at the pin) to
| In-band blocking level  |     |   -82.3  |   dBm  |     |     |
| ----------------------- | --- | -------- | ------ | --- | --- |
give 1% PER with 3 dB desense.
Chip-referred (power at the pin) to
Out-of-band blocking level    -22.3    dBm  give 1% PER with 3 dB desense
(see Receiver Blocking).

© 2025 Qorvo US, Inc. | Rev. D
|     |     | 12 of 53  |     |     | www.qorvo.com   |
| --- | --- | --------- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
® UWB Low-Power Transceiver
3.5  Receiver Sensitivity Characteristics

The receiver sensitivity was measured at the 1% Packet Error Rate (PER).

Table 7: Test conditions of the Rx sensitivity measurements
|     | Parameter  | Value  |     |     |
| --- | ---------- | ------ | --- | --- |
Ambient Temperature  25 ˚C
Supply voltage   3.0V
PHR rate (of data packets)  850Kbps
Payload length (of data packets)  20 bytes
Preamble Code  9
PRF (Pulse repetition frequency)  64 MHz
SFD Type for Preamble for frames length 1024  Decawave-defined 16-symbol SFD
SFD Type for Preamble for frames length 64  IEEE 802.15.4z defined 8-symbol SFD
Carrier frequency offset  ±10 ppm

Table 8: Rx Sensitivity Characteristics (Channel 5)
Typical Receiver
Preamble length
STS length
| Sensitivity  | Data Rate  |            |            | Conditions  |
| ------------ | ---------- | ---------- | ---------- | ----------- |
|              |            | (symbols)  | (symbols)  |             |
(dBm/500 MHz)
| -93   | 6.8 Mbps  | 64    | 64   | See Table 7  |
| ----- | --------- | ----- | ---- | ------------ |
| -100  | 850 Kbps  | 1024  | n/a  | See Table 7  |
No data mode.
| -98  | -   | 64  | 64  |     |
| ---- | --- | --- | --- | --- |
See Table 7

Table 9: Rx Sensitivity Characteristics (Channel 9)
Typical Receiver
|              | Preamble length  |            | STS length  |             |
| ------------ | ---------------- | ---------- | ----------- | ----------- |
| Sensitivity  | Data Rate        |            |             | Conditions  |
|              |                  | (symbols)  | (symbols)   |             |
(dBm/500 MHz)
| -91   | 6.8 Mbps  | 64    | 64   | See Table 7  |
| ----- | --------- | ----- | ---- | ------------ |
| -98   | 850 Kbps  | 1024  | n/a  | See Table 7  |
No data mode.
| -96  | -   | 64  | 64  |     |
| ---- | --- | --- | --- | --- |
See Table 7

© 2025 Qorvo US, Inc. | Rev. D
|     | 13 of 53  |     |     | www.qorvo.com   |
| --- | --------- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     | ®   |     |     | UWB Low-Power Transceiver  |     |
| --- | --- | --- | --- | -------------------------- | --- |
3.6  Reference Clock AC Characteristics
Tamb = 25 ˚C, all supplies at 3.0 V.
Table 10: QM33120W Reference Clock AC Characteristics
|     | Parameter  | Min.  Typ.  | Max.  Units  |     | Condition / Note  |
| --- | ---------- | ----------- | ------------ | --- | ----------------- |
A 38.4 MHz signal can be provided from an
Crystal oscillator reference frequency    38.4    MHz  external reference in place of a crystal if
desired.
Crystal specifications
 Depends on the crystal used and PCB
Load capacitance  10      pF  parasitics. See section Reference Crystal
Oscillator
| Shunt capacitance  |     | 0    | 4  pF  |     |     |
| ------------------ | --- | ---- | ------ | --- | --- |
Drive level      200  µW  Depends on the crystal & load capacitance
used.
| Equivalent Series Resistance (ESR)  |     |     | 60  Ω  |     |     |
| ----------------------------------- | --- | --- | ------ | --- | --- |
QM33120W includes circuitry to trim the
Frequency tolerance      ±20  ppm  crystal oscillator to reduce the initial
frequency offset.
Trimming range provided by on-chip
Crystal trimming range  -20    +20  ppm  circuitry. Depends on the crystal used and
PCB design.

External Reference (For example a TCXO)
Must be AC coupled. A coupling capacitor
| Amplitude  |     | 0.8    | VDD2  Vpp  |     |     |
| ---------- | --- | ------ | ---------- | --- | --- |
value of 2200 pF is recommended.
| SSB phase noise power density  |     |     | -132  dBc / Hz  |     |     |
| ------------------------------ | --- | --- | --------------- | --- | --- |
@1 kHz offset.
| SSB phase noise power density  |     |     | -145  dBc / Hz  |     |     |
| ------------------------------ | --- | --- | --------------- | --- | --- |
@10 kHz offset.
| Duty Cycle  |     | 40    | 60  %  | Duty Cycle  |     |
| ----------- | --- | ----- | ------ | ----------- | --- |
Fast and Slow Oscillators
User programmable*. Minimum at
| Slow RC Oscillator  |     | 9  23  | 30  kHz  |     |     |
| ------------------- | --- | ------ | -------- | --- | --- |
VDD1=1.62 V, Maximum at VDD1=3.6 V
 Internally calibrated. Minimum at -40°C,
| Fast RC Oscillator  |     | 105  115  | 130  MHz  |     |     |
| ------------------- | --- | --------- | --------- | --- | --- |
maximum at +85°C
*Note: Chip start-up time depends on this clock. The typical frequency of the Slow RC oscillator is reflected in the chip start-up time of 913 us. With the time, required
to download the AON after wake-up, the overall start-up time is ~1000 us. It is possible to trim the oscillator to the higher frequency in software, which would
decrease the start-up time to ~770 us.

|     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
14 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     | ®   |     |     | UWB Low-Power Transceiver  |     |
| --- | --- | --- | --- | -------------------------- | --- |
3.7  Transmitter AC Characteristics
Tamb = 25 ˚C, all supplies at 3.0 V.
Table 11: QM33120W Transmitter AC Characteristics
|                                | Parameter    | Min.  Typ.  | Max.  Units  |                | Condition / Note  |
| ------------------------------ | ------------ | ----------- | ------------ | -------------- | ----------------- |
| Center Frequency - Channel 5   |              |   6489.6    |   MHz        |                |                   |
| Center Frequency - Channel 9   |              |   7987.2    |   MHz        |                |                   |
| Channel bandwidth              |              |   499.2     |   MHz        | Programmable.  |                   |
Maximum Output power spectral
|     |     |   -32  |     |     |     |
| --- | --- | ------ | --- | --- | --- |
density Channel 5 (programmable)  dBm /
MHz
Maximum Output power spectral
|     |     |   -33.5  |     |     |     |
| --- | --- | -------- | --- | --- | --- |
density Channel 9 (programmable)
Maximum Output channel power
|     |     |   -6  |     |     |     |
| --- | --- | ----- | --- | --- | --- |
Channel 5  dBm/
500 MHz
Maximum Output channel power
|     |     |   -7.5  |     |     |     |
| --- | --- | ------- | --- | --- | --- |
Channel 9
| Load impedance  |     |   50  |   Ω  | Single ended  |     |
| --------------- | --- | ----- | ---- | ------------- | --- |
Power level range    30    dB  See Transmit Power Adjustment
Output power variation with
|     |     |   0  | 0.008  dB / °C   |     |     |
| --- | --- | ---- | ---------------- | --- | --- |
temperature
Output power variation with voltage    0    dB / V  Internally regulated
Transmit bandwidth variation with
|     |     |   0.2  |   MHz / °C   | With internal calibration enabled  |     |
| --- | --- | ------ | ------------ | ---------------------------------- | --- |
temperature

|     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
15 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     |     |     | ®   |     |     | UWB Low-Power Transceiver  |     |     |
| --- | --- | --- | --- | --- | --- | -------------------------- | --- | --- |
3.8  Link Budget

Using the receiver sensitivity above, expected transmission link budgets can be estimated with the following assumptions:

•  Receiver sensitivities as per the Table 8 and Table 9. .
•  Transmitter and receiver antennas have 0 dBi gain.
•  No losses between the antenna and QM33120W RF pins.

The link budget is calculated as follows:
Link Budget (dB) = Maximum Output Channel Power (dBm/500 MHz) – Receiver Sensitivity Level (dBm/500 MHz)

Table 12: Link Budget
Typical Link
| Budget (dB)  |           |            | Preamble Length  |     | STS length  |     |     |                   |
| ------------ | --------- | ---------- | ---------------- | --- | ----------- | --- | --- | ----------------- |
|              |           | Data Rate  |                  |     |             |     |     | Condition / Note  |
|              |           |            | (symbols)        |     | (symbols)   |     |     |                   |
CH5        CH9
With 12 bytes data
| 94  | 90.5  | 850 Kbps  | 1024  |     | n/a  |     |     |     |
| --- | ----- | --------- | ----- | --- | ---- | --- | --- | --- |
→ max output power settings are used.
With 12 bytes data
| 87   | 83.5  | 6.8 Mbps  | 64  |     | 64  |     |     |     |
| ---- | ----- | --------- | --- | --- | --- | --- | --- | --- |
→ max output power settings are used.
With 12 bytes data
| 92  | 88.5  | -   | 64  |     | 64  |     |     |     |
| --- | ----- | --- | --- | --- | --- | --- | --- | --- |
→ max output power settings are used.

3.9  Temperature and Voltage Monitor Characteristics
Table 13: Temperature and Voltage Monitor Characteristics
|                               | Parameter  |     | Min.   | Typ.  | Max.  | Units  |     | Condition / Note  |
| ----------------------------- | ---------- | --- | ------ | ----- | ----- | ------ | --- | ----------------- |
| Voltage Monitor Range         |            |     | 1.62   |       | 3.6   | V      |     |                   |
| Voltage Monitor Accuracy      |            |     |        | 5     |       | %      |     |                   |
| Temperature Monitor Range     |            |     | -40    |       | 85    | °C     |     |                   |
| Temperature Monitor Accuracy  |            |     | -3     |       | 3     | %      |     |                   |

|     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
16 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     |     |     | ®   |     |     |     |     | UWB Low-Power Transceiver  |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | -------------------------- | --- | --- |

3.10  Location Accuracy Characteristics

Table 14: QM33120W Ranging Accuracy Characteristics
|                    | Parameter  |     |     | Min.  | Typ.  | Max.  | Units  |                | Condition / Note  |     |
| ------------------ | ---------- | --- | --- | ----- | ----- | ----- | ------ | -------------- | ----------------- | --- |
| Ranging accuracy1  |            |     |     | -6    |       | 6     | cm     | BPRF3 packet.  |                   |     |
Ranging standard deviation    3    cm  In line-of-sight conditions.
1. After antenna delay calibration is applied.

Table 15: QM33120W PDoA/AoA Accuracy and Standard Deviation (Std)
|          |       |         | Typical   |     | Equivalent  | Typical    |     | Equivalent  |     |               |
| -------- | ----- | ------- | --------- | --- | ----------- | ---------- | --- | ----------- | --- | ------------- |
|          |       |         | PDoA      |     | AoA         | PDoA       |     | AoA         |     |               |
| Channel  | PLEN  | STSLEN  |           |     |             |            |     |             |     | Note          |
|          |       |         | Accuracy  |     | Accuracy    | Standard   |     | Standard    |     |               |
|          |       |         | (deg)     |     | (deg) [4]   | Dev (deg)  |     | Dev (deg)   |     |               |
|          | 128   | 256     | TBC       |     | TBC         | 4          |     | 2           |     | PDoA mode 31  |
5
PDoA mode 12 & Fira
|     | 64  | 64  | TBC  |     | TBC  | 7   |     | 3.5  |     |     |
| --- | --- | --- | ---- | --- | ---- | --- | --- | ---- | --- | --- |
compatible3
|     | 128  | 256  | ±12.5  |     | ±6.25  | 5   |     | 2.5  |     | PDoA mode 31  |
| --- | ---- | ---- | ------ | --- | ------ | --- | --- | ---- | --- | ------------- |
9
PDoA mode 12 & Fira
|     | 64  | 64  | TBC  |     | TBC  | 11  |     | 5.5  |     |     |
| --- | --- | --- | ---- | --- | ---- | --- | --- | ---- | --- | --- |
compatible3
1. PDoA mode 3 can be used when a long STS frame is enabled (PLEN/STSLEN>128). In this mode, the RF port is switched in the middle of the frame, so the phase is
measured on two segments of the STS frame, on two antenna ports.
2. PDoA mode 1 is used when short frame lengths are enabled (PLEN/STSLEN=64). In this mode, the RF port is switched between frames, so the phase is measured on
Preamble on one port, then STS on the other port.
3. Fira standard specifies UWB configuration with PLEN=64 and STSLEN=64.
4. Note: in a typical PDoA based system the computed angle of arrival (AoA) accuracy is better than the PDoA accuracy by a factor of approximately two i.e., if PDoA
accuracy is ±12.5° then AoA accuracy is ±6.25°.
5. For optimal PDoA performance a carrier frequency offset of greater than |5| ppm is required between devices.
|     |     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
17 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
18 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
4 Typical Performance
4.1 Transmit Spectrum
The typical transmit spectrums for channel 5 and channel 9 are in the pictures below.
Table 16: QM33120W Transmit Spectrum
Typical Transmit Spectrum Channel 5 Maximum Transmit Spectrum Channel 5
Typical Transmit Spectrum Channel 9 Maximum Transmit Spectrum Channel 9
Maximum Transmit Spectrum Channel 9, with
Nominal Transmit Spectrum Channel 9, with
alternative pulse shape
alternative pulse shape

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
19 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
4.2 Transmit Power Adjustment
QM33120W has a coarse TX power adjustment and a fine TX power adjustment. The plots below show the relationship between these
adjustments for each channel.
Table 17: QM33120W Transmit Power Adjustment
Tx power coarse and fine gain settings Channel 5 Tx current consumption for coarse and fine gain
settings Channel 5
Tx power coarse and fine gain settings Channel 9 Tx current consumption for coarse and fine gain
settings Channel 5

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
20 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
4.3 Receiver Blocking
The following plots show typical blocking levels to give 1% UWB PER at 3 dB back off from the sensitivity point. The UWB configuration is:
• PRF = 64 MHz
• Preamble length = 64 symbols
• STS length = 64 symbols
Table 18: QM33120W Receiver Blocking
Blocking Performance Channel 5 Blocking Performance Channel 9

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
21 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
4.4 Ranging
The typically measured distribution of ranging performance (LOS) for 10000 samples.
Table 19: QM33120W Ranging
Double-sided Ranging Performance Channel 5 Single-sided Ranging Performance Channel 5
Double-sided Ranging Performance Channel 9 Single-sided Ranging Performance Channel 9

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
22 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
4.5 PDoA
The following plots show typical measured PDoA performance. The UWB configuration is as follows:
• PRF = 64 MHz
• Preamble length = 128
• STS length = 256
• PDoA Mode = 3
Table 20: QM33120W PDoA
PDoA Performance Channel 5 PDoA Performance Channel 9

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
23 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
5 Functional Description
5.1 Physical Layer Modes
Please refer to IEEE Std. 802.15.4™‐2020 and IEEE Std. 802.15.4z™‐2020 for the PHY specification.
5.2 Supported Channels and Bandwidths
The QM33120W supports the following IEEE Std. 802.15.4™‐2020 and IEEE Std. 802.15.4z™‐2020 UWB channels:
Table 21: QM33120W Supported Channels and Bandwidth
UWB Channel Number Center Frequency (MHz) Bandwidth (MHz)
5 6489.6 499.2
9 7987.2 499.2
5.3 Supported Bit Rates and Pulse Repetition Frequencies (PRF)
The QM33120W supports IEEE Std. 802.15.4-2015, IEEE Std. 802.15.4™‐2020 UWB standard bit rates 850 kbps and 6.81 Mbps and
nominal PRF values of 16 MHz and 64 MHz, the Base PRF (BPRF) mode of a newly defined draft standard IEEE Std. 802.15.4z™‐
2020 is also supported.
Table 22: QM33120W Supported Bit Rates and Pulse Repetition Frequencies
PRF (MHz) Data Rate (Mbps)
16* 0.85
16* 6.81
64** 0.85
64** 6.81
Actual PRF mean values are slightly higher for SHR as opposed to the other portions of a frame. Mean PRF values are 16.1/15.6 MHz and 62.89/62.4 MHz, nominally
referred to as 16 MHz and 64 MHz in this document. Refer to [1], [2] (UWB PHY rate-dependent and timing-related parameters) for full details of peak and mean PRFs.
* Backward-compatible for IEEE Std. 802.15.4-2015 UWB devices
** Base PRF (BPRF) mode of IEEE Std. 802.15.4z™‐2020 and IEEE Std. 802.15.4-2015
In general, lower data rates give increased receiver sensitivity, increased link margin, and longer range but due to longer frame lengths
for a given number of data bytes they result in increased air occupancy per frame and a reduction in the number of individual
transmissions that can take place per unit time.
16 MHz PRF gives a marginal reduction in transmitter power consumption over 64 MHz PRF (BPRF).

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
24 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
5.4 Frame Format IEEE Std. 802.15.4-2015, IEEE Std. 802.15.4™‐2020
IEEE Std. 802.15.4-2015, IEEE Std. 802.15.4™‐2020 frames are structured as shown in Figure 3. Detailed descriptions of the frame
format are given in the standard. The frame consists of a synchronization header (SHR) which includes the preamble symbols and start
frame delimiter (SFD), followed by the PHY header (PHR) and data. The data frame is usually specified in a number of bytes and the
frame format will include 48 Reed-Solomon parity bits following each block of 330 data bits (or less).
While zero length payloads and zero length PHR are supported the maximum frame length is 1023-bytes, including the 2-byte FCS.
Figure 3: IEEE 802.15.4-2015 PPDU Structure
5.5 Frame Formats of IEEE Std. 802.15.4z™‐2020
The 4z amendment added new packet formats to HRP UWB PHY incorporating a Scrambled Timestamp Sequence (STS) into the
packet structure, defining four STS Packet Configurations as shown in Figure 4 below.
• Protocol 0 is a standard IEEE 802.15.4-2015 UWB packet structure. This ensures backward compatibility with DW1000, see
frame format above.
• Protocol 1 includes Scrambled Timestamp Sequence (STS) between the start of frame delimiter (SFD) and the PHY header (PHR).
• Protocol 2 contains the STS after the payload is complete (there is a short configurable gap between the end of the payload and
the start of the STS). The gap can be any integer value from 1 to 127 symbols.
• Protocol 3 is when there is no PHR and no payload.
The STS is a random sequence of positive and negative pulses generated using an AES-128 based deterministic random bit generator
(DRBG). Only valid transmitters and receivers have the correct seed (i.e., the key and IV) to generate the sequence for transmission and
to validly cross correlate in the receiver to determine the receive timestamp. The STS provides for secure receive timestamping and
secure ranging.
Figure 4: IEEE Std. 802.15.4z™‐2020 HRP UWB PHY PPDU Formats
1 6 ,6 4 ,1 0 24 o r 4 0 9
P re a m b le S
S y n c h ro n is a
6 P
e q
tio
re a m b le s
u e n c e
n H e a d e
8 o r 1 6
S ym b o ls
S ta rt F ram e
D e lim ite r
(S F D )
r (S H R )
2 1
S ym b
P H
P H
H e a
(P H
o ls
R
Y
d e r
R )
P H Y P ro to c o l D a
8
ta
*F
M
U
ra
A
n
m e L e n g th +
C P ro to c
P H Y S e rv
it (P P D U )
R e e
o l D
ic e D
d -S o lo m
a ta U
a ta U n
o n E n c
n it ( M
it (P S D
o
P
U
d in
D
)
g
U
b
)
its

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
25 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
5.6 Proprietary Long Frames
The QM33120W offers a proprietary long frame mode where frames of up to 1023-bytes may be transferred. Refer to the QM33120W
user manual for full details.
5.7 No Data Frames
The QM33120W offers zero-length payloads and zero-length PHR. This is for use cases where an alternative method of data
communications is available. Refer to the QM33120W user manual for full details.
5.8 Host Controller Interface
The primary interface QM33120W is via a 4-wire SPI interface. QM33120W will act as a SPI peripheral device, in non-daisy-chain
mode and operate at SPI clock frequencies up to 32 MHz.
5.9 SPI Functional Description
The host interface to QM33120W is a 4-wire SPI-compatible peripheral. The assertion of SPICSn low by the SPI controller (host)
indicates the beginning of a transaction.
The SPI interface is used to read and write registers in the QM33120W device. All data and address transfer on the SPI is most
significant bit first. All address bytes are transmitted with MSB first, and all data is transmitted commencing with the lowest addressed
byte.
• Assertion low of SPICSn initializes transaction.
• De-assertion high of SPICSn ends the SPI transaction.
• The device supports direct and per-byte sub-addressing access to the full register space.
• Efficient block data reading/writing is allowed. Continuous, long transactions can be carried out while the addressed location
is auto incremented on the QM33120W side.
The SPICDI (SPIMISO) I/O is required to go open drain when SPICSn is de-asserted, to allow interoperation with other peripherals on
the SPI bus.
SPI daisy-chaining is not supported. This is the mode where the CDO (SPIMOSI), CDI (SPIMISO) lines are passed through a device
when it is not chip selected.

 QM33120W Datasheet
|     | ®   |     |     | UWB Low-Power Transceiver  |     |     |     |
| --- | --- | --- | --- | -------------------------- | --- | --- | --- |
5.10  SPI Timing Parameters

The SPI peripheral complies with the Motorola SPI protocol within the constraints of the timing parameters listed in the table below and
illustrated in Figure 5 and Figure 6.

S P IC S n
S P IC LK
S P IC D I B it 7 B it 6 B it 5 B it 4 B it 3 B it 2 B it 1 B it 0 B it 7 B it 6 B it 5 B it 4 B it 3 B it 2 B it 1 B it 0 B it 7 B it 6 B it 5
| S P IC D O | 7 6 5 | 4 3 2 1 | 0 7 6 5 | 4 3 2 1 0 |     | 7 6 | 5   |
| ---------- | ----- | ------- | ------- | --------- | --- | --- | --- |
|            | t7    |         | t5      |           | t6  |     |     |
t8
|     |     |     |     |     | t9  |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |

Figure 5: SPI Timing Diagram

| S   | P IC S n  |     |         |         |     |       |     |
| --- | --------- | --- | ------- | ------- | --- | ----- | --- |
| S   | P IC L K  |     |         |         |     |       |     |
| S   | P IM IS O |     | B it  7 | B it  6 | B   | it  5 |     |
t2
| S   | P IM O S I |     | 7   | 6   | 5   |     |     |
| --- | ---------- | --- | --- | --- | --- | --- | --- |
t4
|     |     | t1  | t3  |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |

Figure 6: SPI Detailed Timing Diagram

Table 23: SPI Timing Parameters
|     | Parameter  | Min.  | Max.  | Units  | Description  |     |     |
| --- | ---------- | ----- | ----- | ------ | ------------ | --- | --- |
SPI select asserted low to valid
|     | t1  |     | 10  | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
peripheral output data.
SCLK low to valid peripheral
|     | t2  |     | 9.5  | ns  |     |     |     |
| --- | --- | --- | ---- | --- | --- | --- | --- |
output data.
|     | t3  | 2.5  |     | ns  Controller data setup time.  |     |     |     |
| --- | --- | ---- | --- | -------------------------------- | --- | --- | --- |
|     | t4  | 0.7  |     | ns  Controller data hold time.   |     |     |     |
LSB last byte to MSB next byte.
|     | t5  | 31  |     | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
(see maximum SPI frequency).
SPI select deasserted high to
|     | t6  |     | 10  | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
CDI tristate.
Start time; time from select
|     | t7  | 10  |     | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
asserted to first SCLK.
Idle time between consecutive
|     | t8  | 42  |     | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
accesses.
Last SCLK to SEL_n
|     | t9  | 40  |     | ns  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- |
deasserted.
|     | fSPICLK  |     | 32    | MHz  SPICLK SPI mode 0.  |     |     |     |
| --- | -------- | --- | ----- | ------------------------ | --- | --- | --- |

© 2025 Qorvo US, Inc. | Rev. D
|     |     |     | 26 of 53  |     |     |     | www.qorvo.com   |
| --- | --- | --- | --------- | --- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     | ®   |     |     |     | UWB Low-Power Transceiver  |     |
| --- | --- | --- | --- | --- | -------------------------- | --- |

5.11  SPI Operating Modes

Both clock polarities (SPIPOL=0/1) and phases (SPIPHA=0/1) are supported, as defined in the Motorola SPI protocol. The QM33120W
transfer protocols for each SPIPOL and SPIPHA setting are given in Figure 7 and Figure 8. Mode 0 is the default mode. The mode can
be changed by programming in the OTP memory or writing to the appropriate register, see User Manual.

| C ycle          |           |     |     |     |     | 8*Num ber of  |
| --------------- | --------- | --- | --- | --- | --- | ------------- |
| N um ber, #     | 1         | 2 3 | 4 5 | 6 7 | 8 9 | bytes         |
| SP IP O L=0, SP | IP H A =0 |     |     |     |     |               |
SP IC LK
| SP IP O L=1, SP | IP H A =0 |     |     |     |     |     |
| --------------- | --------- | --- | --- | --- | --- | --- |
SP IC LK
SP IC Sn
SP IC D I
|     | z M SB | 6 5 | 4 3 | 2 1 | LSB M SB | LSB X Z |
| --- | ------ | --- | --- | --- | -------- | ------- |
SP IC D O
|     | z M SB | 6 5 | 4 3 | 2 1 | LSB M SB | LSB X Z |
| --- | ------ | --- | --- | --- | -------- | ------- |

Figure 7: QM33120W SPIPHA=0 Transfer Protocol

| C y c le      | 1   | 2 3 | 4 5 | 6 7 | 8 9 | 8*N u m b er of  |
| ------------- | --- | --- | --- | --- | --- | ---------------- |
| N u m b er, # |     |     |     |     |     | b y te s         |
SPIPOL=0, SPIPHA=1
SPICLK
SPIPOL=1, SPIPHA=1
SPICLK
SPICSn
| SPICDI | z X MSB | 6 5 | 4 3 | 2 1 | LSB MSB | LSB Z |
| ------ | ------- | --- | --- | --- | ------- | ----- |
| SPICDO | z X MSB | 6 5 | 4 3 | 2 1 | LSB MSB | LSB Z |

Figure 8: QM33120W SPIPHA=1 Transfer Protocol

© 2025 Qorvo US, Inc. | Rev. D
27 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
28 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
5.12 SPI Transaction Formatting
The SPI command structure allows for 4 different types of SPI command:
• Fast, single-byte commands. Up to 32 unique commands such as “TX now”, “TX/RX Off”.
• Fast addressed mode. Allowing for read and write addressing to 32 addresses. This command structure is padded by a trailing
bit to allow the SPI address decoder time to fetch any read data. The length of the read is determined by the length of the SPI
transaction.
• Full addressed mode. Allowing for read and write addressing to 32 addresses and up to 128-byte offset addressing. This
command structure is padded by a trailing bit to allow the SPI address decoder time to fetch any read data. The length of the
read or write is determined by the length of the SPI transaction.
• Masked-write transaction. These are intended to simplify read-modify-write operations by allowing the host to write to an
address and apply a set, clear, or toggle mask to 1, 2, or 4 bytes. The SPI command decoder then carries out the required
read-modify-write instructions internally.
Figure 9: SPI Command Formatting

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
29 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
5.13 GPIO and SPI I/O Internal Pull Up / Down
All the GPIO pins have a software controllable internal pull up/down resistor to ensure safe operation when input pins are not driven.
This defaults to enabled and pull-down except for the SPICSn pin which defaults to pull-up. The value of the pull-up/down will vary with
the VDD1 supply voltage over a range from 10 kΩ to 30 kΩ.
Figure 10: SPI and GPIO Pull Up / Down
5.14 Reference Crystal Oscillator
With the addition of an external 38.4 MHz crystal and appropriate loading capacitors, the on-chip crystal oscillator QM33120W generates
the reference frequency for the integrated frequency synthesizer’s PLL.
A trim facility is provided which can be used to trim out crystal initial frequency error. Typically, a trimming range of ± 20 ppm is possible
using a 6-bit trim value. This trimming in 0.125 pF steps provides for up to 8 pF additional capacitance on the XTI and XTO crystal
connections.
5.14.1 Calculation of External Capacitor Values for Frequency Trim
Ideally, the value of external loading capacitors (Cext) should be calculated to give an equal trim range about the center trim value. To
do this, one needs to estimate the parasitic capacitance (Cpar) between the crystal pads XTI/XTO and the crystal pads. A good starting
estimate for Cext is approximately 3.6 pF. However, some trial and error may be required initially. The values of Cm, Lm, Rm, and Co
obtained from the crystal manufacturer are also required.
Q M 3 3 1 2 0 W
A O N IO
C o
Hn ot
r
so t
l le r
S
P
P I
O
R
T
3 0 k O
3 0 k O
3 0 k O
3 0 k O
h m
h m
h m
h m
V D D 1
(1 .8 V )
(1 .8 V )
(1 .8 V )
(1 .8 V )
D IG IO
S P IC S n
S P IC D O
S P IC L K
S P IC D I
IR Q
G P IO (x )

 QM33120W Datasheet
® UWB Low-Power Transceiver

Figure 11: Crystal Model
Using the following formula, the required Cext and trim range can be estimated where:
𝑓 = series frequency
𝑠
𝑓 = parallel frequency  𝑝
𝑓 = loaded (desired) frequency
𝐿
1
|     | 𝑓 = |     |
| --- | --- | --- |
𝑠
|     | 2𝜋√𝐶 | 𝐿   |
| --- | ---- | --- |
|     | 𝑀    | 𝑀   |

𝐶
|     | 𝑓 =𝑓 (√1+ | 𝑀 )  |
| --- | --------- | ---- |
|     | 𝑝 𝑠       |      |
𝐶
0

𝐶
|     | 𝑓 =𝑓 (√1+ | 𝑀 )  |
| --- | --------- | ---- |
|     | 𝐿 𝑠       |      |
𝐶
𝐿

1
| 𝐶 =𝐶 | + (𝐶 +𝐶 | +𝐶 )    |
| ---- | ------- | ------- |
| 𝐿 0  | 2 𝑇𝑅𝐼𝑀  | 𝑃𝐴𝑅 𝐸𝑋𝑇 |

|     | 𝑓     | −𝑓    |
| --- | ----- | ----- |
|     | 𝐿     | 𝐿𝑛𝑜𝑚  |
| ∆𝑓  | =106× |       |
| 𝑝𝑝𝑚 |       | 𝑓     |
𝐿𝑛𝑜𝑚

A typical crystal trimming plot is shown below:

Figure 12: Crystal Trim Plot

© 2025 Qorvo US, Inc. | Rev. D
30 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
31 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
6 Operational States
6.1 Overview
The QM33120W has several basic operating states as described in table below.
Table 24: Operating states
State Description
OFF The digital core is powered off, digital LDO is disabled. Reset is held low.
The system is clocked from 30 MHz RC Osc, SPI comms @ 7 MHz. AON download is performed.
INIT_RC
Automatically goes to IDLE_RC on completion.
IDLE_RC The system is clocked at ~120 MHz to allow full-speed SPI comms.
IDLE_PLL The system is clocked from the PLL at 124.8 MHz.
TX_WAIT TX blocks are sequenced on as required. Includes DELAYED_TX mode.
TX Active TX state. Automatically reverts to IDLE_PLL after transmission.
RX_WAIT RX blocks are sequenced on as required. Includes DELAYED RX mode.
RX Active RX state. Can revert to IDLE_PLL if the packet received or timeout triggers.
SLEEP Low power state. The sleep counter is clocked from slow RC Osc at ~20 kHz.
Low power state. All clocks off. Wakeup via IO event on WAKEUP or SPICSn, or by resetting the
DEEPSLEEP
device (RSTn).

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
32 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
6.2 Operating State Transitions
Figure 13: Operating State Transitions

 QM33120W Datasheet
|     |     | ®   |     |     | UWB Low-Power Transceiver  |     |     |
| --- | --- | --- | --- | --- | -------------------------- | --- | --- |
7  Powering QM33120W

QM33120W is designed such that it can be powered in several different configurations depending on the application. These options are
described below. Figure 14: Timing Diagram for Cold Start POR shows the power-up sequence when external power sources are applied.

When the external power source is applied to the QM33120W for the first time (cold power-up), the internal Power-On Reset (POR) circuit
compares the externally applied supply voltage (VDD1) to an internal power-on threshold (approximately 1.5 V), and once this threshold
is passed the AON block is released from reset and the external device enable pin EXTON is asserted.

Then the VDD2a/b and VDD3 supplies are monitored and once they are above the required voltage as specified in the Datasheet (2.2 V
and 1.4 V respectively), the fast RC oscillator (FAST_RC) and crystal (XTAL Oscillator) will come on within 500 µs and 1 ms, respectively.

Once the digital reset is de-asserted the digital core wakes up and enters the INIT_RC state, (see Figure 14 and Figure 15). Then, once
the configurations stored in AON and OTP have been restored (into the configuration registers), the device will enter IDLE_RC. Then the
host can set the AINIT2IDLE configuration bit in SEQ_CTRL and the IC will enable the PLL and wait for it to lock before entering the
IDLE_PLL state.

|     |     | V D D 1 |     |     |     |     |     |
| --- | --- | ------- | --- | --- | --- | --- | --- |
_ 0 V
R
| O   | P O | R n /R S T n   |     |     |     |     |       |
| --- | --- | -------------- | --- | --- | --- | --- | ----- |
| P   |     |                |     |     |     |     | _ 0 V |
|     |     | E X T O N      |     |     |     |     |       |
_ 0 V
s ro
|  y    | V D D            | 2 a /V D D 3   |                   |                       |                         |     |       |
| ----- | ---------------- | -------------- | ----------------- | --------------------- | ----------------------- | --- | ----- |
| lp ta |                  |                |                   |                       |                         |     | _ 0 V |
| pa r  |                  | < 1 0  m       | s                 |                       |                         |     |       |
| up    | S ig n a l V D D | 2 a /V D D 3   |                   |                       |                         |     |       |
| Sm    |                  | O K            | 2 0  L P  O s c c | y c le s fro m  V D D | x  O K  to  W A K E U P |     |       |
o
C
|     | L P  O   | s cilla to r |     |     |     |     |     |
| --- | -------- | ------------ | --- | --- | --- | --- | --- |
| s   | F a st O | sc illa to r |     |     |     |     |     |
ro
ta
llic
|     | X T A L  O | scilla to r |     |      |      |     |     |
| --- | ---------- | ----------- | --- | ---- | ---- | --- | --- |
| s   |            |             |     |  < 1 |  m s |     |     |
O
|     |     | D IG  S ta te |     | O F F | W A KEU P IN IT_R | C ID LE_R C | ID LE_P LL |
| --- | --- | ------------- | --- | ----- | ----------------- | ----------- | ---------- |
e ro
| C   |      |           |           |                      | ~ 7 0  µ   | s                         |                        |
| --- | ---- | --------- | --------- | -------------------- | ---------- | ------------------------- | ---------------------- |
|  la |      |           | S e t S P | I m o d e  in  O T P |            |                           |                        |
| tig |      |           |           |                      | O T P  b o | o t                       |                        |
|     |  H O | S T  IR Q |           |                      |            | Ha o s t in te rru        | p t (S P I c o m m s   |
| iD  |      |           |           |                      |            | llo w e d  @  3           | 2  M H z)              |
|     |      |           |           |                      |            | A C T IV E  S ta te       | s                      |
|     |      |           | A O N     |  P o w e r u p       |            |                           |                        |
|     |      |           |           |                      | (IN IT _ R | C , ID L E _ R C , ID L E | _ P L L , T X , R X )  |

Figure 14: Timing Diagram for Cold Start POR

© 2025 Qorvo US, Inc. | Rev. D
|     |     |     | 33 of 53  |     |     |     | www.qorvo.com   |
| --- | --- | --- | --------- | --- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
|     | ® UWB Low-Power Transceiver  |     |     |     |
| --- | ---------------------------- | --- | --- | --- |
 > =  5 0 0 µ s
|  fi dohtem C S (if u | se d  to  w ak eu p ) |     |     |       |
| -------------------- | --------------------- | --- | --- | ----- |
|  puekaW              |                       |     |     | _ 0 V |
desu
| W ake U p  (if u | se d  to  w ak e u p ) |     |     | _ 0 V |
| ---------------- | ---------------------- | --- | --- | ----- |
| In tern a l Slee | p  T im e r Eve n t    |     |     |       |
| (w h en          |  Sle e p  is u se d )  |     |     | _ 0 V |
V D D 1
_ 0 V
| ROP | P O R n /R ST n   |     |     |     |
| --- | ----------------- | --- | --- | --- |
_ 0 V
E XT O N
_ 0 V
srotarapmoC
| V          | D D 2 a /V D D 3   |     |     |       |
| ---------- | ------------------ | --- | --- | ----- |
|  ylppuS    |                    |     |     | _ 0 V |
| Sign a l V | D D 2 a /V D D 3   |     |     |       |
O K
_ 0 V
2 1  LP O sc cycles fro m  EX T O N  to  w a keu p
|     | LP O scilla to r |     |     |     |
| --- | ---------------- | --- | --- | --- |
srotallicsO
| Fa   | st O scilla to r |     |     |     |
| ---- | ---------------- | --- | --- | --- |
| X TA | L O scilla to r  |     |     |     |
eroC latigiD D IG  State D E EP SLEE P /SLEE P W A KEU P IN IT_RC IDLE_RC IDLE_PLL
~ 8 5  µ s A O N
Se t SP I m o d e  in  O TP
+  O TP  b o o t
|     |  H O ST  IR Q |     | H o st in te rru p t (SP | I co m m s  _ 0 V |
| --- | ------------- | --- | ------------------------ | ----------------- |
allo w e d  @  3 2  M H z)
 ~ 1  m s
|     |     |     | A C T IV E  S ta te s  |     |
| --- | --- | --- | ---------------------- | --- |
A O N  w a k e  u p
|     |     | (IN IT _ R C , ID | LE _ R C , ID LE _ P LL, T | X , R X )   |
| --- | --- | ----------------- | -------------------------- | ----------- |
Figure 15: Timing Diagram for Warm Start

© 2025 Qorvo US, Inc. | Rev. D
34 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
35 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.1 Lowest Bill of Materials (BOM) powering scheme
In the following configuration, the QM33120W is powered directly from a coin-cell battery. This is for applications that require minimal
BOM. The bulk capacitor is required to store energy. The value of the capacitor depends on the time the transceiver is in the active
Tx/Rx state. Demonstration circuits typically use 47 uF.
Figure 16: Lowest BOM powering option
7.2 Highest Efficiency powering scheme
VDD1
V T X _ D
2.4V-3.6V Q M 33120W
VDD2
Battery
1.6V
Buck VDD3
1.6V
EXTON
Figure 17: Single O/P Buck SMPS option

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
36 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.3 PMIC Powering Scheme
In the following configuration, the external PMIC circuit is used to provide all the power rails to the chip. The VDD1 is used to power
Always-On memory and I/O rail only; the current consumption for powering AON is negligible.
Figure 18: Mobile Option
P M I C
1
2
1
.6 .5
.6 V
V
V
- 3 .6 V
V D D 1
V T X _ D
Q M 3 3 1 2 0 W
V D D 2
V D D 3

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
37 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.4 Typical Transmit and Receive power profiles
The current drawn during operation with QM33120W will vary depending on supplies used, batteries used, use case, etc. Figure 19
shows the current drawn from a CR2032 battery with typical TX frames transmitted and the reception of these frames by the receiver.
7.4.1 TX current profile for the minimal BOM
Figure 19 below shows the current profiles during frame transmission without secure preamble and 6.8 Mbps TDoA tag frame. This
mode is comparable to a DW1000 TDoA tag blink. All supplies are connected to the battery assumed to be at 3.0 V, i.e., the lowest
BOM option, see Lowest Bill of Materials (BOM) Powering Scheme.
Figure 19: Current Profile when Transmitting a Frame (6.8 Mbps) in Lowest BOM Use Case

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
38 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.4.2 TX current profile for high efficiency modes
In the high-efficiency modes, i.e. when an external DC-DC/PMIC is used, the current consumption from VDD3 (1.6V) and VDD2a and
VDD2b (2.5V) is different, therefore more efficient current consumption can be achieved using alternative powering schemes, illustrated
in section 7.2 and 7.3. The VDD1 is used to power AON memory and I/O rail only; the current consumption for powering AON is
negligible.
For high-efficiency schemes, the overall power consumption depends on the efficiency of external DC-DC and/or PMIC. For the
QM33120W device, the power consumption during different phases of operation is illustrated in Figure 20.
TX Current Profile with VDD3=1.6 V & VDD2a/b=2.5 V
45
40
35
30
25
A
m VDD3
20
VDD2a/b
15
10
5
0
Osc Start up WR TX Data PLL start up TX SHR TX Deepsleep
pHR/PSDU
Figure 20: Current Consumption During TX for High Efficiency Powering Modes

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
39 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.4.3 RX current profile for minimal BOM
Figure 21 below illustrates the current profiles during the reception of a typical frame. All supplies are connected to the battery assumed
to be at 3.0 V, i.e., the lowest BOM option, see Figure 16.
The variable part of Preamble Hunt is 30us for Responder and 0 (zero) when using Delayed RX in Two Way Ranging (TWR) protocol.
Figure 21: Typical current profiles when receiving a frame for different types of applications

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
40 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.4.4 RX Current Profile for High Efficiency BOMs
In the high-efficiency modes, i.e., when an external DC-DC/PMIC is used, the current consumption from VDD2 (2.5V) and VDD3 (1.6V)
are different, therefore more efficient current consumption can be achieved using alternative powering schemes, illustrated in section
7.2 and 7.3. The VDD1 is used to power AON memory and IO only, the current consumption for powering AON is negligible.
For high-efficiency schemes, the overall power consumption depends on the efficiency of external the DC-DC and/or PMIC. For the
QM33120W device, the power consumption during different phases of operation is illustrated below.
RX Current Profile with VDD3 = 1.6V & VDD2a/b = 2.5V
80
70
60
50
A 40
m VDD3
30 VDD2a/b
20
10
0
Osc Start Cal config PLL start Preamble RX SHR RX Host RD Deepsleep
up up hunt PHR/PSDU DATA
Figure 22: Current Consumption During RX for High Efficiency Powering Modes

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
41 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
7.5 Internal Power Supply Distribution
The block diagram shows the power distribution within the QM33120W device.
Figure 23: Internal Power Distribution

 QM33120W Datasheet
|     |     |     |     |     | ®   |     |     |     |     | UWB Low-Power Transceiver  |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | -------------------------- | --- | --- | --- |
8  Application Information

8.1  Application Circuit Diagram

|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
| --- | ----- | ------- | ------------- | ------ | ------------------------- | --- | ------------- | --------------- | --- | ------- | --- | --- | --- |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |
|     |       |         |               |        |                           |     |               |                 |     |         |     |     |     |

|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
| --- | --- | ------- | --- | ------- | ------------------- | --------------- | ------------ | ------- | --- | --- | ---------- | ----------------- | --- |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |
|     |     |         |     |         |                     |                 |              |         |     |     |            |                   |     |

|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
| --- | ------ | -------- | ---- | -------- | --------- | ---------- | -------- | --------------- | --------- | -------- | ----- | --- | --- |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |
|     |        |          |      |          |           |            |          |                 |           |          |       |     |     |

|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
| --- | ------- | --- | --- | --- | ----------- | ------ | ---------- | ------- | --- | -------------- | ------- | ---------- | ----- |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |
|     |         |     |     |     |             |        |            |         |     |                |         |            |       |

|     |     |     |     |     |            |       |      |        |            |         |     |     |     |
| --- | --- | --- | --- | --- | ---------- | ----- | ---- | ------ | ---------- | ------- | --- | --- | --- |
|     |     |     |     |     |            |       |      |        |            |         |     |     |     |
|     |     |     |     |     |            |       |      |        |            |         |     |     |     |

|     |     |     |     |     |     |     |     |     |     |     |     |     |     |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
Figure 24: QM33120W Application Circuit

Note: When a PDoA variant (QM33120W) is used as a non-PDoA variant (QM33110W), RF2 should have a 2pF series capacitor and 50 Ohm resistor to ground.
8.2  Recommended Components

The list of components tested and approved by Qorvo is shown in the table below. The use of DC-DC regulators and TCXO’s is optional.

Table 25: QM33120W Recommended Components
| Function  |     |     |     | Manufacturer  |     |     |     | Part Number    |     |     |     | Web Link            |     |
| --------- | --- | --- | --- | ------------- | --- | --- | --- | -------------- | --- | --- | --- | ------------------- | --- |
|           |     |     |     | Partron       |     |     |     | ACS5200HFAUWB  |     |     |     | www.partron.co.kr   |     |
| Antenna   |     |     |     |               |     |     |     |                |     |     |     |                     |     |
|           |     |     |     | Taiyo Yuden   |     |     |     | AH086M555003   |     |     |     | www.yuden.co.jp     |     |

|                              |     |     |     | TXC    |     |     |     | 8Y38472012  |     |     |     | www.txccorp.com |     |
| ---------------------------- | --- | --- | --- | ------ | --- | --- | --- | ----------- | --- | --- | --- | --------------- | --- |
| Crystal (38.4 MHz+/-10 ppm)  |     |     |     |        |     |     |     |             |     |     |     |                 |     |
|                              |     |     |     | Rakon  |     |     |     | RSX-10      |     |     |     | www.rakon.com   |     |

| DC-DC  |     |     |     | TI  |     |     |     | TPS62743  |     |     |     | www.ti.com |     |
| ------ | --- | --- | --- | --- | --- | --- | --- | --------- | --- | --- | --- | ---------- | --- |

|     |     |     |     | TXC  |     |     |     | 7Z38470005  |     |     |     | www.txccorp.com |     |
| --- | --- | --- | --- | ---- | --- | --- | --- | ----------- | --- | --- | --- | --------------- | --- |

  TCXO
|     |     |     |     | Rakon  |     |     |     | IT2200K  |     |     |     | www.rakon.com |     |
| --- | --- | --- | --- | ------ | --- | --- | --- | -------- | --- | --- | --- | ------------- | --- |

© 2025 Qorvo US, Inc. | Rev. D
|     |     |     |     |     |     |     |     | 42 of 53  |     |     |     |     | www.qorvo.com   |
| --- | --- | --- | --- | --- | --- | --- | --- | --------- | --- | --- | --- | --- | --------------- |
Subject to change without notice | All rights reserved

 QM33120W Datasheet
® UWB Low-Power Transceiver
8.3  Recommended PCB Layout and Stackup

Some recommendations for the chip layout are as follows:
•  Keep all the traces as short as possible.
•  Avoid mixing Analog (RF1, RF2, XIN, XOUT), Power (VDD1, VDD2a/b, VDD3, VDD decoupling), and Digital (SPI, etc) groups together.
•  Place all the decoupling capacitors as close to the corresponding chip pads as possible, the smaller capacitor should be closer to the pad.
Connect the ground pad of each capacitor to the good ground plane directly to minimize ESR and ESL of the return current path.
•  RF1 and RF2 lines should be 50 Ohm impedance-controlled lines. Embed the DC-blocking 2pF capacitor pads into the track or add
teardrops to remove any possible discontinuities.
•  The ground copper should be removed from under the chip in the areas shown in the picture (Top layer and Inner Layer 1). The first solid
ground copper should be on the Inner Layer 2. If a different stack-up will be used – this first solid copper layer should be at least 0.25mm
away from the Top Layer, any inner layers close to the Top Layer should have the ground removed in the same manner as in Inner Layer
1 above.
Through Via
50 Ohm lines Keep clear of copper,
tracks and vias Pad
MicroVia on pad
Cutout Inner Layer 1
Cutout Top Layer
Stack-up
|                  | 35um  | Top Layer     |
| ---------------- | ----- | ------------- |
|                  | 100um | Prepreg       |
|                  | 35um  | Inner Layer 1 |
|                  | 356um | Core          |
|                  | 35um  | Inner Layer 2 |
| 2pF DC-Blocking  | 100um | Prepreg       |
capacitors C2, C3
Inner Layer 3
35um
|     | 356um | Core          |
| --- | ----- | ------------- |
|     | 35um  | Inner Layer 4 |
|     | 100um | Prepreg       |
Bottom Layer
35um

Figure 25: Recommended PCB Layout and Stackup

© 2025 Qorvo US, Inc. | Rev. D
43 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
44 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
9 Packaging & Ordering Information
9.1 52-Ball WLCSP
9.1.1 Package Dimensions
Figure 26: QM33120W Package Dimensions

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
45 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
9.1.2 Recommended PCB Footprint
Figure 27: Recommended PCB Footprint

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
46 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
9.1.3 Tape and Reel Packaging Information
Figure 28: Tape orientation and dimensions
Figure 29: Reel Information – 330 mm Reel (13”)

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
47 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
9.1.4 Device Package Marking
The diagram below shows the package markings for QM33120W.
Figure 30: Device Package Marking

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
48 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
10 Glossary
Table 26: Glossary of Terms
Abbreviation Full Title Explanation
BPRF Base PRF mode 64 MHz PRF Mode
Controller Data
CDI SPI interface I/O
input
Controller Data
CDO SPI interface I/O
output
Equivalent The amount of power that a theoretical isotropic antenna (which evenly distributes power in
EIRP Isotropically all directions) would emit to produce the peak power density observed in the direction of
Radiated Power maximum gain of the antenna being used.
General Purpose Pin of an IC that can be configured as an input or output under software control and has no
GPIO
Input / Output specifically identified function.
Institute of
Is the world’s largest technical professional society. It is designed to serve professionals
Electrical and
IEEE involved in all aspects of the electrical, electronic, and computing fields and related areas of
Electronics
science and technology.
Engineers
Physical radio channel configuration in which there is a direct line of sight between the
LOS Line of Sight
transmitter and the receiver.
Non-Line of Physical radio channel configuration in which there is no direct line of sight between the
NLOS
Sight transmitter and the receiver.
A technique allowing a signal to be driven by more than one device. Generally, each device is
permitted to pull the signal to the ground but when not doing so it must allow the signal to
Open Drain Open Drain
float. Devices should not drive the signal high to prevent contention with devices attempting
to pull it low.
Phase Method of determining the direction of propagation of a radio-frequency wave incident on an
PDoA Difference of antenna array using the phase difference between the signal received on each antenna array
Arrival element.
Phase Locked Circuit designed to generate a signal at a particular frequency whose phase is related to an
PLL
Loop incoming “reference” signal.
Used to quantify very small relative proportions. Just as 1% is one out of a hundred, 1 ppm is
PPM Parts Per Million
one part in a million.
Generally used to refer to signals in the range of 3 kHz to 300 GHz. In the context of a radio
Radio
RF receiver, the term is generally used to refer to circuits in a receiver before down-conversion
Frequency
takes place and in a transmitter after up-conversion takes place.
Real Time
RTLS System intended to provide information on the location of various items in real-time.
Location System
Start of Frame
SFD Defined in the context of the IEEE 802.15.4 standard.
Delimiter
Serial Peripheral An industry standard method for interfacing between IC’s using a synchronous serial scheme
SPI
Interface first introduced by Motorola.
Method of deriving information on the location of a transmitter. The time of arrival of a
transmission at two physically different locations whose clocks are synchronized is noted and
Time Difference
TDoA the difference in the arrival times provides information on the location of the transmitter. A
of Arrival
number of such TDoA measurements at different locations can be used to uniquely determine
the position of the transmitter. Refer to Qorvo’s website for further information.
Method of measuring the physical distance between two radio units by exchanging messages
Two Way
TWR between the units and noting the times of transmission and reception. Refer to Qorvo’s
Ranging
website for further information.

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
49 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
11 References
[1] IEEE Std. 802.15.4-2011 or “IEEE Std. 802.15.4™‐2011” (Revision of IEEE Std. 802.15.4-2006). IEEE Standard for Local and
metropolitan area networks – Part 15.4: Low-Rate Wireless Personal Area Networks (LR-WPANs). IEEE Computer Society
Sponsored by the LAN/MAN Standards Committee. Available from http://standards.ieee.org/
[2] IEEE Std. 802.15.4-2015 or “IEEE Std. 802.15.4™‐2015” (Revision of IEEE Std. 802.15.4-2011). IEEE Standard for Local and
metropolitan area networks – Part 15.4: Low-Rate Wireless Personal Area Networks (LR-WPANs). IEEE Computer Society
Sponsored by the LAN/MAN Standards Committee. Available from http://standards.ieee.org/
[3] IEEE Std. 802.15.4™‐2020 (Revision of IEEE Std. 802.15.4‐2015) “IEEE Standard for Low‐Rate Wireless Networks”. IEEE
Computer Society Sponsored by the LAN/MAN Standards Committee. Available from http://standards.ieee.org/
[4] IEEE Std. 802.15.4z™‐2020 (Amendment to IEEE Std. 802.15.4™‐2020) “Amendment 1: Enhanced Ultra-Wideband (UWB)
Physical Layers (PHYs) and Associated Ranging Techniques”. IEEE Computer Society Sponsored by the LAN/MAN Standards
Committee. Available from http://standards.ieee.org/

 QM33120W Datasheet
|     |     | ®   |     | UWB Low-Power Transceiver  |
| --- | --- | --- | --- | -------------------------- |
12  Revision History

Table 27: Document History
| Revision  | Date                             |     |     | Description  |
| --------- | -------------------------------- | --- | --- | ------------ |
| A         | November 2021  Initial release.  |     |     |              |
Datasheet updated to reflect latest characterization results and editorial changes.
- Receiver sensitivity and Link budget values were added for the data rate 850 Kbps.
- Table 10, Crystal Load capacitance value is updated.
- PDoA switch losses are updated.
| B   | April 2022  |     |     |     |
| --- | ----------- | --- | --- | --- |
- Table 14, suggestions are added to satisfy the best PDoA performance.
- Figure 24, update application circuit diagram.
|     | - Table 23, update SPI timings t |     | and t.  |     |
| --- | -------------------------------- | --- | ------- | --- |
1  5
- Figure 27, update recommended PCB layout and Stackup.
C  June 2022  Figure 6, Correction to SPI diagram to illustrate timings more accurately.
Fixed SPI transactions formatting diagram.
Updated RSTn pin description
| D   | October 2025  |     |     |     |
| --- | ------------- | --- | --- | --- |
Added IDLE_PLL current consumption values for Ch5 & Ch9
Corrected pin number for VDD1 on figure 23

13  Handling Precautions

| PARAMETER  |     | RATING  | STANDARD  |     |
| ---------- | --- | ------- | --------- | --- |
ESD – Human Body Model (HBM)  Class 2 (2000 V)  ESDA/JEDEC JS-001  Caution!

| ESD – Charged Device Model (CDM)  |     | Class C2a (500V)  | ESDA/JEDEC JS-002  |     |
| --------------------------------- | --- | ----------------- | ------------------ | --- |
  ESD sensitive device
| MSL – Moisture Sensitivity Level  |     | MSL 1  | IPC/JEDEC J-STD- |     |
| --------------------------------- | --- | ------ | ---------------- | --- |
020

14  Solderability

The QM33120W should be soldered using the reflow profile specified below.

Figure 31: Reflow Profile of the WLCSP package
© 2025 Qorvo US, Inc. | Rev. D
50 of 53  www.qorvo.com
Subject to change without notice | All rights reserved

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
51 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Table 28: Critical Parameters for Lead Free Solder of the WLCSP package
Process Step Lead Free Solder
Ramp Rate 3˚C/sec
Pre-Heat 150˚C to 180˚C, 60 to 180 seconds
Time above Liquidius, 220˚C 30 to 90 seconds
Peak Temperature 255˚C +/- 5˚C
Time within 5˚C of Peak Temperature 10 to 20 seconds
Ramp Down Rate 6˚C/sec Max, 0.8˚C/sec to 0.4˚C recommended
15 Further Information
Qorvo develops semiconductors solutions, software, modules, reference designs - that enable real-time, ultra-accurate, ultra-reliable
local area micro-location services. Qorvo’s technology enables an entirely new class of easy to implement, highly secure, intelligent
location functionality and services for IoT and smart consumer products and applications.
For further information on this or any other Qorvo product, please refer to app note on Qorvo website.
RoHS Compliance
This part is compliant with 2011/65/EU RoHS directive (Restrictions on the Use of Certain Hazardous Substances in Electrical and
Electronic Equipment) as amended by Directive 2015/863/EU.
This product also has the following attributes:
• Lead Free
• Antimony Free
• TBBP-A (C15H12Br402) Free
• SVHC Free
Regulatory Approvals
The QM33120W, as supplied from Qorvo, has not been certified for use in any particular geographic region by the appropriate
regulatory body governing radio emissions in that region although it is capable of such certification depending on the region and the
manner in which it is used.
All products developed by the user incorporating the QM33120W must be approved by the relevant authority governing radio emissions
in any given jurisdiction prior to the marketing or sale of such products in that jurisdiction and user bears all responsibility for obtaining
such approval as needed from the appropriate authorities.
P b

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
52 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Product Status
Marking Product Status Definition
Datasheet contains design specifications for product development. Specifications
ADVANCE INFO Formative / In Design
may change in any manner without notice.
Datasheet contains preliminary data; supplementary data will be published later.
PRELIMINARY First Production Qorvo reserves the right to make changes at any time without notice to improve the
design.
Datasheet contains final specifications. Qorvo reserves the right to make changes
(none) Full Production
► at any time without notice to improve the design.
Datasheet contains specifications on a product that is discontinued. The datasheet
OBSOLETE Not in Production
is for reference information only.

QM33120W Datasheet
UWB Low-Power Transceiver
© 2025 Qorvo US, Inc. | Rev. D
53 of 53 www.qorvo.com
Subject to change without notice | All rights reserved
®
Contact Information
For the latest specifications, additional product information, worldwide sales and distribution locations:
Web: www.qorvo.com
Tel: 1-844-890-8163
Email: customer.support@qorvo.com
Important Notice
The information contained in this Data Sheet and any associated documents (“Data Sheet Information”) is believed to be reliable; however, Qorvo makes no warranties
regarding the Data Sheet Information and assumes no responsibility or liability whatsoever for the use of said information. All Data Sheet Information is subject to change
without notice. Customers should obtain and verify the latest relevant Data Sheet Information before placing orders for Qorvo® products. Data Sheet Information or the use
thereof does not grant, explicitly, implicitly or otherwise any rights or licenses to any third party with respect to patents or any other intellectual property whether with regard to
such Data Sheet Information itself or anything described by such information.
DATA SHEET INFORMATION DOES NOT CONSTITUTE A WARRANTY WITH RESPECT TO THE PRODUCTS DESCRIBED HEREIN, AND QORVO HEREBY
DISCLAIMS ANY AND ALL WARRANTIES WITH RESPECT TO SUCH PRODUCTS WHETHER EXPRESS OR IMPLIED BY LAW, COURSE OF DEALING, COURSE OF
PERFORMANCE, USAGE OF TRADE OR OTHERWISE, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE. Without limiting the generality of the foregoing, Qorvo® products are not warranted or authorized for use as critical components in medical, life-saving, or life-
sustaining applications, or other applications where a failure would reasonably be expected to cause severe personal injury or death. Applications described in the Data Sheet
Information are for illustrative purposes only. Customers are responsible for validating that a particular product described in the Data Sheet Information is suitable for use in a
particular application.
FiRa, FiRa Consortium, the FiRa logo, the FiRa Certified logo, and FiRa tagline are trademarks or registered trademarks of FiRa Consortium or its licensor(s)/ supplier(s) in the
US and other countries and may not be used without permission. All other trademarks, service marks, and product or service names are trademarks or registered trademarks
of their respective owners.
© 2021 Qorvo US, Inc. All rights reserved. This document is subject to copyright laws in various jurisdictions worldwide and may not be reproduced or distributed, in whole or
in part, without the express written consent of Qorvo US, Inc. | QORVO® is a registered trademark of Qorvo US, Inc.
