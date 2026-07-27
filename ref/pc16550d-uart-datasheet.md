



<!-- Start of picture text -->
June 1995<br><!-- End of picture text -->

# PC16550D Universal Asynchronous Receiver�Transmitter with FIFOs� General Description Features 

## Features 

- Y Capable of running all existing 16450 software� 

The PC16550D is an improved version of the original 16450 Universal Asynchronous Receiver�Transmitter (UART)� Functionally identical to the 16450 on powerup (CHARACTER mode)� the PC16550D can be put into an alternate mode (FIFO mode) to relieve the CPU of excessive software overhead� 

- Y Pin for pin compatible with the existing 16450 except for CSOUT <u>(24)</u> and NC <u>(29)�The</u> former CSOUT and NC pins are TXRDY and RXRDY�respectively� 

- Y After reset�all registers are identical to the 16450 register set� 

In this mode internal FIFOs are activated allowing 16 bytes (plus 3 bits of error data per byte in the RCVR FIFO) to be stored in both receive and transmit modes�All the logic is on chip to minimize system overhead and maximize system efficiency�Two pin functions have been changed to allow signalling of DMA transfers� 

- Y In the FIFO mode transmitter and receiver are each buffered with 16 byte FIFO’s to reduce the number of interrrupts presented to the CPU� 

- Y Adds or deletes standard asynchronous communication bits (start�stop�and parity) to or from the serial data� 

- Y Holding and shift registers in the 16450 Mode eliminate the need for precise synchronization between the CPU and serial data� 

The UART performs serial-to-parallel conversion on data characters received from a peripheral device or a MODEM� and parallel-to-serial conversion on data characters received from the CPU�The CPU can read the complete status of the UART at any time during the functional operation�Status information reported includes the type and condition of the transfer operations being performed by the UART�as well as any error conditions (parity�overrun�framing�or break interrupt)� 

- Y Independently controlled transmit�receive�line status� and data set interrupts� 

- Y Programmable baud generator divides any input clock by 1 to (2<sup>16b</sup> 1) and generates the 16<sup>c</sup> clock� 

- Y Independent receiver clock input� Y MODEM control functions (CTS�RTS�DSR�DTR�RI� and DCD)� 

- Y Fully programmable serial-interface characteristics� �5-�6-�7-�or 8-bit characters �Even�odd�or no-parity bit generation and detection �1-�1���-�or 2-stop bit generation �Baud generation (DC to 1�5M baud)� 

- Y False start bit detection� Y Complete status reporting capabilities� Y TRI-STATE� TTL drive for the data and control buses� Y Line break generation and detection� Y Internal diagnostic capabilities� �Loopback controls for communications link fault isolation 

- �Break�parity�overrun�framing error simulation� 

- Y Full prioritized interrupt system controls� 

The UART includes a programmable baud rate generator that is capable of dividing the timing reference clock input by divisors of 1 to (2<sup>16b</sup> 1)�and producing a 16<sup>c</sup> clock for driving the internal transmitter logic�Provisions are also included to use this 16<sup>c</sup> clock to drive the receiver logic�The UART has complete MODEM-control capability�and a processor-interrupt system�Interrupts can be programmed to the user’s requirements�minimizing the computing required to handle the communications link� 

The UART is fabricated using National Semiconductor’s advanced M<sup>2</sup> CMOS process� 

�Can also be reset to 16450 Mode under software control� 

> �Note�This part is patented� 

## Basic Configuration 





<!-- Start of picture text -->
TL�C�8652–1<br><!-- End of picture text -->



<!-- Start of picture text -->
TRI-STATE� is a registered trademark of National Semiconductor Corp�<br><!-- End of picture text -->

C1995 National Semiconductor Corporation TL�C�8652 



<!-- Start of picture text -->
RRD-B30M75�Printed in U�S�A�<br><!-- End of picture text -->

## Table of Contents 

- 1�0 ABSOLUTE MAXIMUM RATINGS 

- 2�0 DC ELECTRICAL CHARACTERISTICS 

- 3�0 AC ELECTRICAL CHARACTERISTICS 

- 4�0 TIMING WAVEFORMS 

- 5�0 BLOCK DIAGRAM 

- 6�0 PIN DESCRIPTIONS 

- 7�0 CONNECTION DIAGRAMS 

### 8�0 REGISTERS (Continued) 

   - 8�3 Programmable Baud Generator 8�4 Line Status Register 

   - 8�5 FIFO Control Register 

   - 8�6 Interrupt Identification Register 

   - 8�7 Interrupt Enable Register 

   - 8�8 Modem Control Register 

   - 8�9 Modem Status Register 

   - 8�10 Scratchpad Register 

   - 8�11 FIFO Interrupt Mode Operation 

- 8�0 REGISTERS 

- 8�1 Line Control Register 

- 8�12 FIFO Polled Mode Operation 

9�0 TYPICAL APPLICATIONS 

- 8�2 Typical Clock Circuits 

2 

## 1�0 Absolute Maximum Ratings 

Temperature Under Bias 0�C to<sup>a</sup> 70�C Note�Maximum ratings indicate limits beyond which permaStorage Temperature b65�C to a150�C nent damage may occur�Continuous operation at these limits is not intended and should be limited to those conditions All Input or Output Voltageswith Respect to VSS b0�5V to a7�0V specified under DC electrical characteristics� Power Dissipation 1W 

## 2�0 DC Electrical Characteristics 

TA<sup>e</sup> 0�C to<sup>a</sup> 70�C�VDD<sup>ea</sup> 5V g10%�VSS<sup>e</sup> 0V�unless otherwise specified� 

|Symbol|Parameter|Conditions|Min|Max|Units|
|---|---|---|---|---|---|
|VILX|Clock Input Low Voltage||b0�5|0�8|V|
|VIHX|Clock Input High Voltage||2�0|VDD|V|
|VIL|Input Low Voltage||b0�5|0�8|V|
|VIH|Input High Voltage||2�0|VDD|V|
|VOL|Output Low Voltage|IOL <sup>e </sup>1�6 mA on all (Note 1)||0�4|V|
|VOH|Output High Voltage|IOH <sup>e b</sup>1�0 mA (Note 1)|2�4||V|
|ICC(AV)|Average Power Supply<br>Current|VDD <sup>e </sup>5�5V�TA <sup>e </sup>25�C<br>No Loads on output<br>SIN�DSR�DCD�<br>CTS�RI <sup>e </sup>2�0V<br>All other inputs <sup>e </sup>0�8V||15|mA|
|IIL|Input Leakage|VDD <sup>e </sup>5�5V�VSS <sup>e </sup>0V||g10|mA|
|ICL|Clock Leakage|All other pins floating�<br>VIN <sup>e </sup>0V�5�5V||g10|mA|
|IOZ|TRI-STATE Leakage|VDD <sup>e </sup>5�5V�VSS <sup>e </sup>0V<br>VOUT <sup>e </sup>0V�5�25V<br>1) Chip deselected<br>2) WRITE mode�<br>chip selected||g20|mA|
|VILMR|MR Schmitt VIL|||0�8|V|
|VIHMR|MR Schmitt VIH||2�0||V|
|Note 1�Does no|t apply to XOUT|||||



Capacitance TA<sup>e</sup> 25�C�VDD<sup>e</sup> VSS<sup>e</sup> 0V 

|Symbol|Parameter|Conditions|Min|Typ|Max|Units|
|---|---|---|---|---|---|---|
|CXIN|Clock Input Capacitance|||7|9|pF|
|CXOUT|Clock Output Capacitance|fc <sup>e </sup>1 MHz<br>Unmeasured pins||7|9|pF|
|CIN|Input Capacitance|<br>returned to VSS||5|7|pF|
|COUT|Output Capacitance|||6|8|pF|
|CI�O|Input�Output Capacitance|||10|12|pF|



3 

## 3�0 AC Electrical Characteristics TA<sup>e</sup> 0�C to<sup>a</sup> 70�C�VDD<sup>ea</sup> 5V g10% 

|Symbol|Parameter|Conditions|Min|Max|Units|
|---|---|---|---|---|---|
|tADS|Address Strobe Width||60||ns|
|tAH|Address Hold Time||0||ns|
|tAR|RD<br>�RD Delayfrom Address|(Note 1)|30||ns|
|tAS|Address SetupTime||60||ns|
|tAW|WR<br>�WR Delayfrom Address|(Note 1)|30||ns|
|tCH|ChipSelect Hold Time||0||ns|
|tCS|ChipSelect SetupTime||60||ns|
|tCSR|RD<br>�RD Delayfrom ChipSelect|(Note 1)|30||ns|
|tCSW|WR<br>�WR Delayfrom Select|(Note 1)|30||ns|
|tDH|Data Hold Time||30||ns|
|tDS|Data SetupTime||30||ns|
|tHZ|RD<br>�RD to FloatingData Delay|�100pF loading(Note 3)|0|100|ns|
|tMR|Master Reset Pulse Width||5000||ns|
|tRA|Address Hold Time from RD<br>�RD|(Note 1)|20||ns|
|tRC|Read Cycle Delay||125||ns|
|tRCS|ChipSelect Hold Time from RD<br>�RD|(Note 1)|20||ns|
|tRD|RD<br>�RD Strobe Width||125||ns|
|tRDD|RD<br>�RD to Driver Enable�Disable|�100pF loading(Note 3)||60|ns|
|tRVD|Delayfrom RD<br>�RD to Data|�100pF loading||60|ns|
|tWA|Address Hold Time from WR<br>�WR|(Note 1)|20||ns|
|tWC|Write Cycle Delay||150||ns|
|tWCS|ChipSelect Hold Time from WR<br>�WR|(Note 1)|20||ns|
|tWR|WR<br>�WR Strobe Width||100||ns|
|tXH|Duration of Clock High Pulse|External Clock (8�Max�)|55||ns|
|tXL|Duration of Clock Low Pulse|External Clock (8�Max�)|55||ns|
|RC|Read Cycle <sup>e </sup>tAR <sup>a </sup>tRD <sup>a </sup>tRC||280||ns|
|WC|Write Cycle <sup>e </sup>tAW <sup>a </sup>tWR <sup>a </sup>tWC||280||ns|
|Baud Gener|ator|||||
|N|Baud Divisor||1|2<sup>16b</sup>1||
|tBHD|Baud Output Positive Edge Delay|100pF Load||175|ns|
|tBLD|Baud Output Negative Edge Delay|100pF Load||175|ns|
|tHW|Baud Output UpTime|fX <sup>e </sup>8�<sup>d</sup>2�100pF Load|75||ns|
|tLW|Baud Output Down Time|fX <sup>e </sup>8�<sup>d</sup>2�100pF Load|100||ns|
|Receiver||||||
|tRAI|Delayfrom Active Edge<br>of RD<br>to Reset Interrupt|||�|ns|
|tRINT|Delay from RD<br>�RD<br>(RD RBR�or RD LSR)<br>to Reset Interrupt|100 pF Load||1000|ns|
|tRXI|Delayfrom RD<br>RBR<br>to RXRDY<br>Inactive|||290|ns|
|tSCD|Delayfrom RCLK to Sample Time|||2000|ns|
|tSINT|Delay from Stop to Set Interrupt|(Note 2)||1|RCLK<br>Cycles|



Note 1� Applicable only when ADS is tied low� Note 2� In the FIFO mode (FCR0<sup>e</sup> 1) the trigger level interrupts�the receiver data available indication�the active RXRDY indication and the overrun error indication will be delayed 3 RCLKs�Status indicators (PE�FE�BI) will be delayed 3 RCLKs after the first byte has been received�For subsequently received bytes these indicators will be updated immediately after RDRBR goes inactive�Timeout interrupt is delayed 8 RCLKs� Note 3� Charge and discharge time is determined by VOL�VOH and the external loading� Note 4� These specifications are preliminary� 

4 

|3�0<br>AC Electrical Characteristics (Continued)||
|---|---|
|Symbol<br>Parameter<br>Conditions<br>Min<br>Max|Units|
|Transmitter||
|tHR<br>Delay from WR<br>�WR (WR THR)<br>100 pF Load<br>175<br>to Reset Interrupt|ns|
|tIR<br>Delay from RD<br>�RD (RD IIR) to Reset<br>100 pF Load<br>250<br>Interrupt (THRE)|ns|
|tIRS<br>Delay from Initial INTR Reset to Transmit<br>8<br>24<br>Start|BAUDOUT<br>Cycles|
|tSI<br>Delay from Initial Write to Interrupt<br>(Note 1)<br>16<br>24|BAUDOUT<br>Cycles|
|tSTI<br>Delay from Stop to Interrupt (THRE)<br>(Note 1)<br>8<br>8|BAUDOUT<br>Cycles|
|tSXA<br>Delay from Start to TXRDY active<br>100 pF Load<br>8|BAUDOUT<br>Cycles|
|tWXI<br>Delay from Write to TXRDY inactive<br>100 pF Load<br>195|ns|
|Modem Control||
|tMDO<br>Delay from WR<br>�WR (WR MCR) to<br>100 pF Load<br>200<br>Output|ns|
|tRIM<br>Delay from RD<br>�RD to Reset Interrupt<br>100 pF Load<br>250<br>(RD MSR)|ns|
|tSIM<br>Delay from MODEM Input to Set Interrupt<br>100 pF Load<br>250|ns|
|Note 1�This delay will be lengthened by 1 character time�minus the last stop bit time if the transmitter interrupt delay circuit is active�(S<br>Operation)�<br>Note 2�These specifications are preliminary�|ee FIFO Interrupt Mode|
|4�0<br>Timing Waveforms (All timings are referenced to valid 0 and valid 1)||
|External Clock Input (24�0 MHz Max�)<br>AC Test Points||
||TL�C�8652–3|
|TL�C�8652–2<br>Note 1�The 2�4V and 0�4V levels are the voltages that the inputs are driven to during AC testing�<br>Note 2�The 2�0V and 0�8V levels are the voltages at which the timing tests are made�<br>BAUDOUT<br>Timing||
||TL�C�8652–4|



5 

## 4�0 Timing Waveforms (Continued) Write Cycle 



<!-- Start of picture text -->
TL�C�8652–5<br><!-- End of picture text -->



<!-- Start of picture text -->
TL�C�8652–6<br><!-- End of picture text -->

6 







<!-- Start of picture text -->
TL�C�8652–9<br>Note 1� See Write Cycle Timing<br><!-- End of picture text -->



<!-- Start of picture text -->
TL�C�8652–10<br><!-- End of picture text -->





Note 1� This is the reading of the last byte in the FIFO� Note 2� If FCR0<sup>e</sup> 1�then tSINT<sup>e</sup> 3 RCLKs�For a timeout interrupt�tSINT<sup>e</sup> 8 RCLKs� 

8 

## 4�0 Timing Waveforms (Continued) 





<!-- Start of picture text -->
TL�C�8652–13<br><!-- End of picture text -->

Note 1� This is the reading of the last byte in the FIFO� Note 2� If FCR0<sup>e</sup> 1�tSINT<sup>e</sup> 3 RCLKs� 

Transmitter Ready (Pin 24) FCR0<sup>e</sup> 0 or FCR0<sup>e</sup> 1 and FCR3<sup>e</sup> 0 (Mode 0) 



TL�C�8652–14 

Transmitter Ready (Pin 24) FCR0<sup>e</sup> 1 and FCR3<sup>e</sup> 1 (Mode 1) 





<!-- Start of picture text -->
TL�C�8652–15<br><!-- End of picture text -->

9 

## 5�0 Block Diagram 



10 

## 6�0 Pin Descriptions 

The following describes the function of all UART pins�Some of these descriptions reference internal circuits� 

In the following descriptions�a low represents a logic 0 (0V nominal) and a high represents a logic 1 (<sup>a</sup> 2�4V nominal)� A0�A1�A2� Register Select�Pins 26–28�Address signals connected to these 3 inputs select a UART register for the CPU to read from or write to during data transfer�A table of registers and their addresses is shown below�Note that the state of the Divisor Latch Access Bit (DLAB)�which is the most significant bit of the Line Control Register�affects the selection of certain UART registers�The DLAB must be set high by the system software to access the Baud Generator Divisor Latches� 

|||Re<br>|gister<br>|Addresses<br>|
|---|---|---|---|---|
|DLAB|A2|A1|A0|Register|
|0|0|0|0|Receiver Buffer (read)�<br>Transmitter Holding<br>Register (write)|
|0|0|0|1|Interrupt Enable|
|X|0|1|0|Interrupt Identification (read)|
|X|0|1|0|FIFO Control (write)|
|X|0|1|1|Line Control|
|X|1|0|0|MODEM Control|
|X|1|0|1|Line Status|
|X|1|1|0|MODEM Status|
|X|1|1|1|Scratch|
|1|0|0|0|Divisor Latch|
|||||(least significant byte)|
|1|0|0|1|Divisor Latch<br>(most significant byte)|



ADS� Address Strobe� Pin 25�The positive edge of an active Address Strobe (ADS) signal latches the Register Select (A0�A1�A2) and Chip Select (CS0�CS1�CS2) signals� 

- Note� An active ADS input is required when the Register Select (A0�A1�A2) and Chip Select (CS0�CS1�CS2) signals are not stable for the duration of a read or write operation�If not required�tie the ADS input permanently low� 

BAUDOUT� Baud Out�Pin 15�This is the 16<sup>c</sup> clock signal from the transmitter section of the UART�The clock rate is equal to the main reference oscillator frequency divided by the specified divisor in the Baud Generator Divisor Latches� The BAUDOUT may also be used for the receiver section by tying this output to the RCLK input of the chip� 

CS0�CS1�CS2� Chip Select�Pins 12–14�When CS0 and CS1 are high and CS2 is low�the chip is selected�This enables communication between the UART and the CPU� The positive edge of an active Address Strobe signal latches the decoded chip select signals�completing chip selection�If ADS is always low�valid chip selects should stabilize according to the tCSW parameter� 

CTS� Clear to Send�Pin 36�When low�this indicates that the MODEM or data set is ready to exchange data�The CTS signal is a MODEM status input whose conditions can be tested by the CPU reading bit 4 (CTS) of the MODEM Status Register�Bit 4 is the complement of the CTS signal�Bit 0 (DCTS) of the MODEM Status Register indicates whether the CTS input has changed state since the previous reading of the MODEM Status Register�CTS has no effect on the Transmitter� 

D7–D0� Data Bus�Pins 1–8�This bus comprises eight TRISTATE input�output lines�The bus provides bidirectional communications between the UART and the CPU�Data� control words�and status information are transferred via the D7–D0 Data Bus� 

DCD� Data Carrier Detect�Pin 38�When low�indicates that the data carrier has been detected by the MODEM or data set�The DCD signal is a MODEM status input whose condition can be tested by the CPU reading bit 7 (DCD) of the MODEM Status Register�Bit 7 is the complement of the DCD signal�Bit 3 (DDCD) of the MODEM Status Register indicates whether the DCD input has changed state since the previous reading of the MODEM Status Register�DCD has no effect on the receiver� 

Note� Whenever the DCD bit of the MODEM Status Register changes state� an interrupt is generated if the MODEM Status Interrupt is enabled� 

DDIS� Driver Disable�Pin 23�This goes low whenever the CPU is reading data from the UART�It can disable or control the direction of a data bus transceiver between the CPU and the UART� 

DSR� Data Set Ready�Pin 37�When low�this indicates that the MODEM or data set is ready to establish the communications link with the UART�The DSR signal is a MODEM status input whose condition can be tested by the CPU reading bit 5 (DSR) of the MODEM Status Register�Bit 5 is the complement of the DSR signal�Bit 1 (DDSR) of the MODEM Status Register indicates whether the DSR input has changed state since the previous reading of the MODEM Status Register� 

Note� Whenever the DDSR bit of the MODEM Status Register changes state�an interrupt is generated if the MODEM Status Interrupt is enabled� 

DTR� Data Terminal Ready�Pin 33�When low�this informs the MODEM or data set that the UART is ready to establish a communications link�The DTR output signal can be set to an active low by programming bit 0 (DTR) of the MODEM Control Register to a high level�A Master Reset operation sets this signal to its inactive (high) state�Loop mode operation holds this signal in its inactive state� 

INTR� Interrupt�Pin 30�This pin goes high whenever any one of the following interrupt types has an active high condition and is enabled via the IER�Receiver Error Flag�Received Data Available�timeout (FIFO Mode only)�Transmitter Holding Register Empty�and MODEM Status�The INTR signal is reset low upon the appropriate interrupt service or a Master Reset operation� 

MR� Master Reset�Pin 35�When this input is high�it clears all the registers (except the Receiver Buffer�Transmitter Holding�and Divisor Latches)�and the control logic of the UART�The states of various output signals (SOUT�INTR� OUT 1�OUT 2�RTS�DTR) are affected by an active MR input (Refer to Table I�) This input is buffered with a TTLcompatible Schmitt Trigger with 0�5V typical hysteresis� 

OUT 1� Output 1�Pin 34�This user-designated output can be set to an active low by programming bit 2 (OUT 1) of the MODEM Control Register to a high level�A Master Reset operation sets this signal to its inactive (high) state�Loop mode operation holds this signal in its inactive state�In the XMOS parts this will achieve TTL levels� 

Note� Whenever the CTS bit of the MODEM Status Register changes state� an interrupt is generated if the MODEM Status Interrupt is enabled� 

11 

## 6�0 Pin Descriptions (Continued) 

OUT 2� Output 2�Pin 31�This user-designated output that can be set to an active low by programming bit 3 (OUT 2) of the MODEM Control Register to a high level�A Master Reset operation sets this signal to its inactive (high) state� Loop mode operation holds this signal in its inactive state�In the XMOS parts this will achieve TTL levels� 

RCLK� Receiver Clock�Pin 9�This input is the 16<sup>c</sup> baud rate clock for the receiver section of the chip� 

RD�RD� Read�Pins 22 and 21�When RD is high or RD is low while the chip is selected�the CPU can read status information or data from the selected UART register� 

Note� Only an active RD or RD input is required to transfer data from the UART during a read operation�Therefore�tie either the RD input permanently low or the RD input permanently high�when it is not used� 

RI� Ring Indicator�Pin 39�When low�this indicates that a telephone ringing signal has been received by the MODEM or data set�The RI signal is a MODEM status input whose condition can be tested by the CPU reading bit 6 (RI) of the MODEM Status Register�Bit 6 is the complement of the RI signal�Bit 2 (TERI) of the MODEM Status Register indicates whether the RI input signal has changed from a low to a high state since the previous reading of the MODEM Status Register� 

Note� Whenever the RI bit of the MODEM Status Register changes from a high to a low state�an interrupt is generated if the MODEM Status Interrupt is enabled� 

RTS� Request to Send�Pin 32�When low�this informs the MODEM or data set that the UART is ready to exchange data�The RTS output signal can be set to an active low by programming bit 1 (RTS) of the MODEM Control Register�A Master Reset operation sets this signal to its inactive (high) state�Loop mode operation holds this signal in its inactive state� 

SIN� Serial Input�Pin 10�Serial data input from the communications link (peripheral device�MODEM�or data set)� 

SOUT� Serial Output�Pin 11�Composite serial data output to the communications link (peripheral�MODEM or data set)�The SOUT signal is set to the Marking (logic 1) state upon a Master Reset operation� 

TXRDY�RXRDY� Pins 24�29�Transmitter and Receiver DMA signalling is available through two pins (24 and 29)� When operating in the FIFO mode�one of two types of DMA signalling per pin can be selected via FCR3�When operating as in the 16450 Mode�only DMA mode 0 is allowed� Mode 0 supports single transfer DMA where a transfer is made between CPU bus cycles�Mode 1 supports multitransfer DMA where multiple transfers are made continuously until the RCVR FIFO has been emptied or the XMIT FIFO has been filled� 

RXRDY� Mode 0�When in the 16450 Mode (FCR0<sup>e</sup> 0) or in the FIFO Mode (FCR0<sup>e</sup> 1�FCR3<sup>e</sup> 0) and there is at least 1 character in the RCVR FIFO or RCVR holding register�the RXRDY pin (29) will be low active�Once it is activated the RXRDY pin will go inactive when there are no more characters in the FIFO or holding register� 

RXRDY� Mode 1�In the FIFO Mode (FCR0<sup>e</sup> 1) when the FCR3<sup>e</sup> 1 and the trigger level or the timeout has been reached�the RXRDY pin will go low active�Once it is activated it will go inactive when there are no more characters in the FIFO or holding register� 

TXRDY� Mode 0�In the 16450 Mode (FCR0<sup>e</sup> 0) or in the FIFO Mode (FCR0<sup>e</sup> 1�FCR3<sup>e</sup> 0) and there are no characters in the XMIT FIFO or XMIT holding register�the TXRDY pin (24) will be low active�Once it is activated the TXRDY pin will go inactive after the first character is loaded into the XMIT FIFO or holding register� 

TXRDY� Mode 1�In the FIFO Mode (FCR0<sup>e</sup> 1) when FCR3<sup>e</sup> 1 and there are no characters in the XMIT FIFO�the TXRDY pin will go low active�This pin will become inactive when the XMIT FIFO is completely full� 

VDD� Pin 40�<sup>a</sup> 5V supply� 

VSS� Pin 20�Ground (0V) reference� 

WR�WR� Write�Pins 19 and 18�When WR is high or WR is low while the chip is selected�the CPU can write control words or data into the selected UART register� 

- Note� Only an active WR or WR input is required to transfer data to the UART during a write operation�Therefore�tie either the WR input permanently low or the WR input permanently high�when it is not used� 

XIN (External Crystal Input)�Pin 16�This signal input is used in conjunction with XOUT to form a feedback circuit for the baud rate generator’s oscillator�If a clock signal will be generated off-chip�then it should drive the baud rate generator through this pin� 

XOUT (External Crystal Output)�Pin 17�This signal output is used in conjunction with XIN to form a feedback circuit for the baud rate generator’s oscillator�If the clock signal will be generated off-chip�then this pin is unused� 

## 7�0 Connection Diagrams 



TL�C�8652–17 

Top View Order Number PC16550DN See NS Package Number N40A 

12 

|7�0<br>Connection Diagrams (Continued)<br>TQFP Package<br>Chip Carrier Package|
|---|
|TL�C�8652–26|
|Order Number PC16550DVEF<br>See NS Package Number VEF44A<br>TL�C�8652–18<br>Top View|
|<br>Order Number PC16550DV<br>See NS Package Number V44A<br>TABLE I�UART Reset Configuration|
|Register�Signal<br>Reset Control<br>Reset State|
|Interrupt Enable Register<br>Master Reset<br>0000<br>0000<br>(Note 1)|
|Interrupt Identification Register<br>Master Reset<br>0000<br>0001|
|FIFO Control<br>Master Reset<br>0000<br>0000|
|Line Control Register<br>Master Reset<br>0000<br>0000|
|MODEM Control Register<br>Master Reset<br>0000<br>0000|
|Line Status Register<br>Master Reset<br>0110<br>0000|
|MODEM Status Register<br>Master Reset<br>XXXX<br>0000<br>(Note 2)|
|SOUT<br>Master Reset<br>High|
|INTR(RCVR Errs)<br>Read LSR�MR<br>Low|
|INTR(RCVR Data Ready)<br>Read RBR�MR<br>Low|
|INTR(THRE)<br>Read IIR�Write THR�MR<br>Low|
|INTR(Modem Status Changes)<br>Read MSR�MR<br>Low|
|OUT 2<br>Master Reset<br>High|
|RTS<br>Master Reset<br>High|
|DTR<br>Master Reset<br>High|
|OUT 1<br>Master Reset<br>High|
|RCVR FIFO<br>MR�FCR1�FCR0�DFCR0<br>All Bits Low|
|XMIT FIFO<br>MR�FCR1�FCR0�DFCR0<br>All Bits Low|
|Note 1�Boldface bits are permanently low�|
|Note 2�Bits 7–4 are driven by the input signals�|



13 

|1 DLABe1<br>Divisor<br>Latch<br>(MS)<br>DLM|Bit 8|Bit 9|Bit 10|Bit 11|Bit 12|Bit 13|Bit 14|Bit 15||
|---|---|---|---|---|---|---|---|---|---|
|0 DLABe1<br>Divisor<br>Latch<br>(LS)<br>DLL|Bit 0|Bit 1|Bit 2|Bit 3|Bit 4|Bit 5|Bit 6|Bit 7||
|7<br>Scratch<br>Reg-<br>ister<br>SCR|Bit 0|Bit 1|Bit 2|Bit 3|Bit 4|Bit 5|Bit 6|Bit 7||
|6<br>MODEM<br>Status<br>Register<br>MSR|Delta<br>Clear<br>to Send<br>(DCTS)|Delta<br>Data<br>Set<br>Ready<br>(DDSR)|Trailing<br>Edge Ring<br>Indicator<br>(TERI)|Delta<br>Data<br>Carrier<br>Detect<br>(DDCD)|Clear<br>to<br>Send<br>(CTS)|Data<br>Set<br>Ready<br>(DSR)|Ring<br>Indicator<br>(RI)|Data<br>Carrier<br>Detect<br>(DCD)||
|5<br>Line<br>Status<br>Register<br>LSR|Data<br>Ready<br>(DR)|Overrun<br>Error<br>(OE)|Parity<br>Error<br>(PE)|Framing<br>Error<br>(FE)|Break<br>Interrupt<br>(BI)|Transmitter<br>Holding<br>Register<br>(THRE)|Transmitter<br>Empty<br>(TEMT)|Error in<br>RCVR<br>FIFO<br>(Note 2)||
|f Registers<br>Address<br>4<br>MODEM<br>Control<br>Register<br>MCR|Data<br>Terminal<br>Ready<br>(DTR)|Request<br>to Send<br>(RTS)|Out 1|Out 2|Loop|0|0|0||
|II�Summary o<br>Register<br>3<br>Line<br>Control<br>Register<br>LCR|Word<br>Length<br>Select<br>Bit 0<br>(WLS0)|Word<br>Length<br>Select<br>Bit 1<br>(WLS1)|Number of<br>Stop Bits<br>(STB)|Parity<br>Enable<br>(PEN)|Even<br>Parity<br>Select<br>(EPS)|Stick<br>Parity|Set<br>Break|Divisor<br>Latch<br>Access Bit<br>(DLAB)||
|TABLE<br>2<br>FIFO<br>Control<br>Register<br>(Write<br>Only)<br>FCR|FIFO<br>Enable|RCVR<br>FIFO<br>Reset|XMIT<br>FIFO<br>Reset|DMA<br>Mode<br>Select|Reserved|Reserved|RCVR<br>Trigger<br>(LSB)|RCVR<br>Trigger<br>(MSB)|d�|
|2<br>Interrupt<br>Ident�<br>Register<br>(Read<br>Only)<br>IIR|‘‘0’’ if<br>Interrupt<br>Pending|Interrupt<br>ID<br>Bit (0)|Interrupt<br>ID<br>Bit (1)|Interrupt<br>ID<br>Bit (2)<br>(Note 2)|0|0|FIFOs<br>Enabled<br>(Note 2)|FIFOs<br>Enabled<br>(Note 2)|smitted or receive|
|1 DLABe0<br>Interrupt<br>Enable<br>Register<br>IER|Enable<br>Received<br>Data<br>Available<br>Interrupt<br>(ERBFI)|Enable<br>Transmitter<br>Holding<br>Register<br>Empty<br>Interrupt<br>(ETBEI)|Enable<br>Receiver<br>Line Status<br>Interrupt<br>(ELSI)|Enable<br>MODEM<br>Status<br>Interrupt<br>(EDSSI)|0|0|0|0|first bit serially tran<br>ode�|
|0 DLABe0<br>Transmitter<br>Holding<br>Register<br>(Write<br>Only)<br>THR|Data Bit 0|Data Bit 1|Data Bit 2|Data Bit 3|Data Bit 4|Data Bit 5|Data Bit 6|Data Bit 7|ignificant bit�It is the<br>ays 0 in the 16450 M|
|0 DLABe0<br>Receiver<br>Buffer<br>Register<br>(Read<br>Only)<br>RBR|Data Bit 0<br>(Note 1)|Data Bit 1|Data Bit 2|Data Bit 3|Data Bit 4|Data Bit 5|Data Bit 6|Data Bit 7|1�Bit 0 is the least s<br>2�These bits are alw|
|Bit<br>No�|0|1|2|3|4|5|6|7|Note <br>Note|



14 

## 8�0 Registers 

The system programmer may access any of the UART registers summarized in Table II via the CPU�These registers control UART operations including transmission and reception of data�Each register bit in Table II has its name and reset state shown� 

### 8�1 LINE CONTROL REGISTER 

The system programmer specifies the format of the asynchronous data communications exchange and set the Divisor Latch Access bit via the Line Control Register (LCR)� The programmer can also read the contents of the Line Control Register�The read capability simplifies system programming and eliminates the need for separate storage in system memory of the line characteristics�Table II shows the contents of the LCR�Details on each bit follow� 

Bits 0 and 1� These two bits specify the number of bits in each transmitted or received serial character�The encoding of bits 0 and 1 is as follows� 

|Bit 1|Bit 0|Character Length|
|---|---|---|
|0|0|5 Bits|
|0|1|6 Bits|
|1|0|7 Bits|
|1|1|8 Bits|



Bit 2� This bit specifies the number of Stop bits transmitted and received in each serial character�If bit 2 is a logic 0� one Stop bit is generated in the transmitted data�If bit 2 is a logic 1 when a 5-bit word length is selected via bits 0 and 1� one and a half Stop bits are generated�If bit 2 is a logic 1 when either a 6-�7-�or 8-bit word length is selected�two Stop bits are generated�The Receiver checks the first Stopbit only�regardless of the number of Stop bits selected� 

Bit 3� This bit is the Parity Enable bit�When bit 3 is a logic 1� a Parity bit is generated (transmit data) or checked (receive data) between the last data word bit and Stop bit of the serial data�(The Parity bit is used to produce an even or odd number of 1s when the data word bits and the Parity bit are summed�) 

Bit 4� This bit is the Even Parity Select bit�When bit 3 is a logic 1 and bit 4 is a logic 0�an odd number of logic 1s is transmitted or checked in the data word bits and Parity bit� When bit 3 is a logic 1 and bit 4 is a logic 1�an even number of logic 1s is transmitted or checked� 

Bit 5� This bit is the Stick Parity bit�When bits 3�4 and 5 are logic 1 the Parity bit is transmitted and checked as a logic 0� If bits 3 and 5 are 1 and bit 4 is a logic 0 then the Parity bit is transmitted and checked as a logic 1�If bit 5 is a logic 0 Stick Parity is disabled� 

Bit 6� This bit is the Break Control bit�It causes a break condition to be transmitted to the receiving UART�When it is set to a logic 1�the serial output (SOUT) is forced to the Spacing (logic 0) state�The break is disabled by setting bit 6 to a logic 0�The Break Control bit acts only on SOUT and has no effect on the transmitter logic� 

- Note� This feature enables the CPU to alert a terminal in a computer communications system�If the following sequence is followed�no erroneous or extraneous characters will be transmitted because of the break� 

- 1�Load an all 0s�pad character�in response to THRE� 

- 2�Set break after the next THRE� 

- 3�Wait for the transmitter to be idle�(TEMT<sup>e</sup> 1)�and clear break when normal transmission has to be restored� 

- During the break�the Transmitter can be used as a character timer to accurately establish the break duration� 

||1�8432 MH<br>|TABLE III�<br>z Cystal|Baud Rates�Divisor<br>3�072 MHz<br>|s and Crystals<br>Crystal|18�432 MH<br>|z Crystal|
|---|---|---|---|---|---|---|
|Baud Rate|Decimal Divisor<br>for 16 <sup>c </sup>Clock|Percent Error|Decimal Divisor<br>for 16 <sup>c </sup>Clock|Percent Error|Decimal Divisor<br>for 16 <sup>c </sup>Clock|Percent Error|
|50|2304|�|3840|�|23040|�|
|75|1536|�|2560|�|15360|�|
|110|1047|0�026|1745|0�026|10473|�|
|134�5|857|0�058|1428|0�034|8565|�|
|150|768|�|1280|�|7680|�|
|300|384|�|640|�|3840|�|
|600|192|�|320|�|1920|�|
|1200|96|�|160|�|920|�|
|1800|64|�|107|0�312|640|�|
|2000|58|0�69|96|�|576|�|
|2400|48|�|80|�|480|�|
|3600|32|�|53|0�628|320|�|
|4800|24|�|40|�|240|�|
|7200|16|�|27|1�23|160|�|
|9600|12|�|20|�|120|�|
|19200|6|�|10|�|60|�|
|38400|3|�|5|�|30|�|
|56000|2|2�86|�|�|21|2�04|
|128000|�|�|�|�|9|�|



Note� For baud rates of 250k�300k�375k�500k�750k and 1�5M using a 24 MHz crystal causes minimal error� 

15 

## 8�0 Registers (Continued) 

Bit 7� This bit is the Divisor Latch Access Bit (DLAB)�It must be set high (logic 1) to access the Divisor Latches of the Baud Generator during a Read or Write operation�It must be set low (logic 0) to access the Receiver Buffer�the Transmitter Holding Register�or the Interrupt Enable Register� 





<!-- Start of picture text -->
TL�C�8652–20<br><!-- End of picture text -->

### Typical Crystal Oscillator Network (Note) 

|CRYSTAL|RP|RX2|C1|C2|
|---|---|---|---|---|
|3�1 MHz|1 MX|1�5k|10-30 pF|40-60 pF|
|1�8 MHz|1 MX|1�5k|10-30 pF|40-60 pF|



Note� These R and C values are approximate and may vary 2x depending on the crystal characteristics�All crystal circuits should be designed specifically for the system� 

### 8�3 PROGRAMMABLE BAUD GENERATOR 

The UART contains a programmable Baud Generator that is capable of taking any clock input from DC to 24 MHz and dividing it by any divisor from 2 to 2<sup>16b</sup> 1�The output frequency of the Baud Generator is 16<sup>c</sup> the Baud �divisor � e (frequency input) d (baud rate c 16)��Two 8-bit latches store the divisor in a 16-bit binary format�These Divisor Latches must be loaded during initialization to ensure proper operation of the Baud Generator�Upon loading either of the Divisor Latches�a 16-bit Baud counter is immediately loaded� 

Table III provides decimal divisors to use with crystal frequencies of 1�8432 MHz�3�072 MHz and 18�432 MHz�respectively�For baud rates of 38400 and below�the error obtained is minimal�The accuracy of the desired baud rate is dependent on the crystal frequency chosen�Using a divisor of zero is not recommended� 

### 8�4 LINE STATUS REGISTER 

This register provides status information to the CPU concerning the data transfer�Table II shows the contents of the Line Status Register�Details on each bit follow� Bit 0� This bit is the receiver Data Ready (DR) indicator�Bit 0 is set to a logic 1 whenever a complete incoming character has been received and transferred into the Receiver Buffer Register or the FIFO�Bit 0 is reset to a logic 0 by reading all of the data in the Receiver Buffer Register or the FIFO� 

Bit 1� This bit is the Overrun Error (OE) indicator�Bit 1 indicates that data in the Receiver Buffer Register was not read by the CPU before the next character was transferred into the Receiver Buffer Register�thereby destroying the previous character�The OE indicator is set to a logic 1 upon detection of an overrun condition and reset whenever the CPU reads the contents of the Line Status Register�If the FIFO mode data continues to fill the FIFO beyond the trigger level�an overrun error will occur only after the FIFO is full and the next character has been completely received in the shift register�OE is indicated to the CPU as soon as it happens�The character in the shift register is overwritten� but it is not transferred to the FIFO� 

Bit 2� This bit is the Parity Error (PE) indicator�Bit 2 indicates that the received data character does not have the correct even or odd parity�as selected by the even-parityselect bit�The PE bit is set to a logic 1 upon detection of a parity error and is reset to a logic 0 whenever the CPU reads the contents of the Line Status Register�In the FIFO mode this error is associated with the particular character in the FIFO it applies to�This error is revealed to the CPU when its associated character is at the top of the FIFO� 

Bit 3� This bit is the Framing Error (FE) indicator�Bit 3 indicates that the received character did not have a valid Stop bit�Bit 3 is set to a logic 1 whenever the Stop bit following the last data bit or parity bit is detected as a logic 0 bit (Spacing level)�The FE indicator is reset whenever the CPU reads the contents of the Line Status Register�In the FIFO mode this error is associated with the particular character in the FIFO it applies to�This error is revealed to the CPU when its associated character is at the top of the FIFO�The UART will try to resynchronize after a framing error�To do this it assumes that the framing error was due to the next start bit�so it samples this ‘‘start’’ bit twice and then takes in the ‘‘data’’� 

Bit 4� This bit is the Break Interrupt (BI) indicator�Bit 4 is set to a logic 1 whenever the received data input is held in the Spacing (logic 0) state for longer than a full word transmission time (that is�the total time of Start bit<sup>a</sup> data bits<sup>a</sup> Parity<sup>a</sup> Stop bits)�The BI indicator is reset whenever the CPU reads the contents of the Line Status Register�In the FIFO mode this error is associated with the particular character in the FIFO it applies to�This error is revealed to the CPU when its associated character is at the top of the FIFO� When break occurs only one zero character is loaded into the FIFO�The next character transfer is enabled after SIN goes to the marking state and receives the next valid start bit� 

Note� Bits 1 through 4 are the error conditions that produce a Receiver Line Status interrupt whenever any of the corresponding conditions are detected and the interrupt is enabled� 

16 

## 8�0 Registers (Continued) 

|FIFO<br>Mode<br>Only|I<br>Ide<br>R<br>|nterru<br>ntifica<br>egiste<br>|pt<br>tion<br>r<br>|<sup>Priority</sup>|TABLE IV�Interr<br>I|upt Control Functions<br>nterrupt Set and Reset Functions||
|---|---|---|---|---|---|---|---|
|Bit 3|Bit 2|Bit 1|Bit 0|<br>Level|Interrupt Type|Interrupt Source|Interrupt Reset Control|
|0|0|0|1|�|None|None|�|
|0|1|1|0|Highest|Receiver Line Status|Overrun Error or Parity Error or<br>FramingError or Break Interrupt|Reading the Line Status<br>Register|
|0|1|0|0|Second|Received Data Available|Receiver Data Available or Trigger<br>Level Reached|Reading the Receiver Buffer<br>Register or the FIFO Drops<br>Below the Trigger Level|
|1|1|0|0|Second|Character Timeout<br>Indication|No Characters Have Been<br>Removed From or Input to the<br>RCVR FIFO During the Last 4 Char�<br>Times and There Is at Least 1 Char�<br>in It DuringThis Time|Reading the Receiver<br>Buffer Register|
|0|0|1|0|Third|Transmitter Holding<br>Register Empty|Transmitter Holding<br>Register Empty|Reading the IIR Register (if<br>source of interrupt) or Writing<br>into the Transmitter Holding<br>Register|
|0|0|0|0|Fourth|MODEM Status|Clear to Send or Data Set Ready or<br>Ring Indicator or Data Carrier<br>Detect|Reading the MODEM<br>Status Register|



Bit 5� This bit is the Transmitter Holding Register Empty (THRE) indicator�Bit 5 indicates that the UART is ready to accept a new character for transmission�In addition�this bit causes the UART to issue an interrupt to the CPU when the Transmit Holding Register Empty Interrupt enable is set high�The THRE bit is set to a logic 1 when a character is transferred from the Transmitter Holding Register into the Transmitter Shift Register�The bit is reset to logic 0 concurrently with the loading of the Transmitter Holding Register by the CPU�In the FIFO mode this bit is set when the XMIT FIFO is empty�it is cleared when at least 1 byte is written to the XMIT FIFO� 

Bit 6� This bit is the Transmitter Empty (TEMT) indicator�Bit 6 is set to a logic 1 whenever the Transmitter Holding Register (THR) and the Transmitter Shift Register (TSR) are both empty�It is reset to a logic 0 whenever either the THR or TSR contains a data character�In the FIFO mode this bit is set to one whenever the transmitter FIFO and shift register are both empty� 

Bit 7� In the 16450 Mode this is a 0�In the FIFO mode LSR7 is set when there is at least one parity error�framing error or break indication in the FIFO�LSR7 is cleared when the CPU reads the LSR�if there are no subsequent errors in the FIFO� 

- Note� The Line Status Register is intended for read operations only�Writing to this register is not recommended as this operation is only used for factory testing�In the FIFO mode the software must load a data byte in the Rx FIFO via Loopback Mode in order to write to LSR2–LSR4� LSR0 and LSR7 can’t be written to in FIFO mode� 

### 8�5 FIFO CONTROL REGISTER 

This is a write only register at the same location as the IIR (the IIR is a read only register)�This register is used to enable the FIFOs�clear the FIFOs�set the RCVR FIFO trigger level�and select the type of DMA signalling� 

Bit 0� Writing a 1 to FCR0 enables both the XMIT and RCVR FIFOs�Resetting FCR0 will clear all bytes in both FIFOs� 

When changing from the FIFO Mode to the 16450 Mode and vice versa�data is automatically cleared from the FIFOs�This bit must be a 1 when other FCR bits are written to or they will not be programmed� 

Bit 1� Writing a 1 to FCR1 clears all bytes in the RCVR FIFO and resets its counter logic to 0�The shift register is not cleared�The 1 that is written to this bit position is self-clearing� 

Bit 2� Writing a 1 to FCR2 clears all bytes in the XMIT FIFO and resets its counter logic to 0�The shift register is not cleared�The 1 that is written to this bit position is self-clearing� 

Bit 3� Setting FCR3 to a 1 will cause the RXRDY and TXRDY pins to change from mode 0 to mode 1 if FCR0<sup>e</sup> 1 (see description of RXRDY and TXRDY pins)� 

Bit 4�5� FCR4 to FCR5 are reserved for future use� Bit 6�7� FCR6 and FCR7 are used to set the trigger level for the RCVR FIFO interrupt� 

|7|6|RCVR FIFO<br>Trigger Level (Bytes)|
|---|---|---|
|0|0|01|
|0|1|04|
|1|0|08|
|1|1|14|



### 8�6 INTERRUPT IDENTIFICATION REGISTER 

In order to provide minimum software overhead during data character transfers�the UART prioritizes interrupts into four levels and records these in the interrupt Identification Register�The four levels of interrupt conditions in order of priority are Receiver Line Status�Received Data Ready�Transmitter Holding Register Empty�and MODEM Status� 

17 

## 8�0 Registers (Continued) 

When the CPU accesses the IIR�the UART freezes all interrupts and indicates the highest priority pending interrupt to the CPU�While this CPU access is occurring�the UART records new interrupts�but does not change its current indication until the access is complete�Table II shows the contents of the IIR�Details on each bit follow� 

Bit 0� This bit can be used in a prioritized interrupt environment to indicate whether an interrupt is pending�When bit 0 is a logic 0�an interrupt is pending and the IIR contents may be used as a pointer to the appropriate interrupt service routine�When bit 0 is a logic 1�no interrupt is pending� Bits 1 and 2� These two bits of the IIR are used to identify the highest priority interrupt pending as indicated in Table IV� 

Bit 3� In the 16450 Mode this bit is 0�In the FIFO mode this bit is set along with bit 2 when a timeout interrupt is pending� Bits 4 and 5� These two bits of the IIR are always logic 0� Bits 6 and 7� These two bits are set when FCR0<sup>e</sup> 1� 

### 8�7 INTERRUPT ENABLE REGISTER 

This register enables the five types of UART interrupts� Each interrupt can individually activate the interrupt (INTR) output signal�It is possible to totally disable the interrupt system by resetting bits 0 through 3 of the Interrupt Enable Register (IER)�Similarly�setting bits of the IER register to a logic 1�enables the selected interrupt(s)�Disabling an interrupt prevents it from being indicated as active in the IIR and from activating the INTR output signal�All other system functions operate in their normal manner�including the setting of the Line Status and MODEM Status Registers�Table II shows the contents of the IER�Details on each bit follow� Bit 0� This bit enables the Received Data Available Interrupt (and timeout interrupts in the FIFO mode) when set to logic 1� 

Bit 1� This bit enables the Transmitter Holding Register Empty Interrupt when set to logic 1� 

Bit 2� This bit enables the Receiver Line Status Interrupt when set to logic 1� 

Bit 3� This bit enables the MODEM Status Interrupt when set to logic 1� 

Bits 4 through 7� These four bits are always logic 0� 

### 8�8 MODEM CONTROL REGISTER 

This register controls the interface with the MODEM or data set (or a peripheral device emulating a MODEM)�The contents of the MODEM Control Register are indicated in Table II and are described below� 

Bit 0� This bit controls the Data Terminal Ready (DTR) output�When bit 0 is set to a logic 1�the DTR output is forced to a logic 0�When bit 0 is reset to a logic 0�the DTR output is forced to a logic 1� 

Note� The DTR output of the UART may be applied to an EIA inverting line driver (such as the DS1488) to obtain the proper polarity input at the succeeding MODEM or data set� 

Bit 1� This bit controls the Request to Send (RTS) output� Bit 1 affects the RTS output in a manner identical to that described above for bit 0� 

Bit 2� This bit controls the Output 1 (OUT 1) signal�which is an auxiliary user-designated output�Bit 2 affects the OUT 1 output in a manner identical to that described above for bit 0� 

Bit 3� This bit controls the Output 2 (OUT 2) signal�which is an auxiliary user-designated output�Bit 3 affects the OUT 2 output in a manner identical to that described above for bit 0� 

Bit 4� This bit provides a local loopback feature for diagnostic testing of the UART�When bit 4 is set to logic 1�the following occur�the transmitter Serial Output (SOUT) is set to the Marking (logic 1) state�the receiver Serial Input (SIN) is disconnected�the output of the Transmitter Shift Register is ‘‘looped back’’ into the Receiver Shift Register input�the four MODEM Control inputs (DSR�CTS�RI�and DCD) are disconnected�and the four MODEM Control outputs (DTR� RTS�OUT 1�and OUT 2) are internally connected to the four MODEM Control inputs�and the MODEM Control output pins are forced to their inactive state (high)�In the loopback mode�data that is transmitted is immediately received� This feature allows the processor to verify the transmit-and received-data paths of the UART� 

In the loopback mode�the receiver and transmitter interrupts are fully operational�Their sources are external to the part�The MODEM Control Interrupts are also operational� but the interrupts’ sources are now the lower four bits of the MODEM Control Register instead of the four MODEM Control inputs�The interrupts are still controlled by the Interrupt Enable Register� 

Bits 5 through 7� These bits are permanently set to logic 0� 

### 8�9 MODEM STATUS REGISTER 

This register provides the current state of the control lines from the MODEM (or peripheral device) to the CPU�In addition to this current-state information�four bits of the MODEM Status Register provide change information�These bits are set to a logic 1 whenever a control input from the MODEM changes state�They are reset to logic 0 whenever the CPU reads the MODEM Status Register� 

The contents of the MODEM Status Register are indicated in Table II and described below� 

Bit 0� This bit is the Delta Clear to Send (DCTS) indicator� Bit 0 indicates that the CTS input to the chip has changed state since the last time it was read by the CPU� 

Bit 1� This bit is the Delta Data Set Ready (DDSR) indicator� Bit 1 indicates that the DSR input to the chip has changed state since the last time it was read by the CPU� 

Bit 2� This bit is the Trailing Edge of Ring Indicator (TERI) detector�Bit 2 indicates that the RI input to the chip has changed from a low to a high state� 

Bit 3� This bit is the Delta Data Carrier Detect (DDCD) indicator�Bit 3 indicates that the DCD input to the chip has changed state� 

Note� Whenever bit 0�1�2�or 3 is set to logic 1�a MODEM Status Interrupt is generated� 

Bit 4� This bit is the complement of the Clear to Send (CTS) input�If bit 4 (loop) of the MCR is set to a 1�this bit is equivalent to RTS in the MCR� 

Bit 5� This bit is the complement of the Data Set Ready (DSR) input�If bit 4 of the MCR is set to a 1�this bit is equivalent to DTR in the MCR� 

Bit 6� This bit is the complement of the Ring Indicator (RI) input�If bit 4 of the MCR is set to a 1�this bit is equivalent to OUT 1 in the MCR� 

18 

## 8�0 Registers (Continued) 

Bit 7� This bit is the complement of the Data Carrier Detect (DCD) input�If bit 4 of the MCR is set to a 1�this bit is equivalent to OUT 2 in the MCR� 

### 8�10 SCRATCHPAD REGISTER 

This 8-bit Read�Write Register does not control the UART in anyway�It is intended as a scratchpad register to be used by the programmer to hold data temporarily� 

### 8�11 FIFO INTERRUPT MODE OPERATION 

When the RCVR FIFO and receiver interrupts are enabled (FCR0<sup>e</sup> 1�IER0<sup>e</sup> 1) RCVR interrupts will occur as follows� 

- A�The receive data available interrupt will be issued to the CPU when the FIFO has reached its programmed trigger level�it will be cleared as soon as the FIFO drops below its programmed trigger level� 

- B�The IIR receive data available indication also occurs when the FIFO trigger level is reached�and like the interrupt it is cleared when the FIFO drops below the trigger level� 

- C�The receiver line status interrupt (IIR<sup>e</sup> 06)�as before� has higher priority than the received data available (IIR<sup>e</sup> 04) interrupt� 

- D�The data ready bit (LSR0) is set as soon as a character is transferred from the shift register to the RCVR FIFO�It is reset when the FIFO is empty� 

- When RCVR FIFO and receiver interrupts are enabled� RCVR FIFO timeout interrupts will occur as follows� 

- A�A FIFO timeout interrupt will occur�if the following conditions exist� 

   - �at least one character is in the FIFO 

   - �the most recent serial character received was longer than 4 continuous character times ago (if 2 stop bits are programmed the second one is included in this time delay)� 

- �the most recent CPU read of the FIFO was longer than 4 continuous character times ago� 

- The maximum time between a received character and a timeout interrupt will be 160 ms at 300 baud with a 12-bit receive character (i�e��1 Start�8 Data�1 Parity and 2 Stop Bits)� 

- B�Character times are calculated by using the RCLK input for a clock signal (this makes the delay proportional to the baudrate)� 

- C�When a timeout interrupt has occurred it is cleared and the timer reset when the CPU reads one character from the RCVR FIFO� 

- B�The transmitter FIFO empty indications will be delayed 1 character time minus the last stop bit time whenever the following occurs�THRE<sup>e</sup> 1 and there have not been at least two bytes at the same time in the transmit FIFO� since the last THRE<sup>e</sup> 1�The first transmitter interrupt after changing FCR0 will be immediate�if it is enabled� 

Character timeout and RCVR FIFO trigger level interrupts have the same priority as the current received data available interrupt�XMIT FIFO empty has the same priority as the current transmitter holding register empty interrupt� 

### 8�12 FIFO POLLED MODE OPERATION 

With FCR0<sup>e</sup> 1 resetting IER0�IER1�IER2�IER3 or all to zero puts the UART in the FIFO Polled Mode of operation� Since the RCVR and XMITTER are controlled separately either one or both can be in the polled mode of operation� In this mode the user’s program will check RCVR and XMITTER status via the LSR�As stated previously� 

- LSR0 will be set as long as there is one byte in the RCVR FIFO� 

LSR1 to LSR4 will specify which error(s) has occurred� Character error status is handled the same way as when in the interrupt mode�the IIR is not affected since IER2<sup>e</sup> 0� 

- LSR5 will indicate when the XMIT FIFO is empty� LSR6 will indicate that both the XMIT FIFO and shift register are empty� 

- LSR7 will indicate whether there are any errors in the RCVR FIFO� 

There is no trigger level reached or timeout condition indicated in the FIFO Polled Mode�however�the RCVR and XMIT FIFOs are still fully capable of holding characters� 

## 9�0 Typical Applications 



- D�When a timeout interrupt has not occurred the timeout timer is reset after a new character is received or after the CPU reads the RCVR FIFO� 

- When the XMIT FIFO and transmitter interrupts are enabled (FCR0<sup>e</sup> 1�IER1<sup>e</sup> 1)�XMIT interrupts will occur as follows� 

- A�The transmitter holding register interrupt (02) occurs when the XMIT FIFO is empty�it is cleared as soon as the transmitter holding register is written to (1 to 16 characters may be written to the XMIT FIFO while servicing this interrupt) or the IIR is read� 

19 







21 



|1�Life<br>support<br>devices<br>or<br>systems<br>a<br>systems which�(a) are intended for s<br>into the body�or (b) support or sustain <br>failure to perform�when properly used <br>with instructions for use provided in th<br>be reasonably expected to result in a s<br>to the user�|re<br>devices<br>or<br>urgical implant<br> life�and whose<br> in accordance<br>e labeling�can<br>ignificant injury|2�A critical comp<br>support device o<br>be reasonably e<br>support device <br>effectiveness�|onent is any compo<br>r system whose failure<br>xpected to cause the f<br> or system�or to affe|nent of a life<br> to perform can<br>ailure of the life<br>ct its safety or|
|---|---|---|---|---|
|National Semiconductor<br>National Semiconductor<br>Corporation<br>GmbH<br>2900 Semiconductor Drive<br>Livry-Gargan-Str�10<br>P�O�Box 58090<br>D-82256 Furstenfeldbruck<br>Santa Clara�CA 95052-8090<br>Germany<br>Tel�1(800) 272-9959<br>Tel�(81-41) 35-0<br>TWX�(910) 339-9240<br>Telex�527649<br>Fax�(81-41) 35-1|National Semiconductor<br>Japan Ltd�<br>Sumitomo Chemical<br>Engineering Center<br>Bldg�7F<br>1-7-1�Nakase�Mihama-Ku<br>Chiba-City�<br>Ciba Prefecture 261<br>Tel�(043) 299-2300<br>Fax�(043) 299-2500|National Semiconductor<br>Hong Kong Ltd�<br>13th Floor�Straight Block�<br>Ocean Centre�5 Canton Rd�<br>Tsimshatsui�Kowloon<br>Hong Kong<br>Tel�(852) 2737-1600<br>Fax�(852) 2736-9960|National Semiconductores<br>Do Brazil Ltda�<br>Rue Deputado Lacorda Franco<br>120-3A<br>Sao Paulo-SP<br>Brazil 05418-000<br>Tel�(55-11) 212-5066<br>Telex�391-1131931 NSBR BR<br>Fax�(55-11) 212-1181|National Semiconductor<br>(Australia) Pty�Ltd�<br>Building 16<br>Business Park Drive<br>Monash Business Park<br>Nottinghill�Melbourne<br>Victoria 3168 Australia<br>Tel�(3) 558-9999<br>Fax�(3) 558-9998|
|National does not assume any responsibility for use of any circuitry described|�no circuit patent licenses are i|mplied and National reserves the righ|t at any time without notice to change sa|id circuitry and specifications�|





