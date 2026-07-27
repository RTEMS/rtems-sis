# GRLIB IP Core manual, scoped to peripherals SIS models
Excerpted from grlib-ip-core-manual.pdf (GRIP, Frontgrade Gaisler, Jun 2026, Version 2026.2). Chapters selected: AHB/APB bridge, APB UART, DSU, GPTIMER, GRETH, IRQMP, L2C, SDCTRL -- the peripherals actually instantiated by leon3.c/gr740.c via grlib_apb_add/grlib_ahbs_add.


# 115-119: APBCTRL - AMBA AHB/APB bridge with plug&play support

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
| |<br>|<br>|<br>i nne | TT"el<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
ee<br>a<br>a eeee<br>ee<br>a<br>a<br>a<br>a<br>a eeee<br>pf<br>a ee<br><!-- End of picture text -->



# GRLIB IP Core 





## **16.9 Component declaration** 

library grlib; use grlib.amba.all; component apbctrl generic ( hindex  : integer := 0; haddr   : integer := 0; hmask   : integer := 16#fff#; nslaves : integer range 1 to NAPBSLV := NAPBSLV; debug   : integer range 0 to 2 := 2;   -- print config to console icheck  : integer range 0 to 1 := 1 ); port ( rst     : in  std_ulogic; clk     : in  std_ulogic; ahbi    : in  ahb_slv_in_type; ahbo    : out ahb_slv_out_type; apbi    : out apb_slv_in_type; apbo    : in  apb_slv_out_vector ); end component; 

## **16.10 Instantiation** 

This example shows how an APB bridge can be instantiated. 

library ieee; use ieee.std_logic_1164.all; library grlib; use grlib.amba.all; use work.debug.all; . . -- AMBA signals signal ahbsi : ahb_slv_in_type; signal ahbso : ahb_slv_out_vector := (others => ahbs_none); signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); begin -- APB bridge apb0 : apbctrl-- AHB/APB bridge generic map (hindex => 1, haddr => CFG_APBADDR) port map (rstn, clk, ahbsi, ahbso(1), apbi, apbo ); -- APB slaves uart1 : apbuart generic map (pindex => 1, paddr => 1,  pirq => 2) port map (rstn, clk, apbi, apbo(1), u1i, u1o); irqctrl0 : irqmp generic map (pindex => 2, paddr => 2) port map (rstn, clk, apbi, apbo(2), irqo, irqi); 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

118 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->







# 130-141: APBUART - AMBA APB UART Serial Interface

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->









# GRLIB IP Core 





Data frame, no parity: 

Data frame, no parity: Start D0 D1 D2 D3 D4 D5 D6 D7 Stop Data frame with parity: Start D0 D1 D2 D3 D4 D5 D6 D7 Parity Stop 

_Figure 27._ UART data frames 

Following the transmission of the stop bit, if a new character is not available in the transmitter FIFO, the transmitter serial data output remains high and the transmitter shift register empty bit (TS) will be set in the UART status register. Transmission resumes and the TS is cleared when a new character is loaded into the transmitter FIFO. When the FIFO is empty the TE bit is set in the status register. If the transmitter is disabled, it will immediately stop any active transmissions including the character currently being shifted out from the transmitter shift register. The transmitter holding register may not be loaded when the transmitter is disabled or when the FIFO (or holding register) is full. If this is done, data might be overwritten and one or more frames are lost. 

The discussion above applies to any FIFO configurations including the special case with a holding register (VHDL generic _fifosize_ = 1). If FIFOs are used (VHDL generic _fifosize_ > 1) some additional status and control bits are available. The TF status bit (not to be confused with the TF control bit) is set if the transmitter FIFO is currently full and the TH bit is set as long as the FIFO is _less_ than halffull (less than half of entries in the FIFO contain data). The TF control bit enables FIFO interrupts when set. The status register also contains a counter (TCNT) showing the current number of data entries in the FIFO. 

When flow control is enabled, the CTSN input must be low in order for the character to be transmitted. If it is deasserted in the middle of a transmission, the character in the shift register is transmitted and the transmitter serial output then remains inactive until CTSN is asserted again. If the CTSN is connected to a receivers RTSN, overrun can effectively be prevented. 

## **18.2.2 Transmitter break process** 

A break signal is defined as a continuous ‘0’ bits on the TX line for a period of _breaksize_ clock cycles followed by a single logical ‘1’ bit. A transmit break (TB) command via the UART control register together with the (TE) bit enabled, will issue a transmission of a break signal. The (TB) bit is selfclearing, meaning the user will need to issue a new transmit break command each time a break frame is desired. When a break is requested during active FIFO transmission, the UART completes the current word before initiating the break. Once the break character is sent, normal transmission from the FIFO automatically resumes from where it was interrupted. 

The break size (BS) field in the UART control register allows users to configure the length of the break signal. This field explicitly sets the number of consecutive ‘0’ bits transmitted during a break transmission, not including the final ‘1’ that marks the end of the break. 

## **18.2.3 Receiver operation** 

The serial input signal is first passed through a meta-stability filter. The first stage of this filter consists of two flip-flops connected in series that are clocked by the system clock. The output of the second flip-flop is then sampled into a three-bit shift register at a rate of 8 times the configured baudrate. Finally, the majority vote of the bits in the shift register is used as the serial data input for the rest of 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

131 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **18.7.3 UART Control Register** 

_Table 129._ 0x08 - CTRL - UART control register 

|31<br>30|21<br>20<br>19<br>16<br>15<br>14<br>13<br>12<br>11<br>10<br>9<br>8<br>7<br>6<br>5<br>4<br>3<br>2<br>1<br>0|
|---|---|
|FA|RESERVED<br>TB<br>BS<br>NS<br>SI<br>DI<br>BI<br>DB RF TF EC LB FL PE PS<br>TI<br>RI<br>TE RE|
|0|0<br>0<br>0xA<br>NR NR NR<br>0<br>0<br>NR NR<br>0<br>NR<br>0<br>NR NR NR NR<br>0<br>0|
|r|r<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>r<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw|
|31|FIFOs available (FA) - Set to 1 when receiver and transmitter FIFOs are available. When 0, only<br>holding register are available.|
|30: 21|RESERVED|
|20|Transmit break (TB) - When set, and TE bit is enabled, the core will transmit a break signal with<br>the size specified in the BS field. This bit is self-clearing once break operation completes.|
|19: 16|Break size (BS) - The value of this field determines the length in bits of a break transfer on the<br>UART transmitter. It also indicates the expected break size for the UART receiver. The actual<br>break size is calculated as BS+1, meaning possible break character sizes of 11-16 bits.|
|15|Number of stop bits (NS) - When set to ‘1’ then two stop bits will be used, otherwise one stop bit<br>will be used.|
|14|Transmitter shift register empty interrupt enable (SI) - When set, an interrupt will be generated when<br>the transmitter shift register becomes empty. See section 18.6 for more details.|
|13|Delayed interrupt enable (DI) - When set, delayed receiver interrupts will be enabled and an inter-<br>rupt will only be generated for received characters after a delay of 4 character times + 4 bits if no<br>new character has been received during that interval. This is only applicable if receiver interrupt<br>enable is set. See section 18.6 for more details.|
|12|Break interrupt enable (BI) - When set, an interrupt will be generated each time a break character is<br>received. See section 18.6 for more details.|
|11|FIFO debug mode enable (DB) - read only, writable in the FIFO Debug Control register.|
|10|Receiver FIFO interrupt enable (RF) - when set, Receiver FIFO level interrupts are enabled.|
|9|Transmitter FIFO interrupt enable (TF) - when set, Transmitter FIFO level interrupts are enabled.|
|8|External Clock (EC) - if set, the UART scaler will be clocked by UARTI.EXTCLK.|
|7|Loop back (LB) - if set, loop back mode will be enabled.|
|6|Flow control (FL) - if set, enables flow control using CTS/RTS (when implemented).|
|5|Parity enable (PE) - if set, enables parity generation and checking.|
|4|Parity select (PS) - selects parity polarity (0 = even parity, 1 = odd parity) (when PE=1).|
|3|Transmitter interrupt enable (TI) - if set, interrupts are generated when characters are transmitted<br>(see section 18.6 for details).|
|2|Receiver interrupt enable (RI) - if set, interrupts are generated when characters are received (see sec-<br>tion 18.6 for details).|
|1|Transmitter enable (TE) - if set, enables the transmitter.|
|0|Receiver enable (RE) - if set, enables the receiver.|



## **18.7.4 UART Scaler Register** 

_Table 130._ 0x0C - SCALER - UART scaler reload register 

|31|sbits|sbits-1<br>0|
|---|---|---|
|RESERVED||SCALER RELOAD VALUE|
|0||NR|
|r||rw|



sbits-1:0 Scaler reload value 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

136 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>a aes<br>ee<br>eeee<br>es<br>es<br>es<br>es<br>es|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





-- APB signals signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); -- UART signals signal uarti : uart_in_type; signal uarto : uart_out_type; begin -- AMBA Components are instantiated here ... -- APB UART uart0 : apbuart generic map (pindex => 1, paddr => 1,  pirq => 2, console => 1, fifosize => 1) port map (rstn, clk, apbi, apbo(1), uarti, uarto); -- UART input data uarti.rxd <= rxd; -- APB UART inputs not used in this configuration uarti.ctsn <= ’0’; uarti.extclk <= ’0’; -- connect APB UART output to entity output signal txd <= uarto.txd; 

end; 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

141 



# 267-282: DSU3 - LEON3 Hardware Debug Support Unit

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
||<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>a<br><!-- End of picture text -->



FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a|
|---|





# GRLIB IP Core 





ten is held in the trace buffer index register, and is automatically incremented after each transfer. Tracing is stopped when the EN bit is reset, or when a AHB breakpoint is hit. Tracing is temporarily suspended when the processor enters debug mode, unless the trace force bit (TF) in the trace control register is set. If the trace force bit is set, the trace buffer is activated as long as the enable bit is set. The force bit is reset if an AHB breakpoint is hit and can also be cleared by software. Note that neither the trace buffer memory nor the breakpoint registers (see below) can be read/written by software when the trace buffer is enabled. 

The DSU has an internal time tag counter and this counter is frozen when the processor enters debug mode. When AHB tracing is performed in debug mode (using the trace force bit) it may be desirable to also enable the time tag counter. This can be done using the timer enable bit (TE). Note that the time tag is also used for the instruction trace buffer and the timer enable bit should only be set when using the DSU as an AHB trace buffer only, and not when performing profiling or software debugging. The timer enable bit is reset on the same events as the trace force bit. 

## **32.3.1 AHB trace buffer filters** 

The DSU can be implemented with filters that can be applied to the AHB trace buffer, breakpoints and watchpoints. If implemented, these filters are controlled via the AHB trace buffer filter control and AHB trace buffer filter mask registers. The fields in these registers allows masking access characteristics such as master, slave, read, write and address range so that accesses that correspond to the specified mask are not written into the trace buffer. Address range masking is done using the second AHB breakpoint register set. The values of the LD and ST fields of this register has no effect on filtering. 

## **32.3.2 AHB statistics** 

The DSU can be implemented to generate statistics from the traced AHB bus. When statistics collection is enabled the DSU will assert outputs that are suitable to connect to a LEON3 statistics unit (L3STAT). The statistical outputs can be filtered by the AHB trace buffer filters, this is controlled by the Performance counter Filter bit (PF) in the AHB trace buffer filter control register. The DSU can collect data for the events listed in table 292 below. 

_Table 292._ AHB events 

|**Event**|**Description**|**Note**|
|---|---|---|
|idle|HTRANS=IDLE|Active when HTRANS IDLE is driven on the AHB slave inputs and<br>slave has asserted HREADY.|
|busy|HTRANS=BUSY|Active when HTRANS BUSY is driven on the AHB slave inputs and<br>slave has asserted HREADY.|
|nseq|HTRANS=NONSEQ|Active when HTRANS NONSEQ is driven on the AHB slave inputs<br>and slave has asserted HREADY.|
|seq|HTRANS=SEQ|Active when HTRANS SEQUENTIAL is driven on the AHB slave<br>inputs and slave has asserted HREADY.|
|read|Read access|Active when HTRANS is SEQUENTIAL or NON-SEQUENTIAL,<br>slave has asserted HREADY and the HWRITE input is low.|
|write|Write access|Active when HTRANS is SEQUENTIAL or NON-SEQUENTIAL,<br>slave has asserted HREADY and the HWRITE input is high.|
|hsize[5:0]|Transfer size|Active when HTRANS is SEQUENTIAL or NON-SEQUENTIAL,<br>slave has asserted HREADY and HSIZE is BYTE (hsize[0]),<br>HWORD (HSIZE[1]), WORD (hsize[2]), DWORD (hsize[3]),<br>4WORD hsize[4], or 8WORD (hsize[5]).|
|ws|Wait state|Active when HREADY input to AHB slaves is low and AMBA<br>response is OKAY.|
|retry|RETRY response|Active when master receives RETRY response|
|split|SPLIT response|Active when master receives SPLIT response|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

269 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->











<!-- Start of picture text -->
FEEEEEEEEEP<br><!-- End of picture text -->



FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **32.6.4 DSU trap register** 

The DSU trap register is a read-only register that indicates which SPARC trap type that caused the processor to enter debug mode. When debug mode is force by setting the BN bit in the DSU control register, the trap type will be 0xb (hardware watchpoint trap). 

_Table 299._ 0x400020 - DTR - DSU Trap register 

|31|13<br>12<br>11<br>4<br>3<br>0|
|---|---|
||RESERVED<br>EM<br>TRAPTYPE<br>R|
|31: 13|RESERVED|
|12|Error mode (EM) - Set if the trap would have cause the processor to enter error mode.|
|11: 4|Trap type (TRAPTYPE) - 8-bit SPARC trap type|
|3: 0|Read as 0x0|



## **32.6.5 DSU time tag counter** 

The trace buffer time tag counter is incremented each clock as long as the processor is running. The counter is stopped when the processor enters debug mode and when the DSU is disabled (unless the timer enable bit in the AHB trace buffer control register is set), and restarted when execution is resumed. 

_Table 300._ 0x000008 - DTTC - DSU time tag counter 

|31|0|
|---|---|
||TIMETAG|
||0|
||rw|
|31: 0|DSU Time Tag Value (TIMETAG)|



The value is used as time tag in the instruction and AHB trace buffer. 

The width of the timer is configurable at implementation time. 

## **32.6.6 DSU ASI register** 

The DSU can perform diagnostic accesses to different ASI areas. The value in the ASI diagnostic access register is used as ASI while the address is supplied from the DSU. 

_Table 301._ 0x400024 - DASI - ASI diagnostic access register 

|31|8<br>7<br>0|
|---|---|
||RESERVED<br>ASI|
||0<br>NR|
||r<br>rw|
|31: 8|RESERVED|
|7: 0|ASI (ASI) - ASI to be used on diagnostic ASI access|



## **32.6.7 AHB Trace buffer control register** 

The AHB trace buffer is controlled by the AHB trace buffer control register: 

_Table 302._ 0x000040 - ATBC - AHB trace buffer control register 

|31<br>16<br>15|8<br>7<br>6<br>5|4<br>3|2<br>1<br>0|
|---|---|---|---|
|DCNT<br>RESERVED|DF SF TE TF|BW|BR DM EN|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

274 

# GRLIB IP Core 





_Table 302._ 0x000040 - ATBC - AHB trace buffer control register 

||0<br>0<br>0<br>0<br>0<br>0<br>0<br>0<br>0<br>0|
|---|---|
||rw<br>r<br>rw<br>rw<br>rw<br>rw<br>r<br>rw<br>rw<br>rw|
|31: 16|Trace buffer delay counter (DCNT) - Note that the number of bits actually implemented depends on<br>the size of the trace buffer.|
|15: 9|RESERVED|
|8|Enable Debug Mode Timer Freeze (DF) - The time tag counter keeps counting in debug mode when<br>at least one of the processors has the internal timer enabled. If this bit is set to ‘1’ then the time tag<br>counter is frozen when the processors have entered debug mode.|
|7|Sample Force (SF) - If this bit is written to ‘1’ it will have the same effect on the AHB trace buffer as<br>if HREADY was asserted on the bus at the same time as a sequential or non-sequential transfer is<br>made. This means that setting this bit to ‘1’ will cause the values in the trace buffer’s sample regis-<br>ters to be written into the trace buffer, and new values will be sampled into the registers. This bit will<br>automatically be cleared after one clock cycle.|
||Writing to the trace buffer still requires that the trace buffer is enabled (EN bit set to ‘1’) and that the<br>CPU is not in debug mode or that tracing is forced (TF bit set to ‘1’). This functionality is primarily<br>of interest when the trace buffer is tracing a separate bus and the traced bus appears to have frozen.|
|6|Timer enable (TE) - Activates time tag counter also in debug mode.|
|5|Trace force (TF) - Activates trace buffer also in debug mode. Note that the trace buffer must be disa-<br>bled when reading out trace buffer data via the core’s register interface.|
|4: 3|Bus width (BW) - This value corresponds to log2(Supported bus width / 32)|
|2|Break (BR) - If set, the processor will be put in debug mode when AHB trace buffer stops due to<br>AHB breakpoint hit.|
|1|Delay counter mode (DM) - Indicates that the trace buffer is in delay counter mode.|
|0|Trace enable (EN) - Enables the trace buffer.|



## **32.6.8 AHB trace buffer index register** 

The AHB trace buffer index register contains the address of the next trace line to be written. 

_Table 303._ 0x000044 - ATBI - AHB trace buffer index register 

|31|4<br>3<br>0|
|---|---|
||INDEX<br>R|
||NR<br>0|
||rw<br>r|
|31: 4|Trace buffer index counter (INDEX) - Note that the number of bits actually implemented depends on<br>the size of the trace buffer.|
|3: 0|Read as 0x0|



## **32.6.9 AHB trace buffer filter control register** 

The trace buffer filter control register is only available if the core has been implemented with support for AHB trace buffer filtering. 

_Table 304._ 0x000048 - ATBFC - AHB trace buffer filter control register 

|31|14|13<br>12|11<br>10|9<br>8|7<br>4|3<br>2|1<br>0|
|---|---|---|---|---|---|---|---|
|RESERVED||WPF|R|BPF|RESERVED|PF AF|FR FW|
|0||0|0|0|0|0<br>0|0<br>0|
|r||rw|r|rw|r|rw<br>rw|rw<br>rw|



31: 14 RESERVED 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

275 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





_Table 307._ 0x000054, 0x00005C - ATBBM - AHB trace buffer break mask register 

|31||2<br>1<br>0|
|---|---|---|
||BMASK[31:2]|LD ST|
||NR|0<br>0|
||rw|rw<br>rw|
|31: 2|Breakpoint mask (BMASK) - (see text)||
|1|Load (LD) - Break on data load address||
|0|Store (ST) - Break on data store address||



## **32.6.12 Instruction trace control register 0** 

The instruction trace control register 0 contains a pointer that indicates the next line of the instruction trace buffer to be written. 

_Table 308._ 0x110000 - ITBCO - Instruction trace control register 0 

|31<br>29<br>28|16<br>15|0|
|---|---|---|
||RESERVED|ITPOINTER|
||0|NR|
||r|rw|
|31: 28|Trace filter configuration||
|27: 16|RESERVED||
|15: 0|Instruction trace pointer (ITPOINTER) - Note that th<br>on the size of the trace buffer|e number of bits actually implemented depends|



## **32.6.13 Instruction trace control register 1** 

The instruction trace control register 1 contains settings used for trace buffer overflow detection, in addition it includes settings used for some of the instruction trace buffer filtering options. This register can be written while the processor is running. 

Bits [31:28] is used to enable or disable Instruction Trace Buffer Address based Filtering (ITBAF). ITBAF is intended to allow the available hardware watch-point (HWP) registers to be used as instruction trace buffer filters when they are not used for breakpoint operation. If a bit is set to ‘1’ in ITBAF, the corresponding address and mask information in the HWP register will be used to filter instruction trace entries based on the program counter (PC) value. Bits[31:28] corresponds to HWP[3:0] respectively. ITBAF can only be used if the corresponding HWP register exist in the hardware. Instruction Trace Buffer Address based Filtering Option (ITBAFO, Bits[19:16]) determines the type of filtering for the corresponding ITBAF entry. If an ITBAFO entry is set to ‘0’ only the PC value(s) that match the address and mask option in the corresponding HWP register will be logged in the instruction trace buffer (ITB). If a bit is set to ‘1’ only the PC value(s) that does not match the address and mask option in the corresponding HWP register will be logged in the ITB. Bits[19:16] corresponds to the option for ITBAF[3:0] respectively. If there is more than one address filtering operation is enabled, the corresponding filtering operations will be combined together. 

Bits[15:0] corresponds to ASI last digit based filtering mask (ASIFMASK). ASIFMASK is in effect when the trace filter configuration is set to 0xE (SPARC Format 3 LOAD or STORE instructions to alternate space 0x80 - 0xFF with ASI last digit base filtering). Bits[15:0] corresponds to digits [0xF:0x0] respectively. If a bit is set to ‘0’ in the ASIFMASK, the load and store instructions which have an ASI between the range of 0x80-0xFF and have the corresponding last digit are logged in the instruction trace buffer. For example if only the bit0 and bit2 of the ASIFMASK are ‘0’ then only the load and store instructions with ASIs 0x80, 0x82, 0x90, 0x92, 0xA0, 0xA2, 0xB0, 0xB2, 0xC0, 0xC2, 0xD0, 0xD2, 0xE0, 0xE2, 0xF0, 0xF2 are tracked in the ITB. After the reset of processor all the bits 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

277 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>a<br>a<br>a<br>re<br>ee ee<br>esee<br>ee<br>a|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>es<br>a<br>rs ee<br>eeee<br>ee<br>a<br>aee ee ee<br>po<br>rs es ee ee<br>a<br>aee<br>eee<br>es<br>es<br>eees<br>re<br>eeeeee|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->







# 455-464: GPTIMER - General Purpose Timer Unit

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->











FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





### **40.3.1 Scaler Value Register** 

_Table 464._ 0x00 - SCALER - Scaler value register 

|31|16|16-1|0|
|---|---|---|---|
|RESERVED|||SCALER|
|0|||all 1|
|r|||rw|



16-1: 0 Scaler value. This value will also be set by writes to the Scaler reload value register. Any unused most significant bits are reserved. Always reads as ‘000...0’. 

### **40.3.2 Scaler Reload Value Register** 

_Table 465._ 0x04 - SRELOAD - Scaler reload value register 

|31|16|16-1|0|
|---|---|---|---|
|RESERVED|||SCALER RELOAD VALUE|
|0|||all 1|
|r|||rw|



16-1: 0 Scaler reload value. Writes to this register also set the scaler value. 

Any unused most significant bits are reserved. Always read as ‘000...0’. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

457 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **40.4 Vendor and device identifiers** 

The core has vendor identifier 0x01 (Frontgrade Gaisler) and device identifier 0x011. For description of vendor and device identifiers see GRLIB IP Library User’s Manual. 

## **40.5 Implementation** 

### **40.5.1 Reset** 

The core changes reset behaviour depending on settings in the GRLIB configuration package (see GRLIB User’s Manual). 

The core will add reset for all registers if the GRLIB config package setting _grlib_sync_reset_enable_all_ is set. 

The core will use asynchronous reset for all registers if the GRLIB config package setting _grlib_async_reset_enable_ is set. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

460 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>aee<br>a<br>a<br>a<br>a<br>pfpf<br>ee<br>a<br>a|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
ee es eees<br>es<br>es es ns<br>ns<br>es es ns<br>ee es<br>ee ee<br>==<br>ne<br>EEEa eeeee<br><!-- End of picture text -->



FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->







# 680-699: GRETH - Ethernet Media Access Controller (MAC) with EDCL support

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->






## GRLIB IP Core 





The Media Independent Interface (MII) is used for communicating with the PHY. There is an Ethernet transmitter which sends all data from the AHB domain on the Ethernet using the MII interface. Correspondingly, there is an Ethernet receiver which stores all data from the Ethernet on the AHB bus. Both of these interfaces use FIFOs when transferring the data streams. The GRETH also supports the RMII which uses a subset of the MII signals. 

The EDCL and the DMA channels share the Ethernet receiver and transmitter. 

#### **53.2.2 Protocol support** 

The GRETH is implemented according to IEEE standard 802.3-2002 and IEEE standard 802.3Q2003. There is no support for the optional control sublayer. This means that packets with type 0x8808 (the only currently defined ctrl packets) are discarded. The support for 802.3Q is optional and need to be enabled via generics. 

#### **53.2.3 Clocking** 

GRETH has three clock domains: The AHB clock, Ethernet receiver clock and the Ethernet transmitter clock. The ethernet transmitter and receiver clocks are generated by the external ethernet PHY, and are inputs to the core through the MII interface. The three clock domains are unrelated to each other and all signals crossing the clock regions are fully synchronized inside the core. 

Both full-duplex and half-duplex operating modes are supported and both can be run in either 10 or 100 Mbit. The minimum AHB clock for 10 Mbit operation is 2.5 MHz, while 18 MHz is needed for 100 Mbit. Using a lower AHB clock than specified will lead to excessive packet loss. 

#### **53.2.4 RAM debug support** 

Support for debug accesses the core’s internal RAM blocks can be optionally enabled using the ramdebug VHDL generic. Setting it to 1 enables accesses to the transmitter and receiver RAM buffers and setting it to 2 enables accesses to the EDCL buffer in addition to the previous two buffers. 

The transmitter RAM buffer is accessed starting from APB address offset 0x10000 which corresponds to location 0 in the RAM. There are 512 32-bit wide locations in the RAM which results in the last address being 0x107FC corresponding to RAM location 511 (byte addressing used on the APB bus). 

Correspondingly the receiver RAM buffer is accessed starting from APB address offset 0x20000. The addresses, width and depth is the same. 

The EDCL buffers are accessed starting from address 0x30000. The number of locations depend on the configuration and can be from 256 to 16384. Each location is 32-bits wide so the maximum address is 0x3FC and 0xFFFC correspondingly. 

Before any debug accesses can be made the ramdebugen bit in the control register has to be set. During this time the debug interface controls the RAM blocks and normal operations is stopped. EDCL packets are not received. The MAC transmitter and receiver could still operate if enabled but the RAM buffers would be corrupt if debug accces are made simultaneously. Thus they MUST be disabled before the RAM debug mode is enabled. 

#### **53.2.5 Multibus version** 

There is a version of the core which has an additional master interface that can be used for the EDCL. Otherwise this version is identical to the basic version. The additional master interface is enabled with the edclsepahb VHDL generic. Then the ethi.edclsepahb signal control whether EDCL accesses are done on the standard master interface or the additional interface. Setting the signal to ‘0’ makes the EDCL use the standard master interface while ‘1’ selects the additional master. This signal is only sampled at reset and changes to this signal have no effect until the next reset. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

681 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



a 

Po 



## GRLIB IP Core 





Bits 31 to STS.NRD+10 hold the base address of descriptor area while bits STS.NRD+9 to 3 form a pointer to an individual descriptor. The first descriptor should be located at the base address and when it has been used by the GRETH the pointer field is incremented by 8 to point at the next descriptor. The pointer will automatically wrap back to zero when the upper boundary has been reached. The WR bit in the descriptors can be set to make the pointer wrap back to zero before the upper boundary.The pointer field has also been made writable for maximum flexibility but care should be taken when writing to the descriptor pointer register. It should never be touched when a transmission is active. 

The final step to activate the transmission is to set the transmit enable bit in the control register. This tells the GRETH that there are more active descriptors in the descriptor table. This bit should always be set when new descriptors are enabled, even if transmissions are already active. The descriptors must always be enabled before the transmit enable bit is set. 

#### **53.3.3 Descriptor handling after transmission** 

When a transmission of a packet has finished, status is written to the first word in the corresponding descriptor. The Underrun Error bit is set if the FIFO became empty before the packet was completely transmitted while the Attempt Limit Error bit is set if more collisions occurred than allowed. The packet was successfully transmitted only if both of these bits are zero. The other bits in the first descriptor word are set to zero after transmission while the second word is left untouched. 

The enable bit should be used as the indicator when a descriptor can be used again, which is when it has been cleared by the GRETH. There are three bits in the GRETH status register that hold transmission status. The Transmitter Error (TE) bit is set each time an transmission ended with an error (when at least one of the two status bits in the transmit descriptor has been set). The Transmitter Interrupt (TI) is set each time a transmission ended successfully. 

The transmitter AHB error (TA) bit is set when an AHB error was encountered either when reading a descriptor or when reading packet data. Any active transmissions were aborted and the transmitter was disabled. The transmitter can be activated again by setting the transmit enable register. 

#### **53.3.4 Setting up the data for transmission** 

The data to be transmitted should be placed beginning at the address pointed by the descriptor address field. The GRETH does not add the Ethernet address and type fields so they must also be stored in the data buffer. The 4 B Ethernet CRC is automatically appended at the end of each packet. Each descriptor will be sent as a single Ethernet packet. If the size field in a descriptor is greater than defined by maxsize generic + header size bytes, the packet will not be sent. 

### **53.4 Rx DMA interface** 

The receiver DMA interface is used for receiving data from an Ethernet network. The reception is done using descriptors located in memory. 

#### **53.4.1 Setting up descriptors** 

A single descriptor is shown in table 789 and 790. The address field should point to a word-aligned buffer where the received data should be stored. The GRETH will never store more than defined by the maxisize generic + header size bytes to the buffer. If the interrupt enable (IE) bit is set, an interrupt will be generated when a packet has been received to this buffer (this requires that the receiver interrupt bit in the control register is also set). The interrupt will be generated regardless of whether the packet was received successfully or not. The Wrap (WR) bit is also a control bit that should be set before the descriptor is enabled and it will be explained later in this section. 

_Table 789._ GRETH receive descriptor word 0 (address offset 0x0) 

|31<br>2|7<br>26<br>25||19<br>18<br>17<br>16<br>15<br>14|13<br>12<br>11<br>10|0|
|---|---|---|---|---|---|
|RESERVED|MC|RESERVED|LE OE CE FT AE|IE WR EN|LENGTH|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

683 

## GRLIB IP Core 





_Table 789._ GRETH receive descriptor word 0 (address offset 0x0) 

31: 27 RESERVED 26 Multicast address (MC) - The destination address of the packet was a multicast address (not broadcast). 25: 19 RESERVED 18 Length error (LE) - The length/type field of the packet did not match the actual number of received bytes. 17 Overrun error (OE) - The frame was incorrectly received due to a FIFO overrun. 16 CRC error (CE) - A CRC error was detected in this frame. 15 Frame too long (FT) - A frame larger than the maximum size was received. The excessive part was truncated. 14 Alignment error (AE) - An odd number of nibbles were received. 13 Interrupt Enable (IE) - Enable Interrupts. An interrupt will be generated when a packet has been received to this descriptor provided that the receiver interrupt enable bit in the control register is set. The interrupt is generated regardless if the packet was received successfully or if it terminated with an error. 12 Wrap (WR) - Set to one to make the descriptor pointer wrap to zero after this descriptor has been used. If this bit is not set the pointer will increment by 8. The pointer automatically wraps to zero when the 1 kB boundary of the descriptor table is reached. 11 Enable (EN) - Set to one to enable the descriptor. Should always be set last of all the descriptor fields. 10: 0 LENGTH - The number of bytes received to this descriptor. 

_Table 790._ GRETH receive descriptor word 1 (address offset 0x4) 

|31|2<br>1<br>0|
|---|---|
||ADDRESS<br>RES|
|31: 2|Address (ADDRESS) - Pointer to the buffer area from where the packet data will be loaded.|
|1: 0|RESERVED|



#### **53.4.2 Starting reception** 

Enabling a descriptor is not enough to start reception. A pointer to the memory area holding the descriptors must first be set in the GRETH. This is done in the receiver descriptor pointer register. The the beginning of the area and must start on an address that is aligned to the size of the descriptor table. The size of the descriptor table can be determined from the formula: STS.NRD*8. The STS.NRD field shows the number of entries in the descriptor table, and each descriptor size is 8 bytes. 

Bits 31 to STS.NRD+10 hold the base address of descriptor area while bits STS.NRD+9 to 3 form a pointer to an individual descriptor. The first descriptor should be located at the base address and when it has been used by the GRETH the pointer field is incremented by 8 to point at the next descriptor. The pointer will automatically wrap back to zero when the upper boundary has been reached. The WR bit in the descriptors can be set to make the pointer wrap back to zero before the upper boundary. 

The pointer field has also been made writable for maximum flexibility but care should be taken when writing to the descriptor pointer register. It should never be touched when reception is active. 

The final step to activate reception is to set the receiver enable bit in the control register. This will make the GRETH read the first descriptor and wait for an incoming packet. 

#### **53.4.3 Descriptor handling after reception** 

The GRETH indicates a completed reception by clearing the descriptor enable bit. The other control bits (WR, IE) are also cleared. The number of received bytes is shown in the length field. The parts of the Ethernet frame stored are the destination address, source address, type and data fields. Bits 17-14 in the first descriptor word are status bits indicating different receive errors. All four bits are zero after a reception without errors. The status bits are described in table 789. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

684 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





## GRLIB IP Core 





ARQ algorithm to provide reliable AHB instruction transfers. Through this link, a read or write transfer can be generated to any address on the AHB bus. The EDCL is optional and must be enabled with a generic. 

#### **53.6.1 Operation** 

The EDCL receives packets in parallel with the MAC receive DMA channel. It uses a separate MAC address which is used for distinguishing EDCL packets from packets destined to the MAC DMA channel. The EDCL also has an IP address which is set through generics. Since ARP packets use the Ethernet broadcast address, the IP-address must be used in this case to distinguish between EDCL ARP packets and those that should go to the DMA-channel. Packets that are determined to be EDCL packets are not processed by the receive DMA channel. 

When the packets are checked to be correct, the AHB operation is performed. The operation is performed with the same AHB master interface that the DMA-engines use. The replies are automatically sent by the EDCL transmitter when the operation is finished. It shares the Ethernet transmitter with the transmitter DMA-engine but has higher priority. 

#### **53.6.2 EDCL protocols** 

The EDCL accepts Ethernet frames containing IP or ARP data. ARP is handled according to the protocol specification with no exceptions. 

IP packets carry the actual AHB commands. The EDCL expects an Ethernet frame containing IP, UDP and the EDCL specific application layer parts. Table 791 shows the IP packet required by the EDCL. The contents of the different protocol headers can be found in TCP/IP literature. 

_Table 791._ The IP packet expected by the EDCL. 

|Ethernet|IP|UDP|2 B|4 B|4 B|Data 0 - 242|Ethernet|
|---|---|---|---|---|---|---|---|
|Header|Header|Header|Offset|Control word|Address|4B Words|CRC|



The following is required for successful communication with the EDCL: A correct destination MAC address as set by the generics, an Ethernet type field containing 0x0806 (ARP) or 0x0800 (IP). The IP-address is then compared with the value determined by the generics for a match. The IP-header checksum and identification fields are not checked. There are a few restrictions on the IP-header fields. The version must be four and the header size must be 5 B (no options). The protocol field must always be 0x11 indicating a UDP packet. The length and checksum are the only IP fields changed for the reply. 

The EDCL only provides one service at the moment and it is therefore not required to check the UDP port number. The reply will have the original source port number in both the source and destination fields. UDP checksum are not used and the checksum field is set to zero in the replies. 

The UDP data field contains the EDCL application protocol fields. Table 792 shows the application protocol fields (data field excluded) in packets received by the EDCL. The 16-bit offset is used to align the rest of the application layer data to word boundaries in memory and can thus be set to any value. The R/W field determines whether a read (0) or a write(1) should be performed. The length 

_Table 792._ The EDCL application layer fields in received frames. 

|16-bit Offset<br>14-bit Sequence number|1-bit R/W|10-bit Length|7-bit Unused|
|---|---|---|---|



field contains the number of bytes to be read or written. If R/W is one the data field shown in table 791 contains the data to be written. If R/W is zero the data field is empty in the received packets. Table 793 shows the application layer fields of the replies from the EDCL. The length field is always zero for replies to write requests. For read requests it contains the number of bytes of data contained in the data field. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

686 

## GRLIB IP Core 





_Table 793._ The EDCL application layer fields in transmitted frames. 

|16-bit Offset<br>14-bit sequence number|1-bit ACK/NAK|10-bit Length|7-bit Unused|
|---|---|---|---|



The EDCL implements a Go-Back-N algorithm providing reliable transfers. The 14-bit sequence number in received packets are checked against an internal counter for a match. If they do not match, no operation is performed and the ACK/NAK field is set to 1 in the reply frame. The reply frame contains the internal counter value in the sequence number field. If the sequence number matches, the operation is performed, the internal counter value is stored in the sequence number field, the ACK/ NAK field is set to 0 in the reply and the internal counter is incremented, . The length field is always set to 0 for ACK/NAK=1 frames. The unused field is not checked and is copied to the reply. It can thus be set to hold for example some extra identifier bits if needed. 

#### **53.6.3 EDCL IP and Ethernet address settings** 

The default value of the EDCL IP and MAC addresses are set by ipaddrh, ipaddrl, macaddrh and macaddrl generics. The IP address and MAC address can later be changed by software. To allow several EDCL enabled GRETH controllers on the same sub-net, the 4 LSB bits of the IP and MAC address can optionally be set by an input signal. This is enabled by setting the edcl generic = 2, and driving the 4-bit LSB value on ethi.edcladdr. 

#### **53.6.4 EDCL buffer size** 

The EDCL has a dedicated internal buffer memory which stores the received packets during processing. The size of this buffer is configurable with a VHDL generic to be able to obtain a suitable compromise between throughput and resource utilization in the hardware. Table 794 lists the different buffer configurations. For each size the table shows how many concurrent packets the EDCL can handle, the maximum size of each packet including headers and the maximum size of the data payload. Sending more packets before receiving a reply than specified for the selected buffer size will lead to dropped packets. The behavior is unspecified if sending larger packets than the maximum allowed. 

_Table 794._ EDCL buffer sizes 

|Total buffer size (kB)|Number of packet buffers|Packet buffer size (B)|Maximum data payload (B)|
|---|---|---|---|
|1|4|256|200|
|2|4|512|456|
|4|8|512|456|
|8|8|1024|968|
|16|16|1024|968|
|32|32|1024|968|
|64|64|1024|968|



### **53.7 Media Independent Interfaces** 

There are several interfaces defined between the MAC sublayer and the Physical layer. The GRETH supports two of them: The Media Independent Interface (MII) and the Reduced Media Independent Interface (RMII). 

The MII was defined in the 802.3 standard and is most commonly supported. The ethernet interface have been implemented according to this specification. It uses 16 signals. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

687 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





## GRLIB IP Core 





#### **53.9.1 Control Register** 

|_Table 797._0x00 - CTRL - GRETH control register<br>31<br>30<br>28<br>27<br>26<br>25<br>24<br>15<br>14<br>13<br>12<br>11<br>10<br>9<br>8<br>7<br>6<br>5<br>4<br>3<br>2<br>1<br>0|
|---|
|EA<br>BS<br>MA MC<br>RESERVED<br>ED RD DD ME PI<br>RES<br>SP RS PM FD<br>RI<br>TI<br>RE TE|
|*<br>*<br>*<br>*<br>*<br>0<br>*<br>*<br>0<br>0<br>0<br>0<br>1<br>0<br>0<br>0<br>0<br>0<br>0<br>0|
|r<br>r<br>r<br>r<br>r<br>r<br>rw<br>rw<br>rw<br>rw<br>rw<br>r<br>rw<br>wc<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw|
|31<br>EDCL available (EA) - Set to one if the EDCL is available.|
|30: 28<br>EDCL buffer size (BS) - Shows the amount of memory used for EDCL buffers. 0 = 1 kB, 1 = 2 kB,<br>...., 6 = 64 kB.|
|27<br>RESERVED|
|26<br>MDIO interrupts available (MA) - Set to one when the core supports mdio interrupts. Read only.|
|25<br>Multicast available (MC) - Set to one when the core supports multicast address reception. Read only.|
|24: 15<br>RESERVED|
|14<br>EDCL Disable (ED) - Set to one to disable the EDCL and zero to enable it. Reset value taken from<br>the ethi.edcldisable signal. Only available if the EDCL hardware is present in the core.|
|13<br>RAM debug enable (RD) - Set to one to enable the RAM debug mode. Reset value: ‘0’. Only avail-<br>able if the VHDL generic ramdebug is nonzero.|
|12<br>Disable duplex detection (DD) - Disable the EDCL speed/duplex detection FSM. If the FSM cannot<br>complete the detection the MDIO interface will be locked in busy mode. If software needs to access<br>the MDIO the FSM can be disabled here and as soon as the MDIO busy bit is 0 the interface is avail-<br>able. Note that the FSM cannot be reenabled again.|
|11<br>Multicast enable (ME) - Enable reception of multicast addresses. Reset value: ‘0’.|
|10<br>PHY status change interrupt enable (PI) - Enables interrupts for detected PHY status changes.|
|9: 8<br>RESERVED|
|7<br>Speed (SP) - Sets the current speed mode. 0 = 10 Mbit, 1 = 100 Mbit. Only used in RMII mode (rmii<br>= 1). A default value is automatically read from the PHY after reset. Reset value: ‘1’.|
|6<br>Reset (RS) - A one written to this bit resets the GRETH core. Self clearing. No other accesses should<br>be done .to the slave interface other than polling this bit until it is cleared.|
|5<br>Promiscuous mode (PM) - If set, the GRETH operates in promiscuous mode which means it will<br>receive all packets regardless of the destination address. Reset value: ‘0’.|
|4<br>Full duplex (FD) - If set, the GRETH operates in full-duplex mode otherwise it operates in half-<br>duplex. Reset value: ‘0’.|
|3<br>Receiver interrupt (RI) - Enable Receiver Interrupts. An interrupt will be generated each time a<br>packet is received when this bit is set. The interrupt is generated regardless if the packet was<br>received successfully or if it terminated with an error. Reset value: ‘0’.|
|2<br>Transmitter interrupt (TI) - Enable Transmitter Interrupts. An interrupt will be generated each time a<br>packet is transmitted when this bit is set. The interrupt is generated regardless if the packet was<br>transmitted successfully or if it terminated with an error. Reset value: ‘0’.|
|1<br>Receive enable (RE) - Should be written with a one each time new descriptors are enabled. As long<br>as this bit is one the GRETH will read new descriptors and as soon as it encounters a disabled<br>descriptor it will stop until RE is set again. This bit should be written with a one after the new<br>descriptors have been enabled. Reset value: ‘0’.|
|0<br>Transmit enable (TE) - Should be written with a one each time new descriptors are enabled. As long<br>as this bit is one the GRETH will read new descriptors and as soon as it encounters a disabled<br>descriptor it will stop until TE is set again. This bit should be written with a one after the new<br>descriptors have been enabled. Reset value: ‘0’.|



#### **53.9.2 Status Register** 

_Table 798._ 0x04 - STAT - GRETH status register 

|31<br>28<br>27<br>24<br>23|9<br>8|7<br>6|5<br>4|3|2<br>1<br>0|
|---|---|---|---|---|---|
|RESERVED<br>NRD<br>RESERVED|PS<br>|IA<br>TS|TA RA|TI|RI<br>TE RE|
|0<br>*<br>0|0|0<br>0|NR NR|NR|NR NR NR|
|r<br>r<br>r|wc|wc wc|wc wc|wc|wc wc wc|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

689 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





## GRLIB IP Core 





#### **53.9.6 Transmitter Descriptor Table Base Address Register** 

_Table 802._ 0x14 - TXBASE - GRETH transmitter descriptor table base address register. 

|31|X+1<br>X|3<br>2<br>0|
|---|---|---|
|BASEADDR||DESCPNT<br>RES|
|NR||0<br>0|
|rw||rw<br>r|



31: X+1 Transmitter descriptor table base address (BASEADDR) - Base address to the transmitter descriptor table.Not Reset.The value of x is given by the formula: 9 + STS.NXD 

X: 3 Descriptor pointer (DESCPNT) - Pointer to individual descriptors. Automatically incremented by the Ethernet MAC. The value of x is given by the formula: 9 + STS.NXD. 2: 0 RESERVED 

#### **53.9.7 Receiver Descriptor Table Base Address Register** 

_Table 803._ 0x18 - RXBASE - GRETH receiver descriptor table base address register. 

|31|X+1<br>X<br>|3<br>2<br>0|
|---|---|---|
||BASEADDR<br>DESCPNT|RES|
||NR<br>0|0|
||rw<br>rw|r|
|31: X+1<br>X: 3<br>2: 0|Receiver descriptor table base address (BASEADDR) - Base address to the receiver de<br>table.Not Reset. The value of x is given by the formula: 9 + STS.NXD<br>Descriptor pointer (DESCPNT) - Pointer to individual descriptors. Automatically incre<br>the Ethernet MAC. The value of x is given by the formula: 9 + STS.NXD<br>RESERVED|scriptor<br>mented by|
|**53.9.8 EDCL I**<br>_Table 804._0x1C - E<br>31|**P Register**<br>DCLIP - GRETH EDCL IP register|0|
||EDCL IP ADDRESS||
||*<br>rw||



31: 0 EDCL IP address. Reset value is set with the ipaddrh and ipaddrl generics. 

#### **53.9.9 Hash Table Msb Register** 

_Table 805._ 0x20 - HhSB - GRETH Hash table msb register 

|31|0|
|---|---|
||Hash table (64:32)|
||NR|
||rw|



31: 0 Hash table msb. Bits 64 downto 32 of the hash table. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

692 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>a<br>are ee<br>pf<br>|
|---|
|a<br>esee<br>ee<br>pf<br>reee<br>a a|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>ee<br>ee<br>a<br>pf|
|---|
|ee<br>re ee|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|aa<br>es<br>a es<br>es<br>es<br>es<br>es<br>es<br>es<br>es<br>es<br>es<br>es<br>es<br>es|
|---|
|es<br>es<br>es<br>es<br>espf<br>|<br>ee<br>Gd EE<br>**ee**<br>ee <sup>ee</sup><br>ee<br>a|





## GRLIB IP Core 





### **53.14 Library dependencies** 

Table 811 shows libraries used when instantiating the core (VHDL libraries). 

_Table 811._ Library dependencies 

|**Library**|**Package**|**Imported unit(s)**|**Description**|
|---|---|---|---|
|GRLIB|AMBA|Signals|AMBA signal definitions|
|GAISLER|NET|Signals, components|GRETH component declaration|



### **53.15 Instantiation** 

The first example shows how the non-mb version of the core can be instantiated and the second one show the mb version. 

#### **53.15.1 Non-MB version** 

library ieee; use ieee.std_logic_1164.all; library grlib; use grlib.amba.all; use grlib.tech.all; library gaisler; use gaisler.ethernet_mac.all; entity greth_ex is port ( clk  : in std_ulogic; rstn : in std_ulogic; -- ethernet signals ethi :: in  eth_in_type; etho :  in  eth_out_type ); end; architecture rtl of greth_ex is -- AMBA signals signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); signal ahbmi : ahb_mst_in_type; signal ahbmo : ahb_mst_out_vector := (others => ahbm_none); begin -- AMBA Components are instantiated here ... -- GRETH e1 : greth generic map( hindex       => 0, pindex       => 12, paddr        => 12, pirq         => 12, memtech      => inferred, mdcscaler    => 50, enable_mdio  => 1, fifosize     => 32, nsync        => 1, edcl         => 1, edclbufsz    => 8, macaddrh     => 16#00005E#, 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

697 

## GRLIB IP Core 





macaddrl     => 16#00005D#, ipaddrh      => 16#c0a8#, ipaddrl      => 16#0035#) port map( rst          => rstn, clk          => clk, ahbmi        => ahbmi, ahbmo        => ahbmo(0), apbi         => apbi, apbo         => apbo(12), ethi         => ethi, etho         => etho ); end; 

#### **53.15.2 MB version** 

library ieee; use ieee.std_logic_1164.all; library grlib; use grlib.amba.all; use grlib.tech.all; library gaisler; use gaisler.ethernet_mac.all; entity greth_ex is port ( clk  : in std_ulogic; rstn : in std_ulogic; -- ethernet signals ethi :: in  eth_in_type; etho :  in  eth_out_type ); end; architecture rtl of greth_ex is -- AMBA signals signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); signal ahbmi : ahb_mst_in_type; signal ahbmo : ahb_mst_out_vector := (others => ahbm_none); begin -- AMBA Components are instantiated here ... -- GRETH e1 : greth_mb generic map( hindex       => 0, pindex       => 12, paddr        => 12, pirq         => 12, memtech      => inferred, mdcscaler    => 50, enable_mdio  => 1, fifosize     => 32, nsync        => 1, edcl         => 1, edclbufsz    => 8, macaddrh     => 16#00005E#, macaddrl     => 16#00005D#, ipaddrh      => 16#c0a8#, ipaddrl      => 16#0035#, ehindex      => 1, edclsepahb   => 1) 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

698 

## GRLIB IP Core 





port map( rst          => rstn, clk          => clk, ahbmi        => ahbmi, ahbmo        => ahbmo(0), ahbmi2       => ahbmi, ahbmo2       => ahbmo(1), apbi         => apbi, apbo         => apbo(12), ethi         => ethi, etho         => etho ); end; 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

699 



# 1557-1566: IRQMP - Multiprocessor Interrupt Controller

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->


















FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





## GRLIB IP Core 





### **96.3.1 Interrupt Level Register** 

_Table 1894._ 0x000 - ILEVEL - Interrupt Level Register 

|31|16<br>15|1<br>0|
|---|---|---|
||RESERVED<br>IL[15:1]|R|
||0<br>NR|0|
||r<br>rw|r|
|31:16|Reserved||
|15:1|Interrupt Level_n_(IL[_n_]) - Interrupt priority level for interrupt_n_.||
|0|Reserved||



### **96.3.2 Interrupt Pending Register** 

|_Table 1895._0x004 -|IPEND - Interrupt Pending Register||
|---|---|---|
|31|16<br>15|1<br>0|
||EIP[31:16]<br>IP[15:1]|R|
||0<br>0|0|
||rw<br>rw|r|
|31:16|Extended Interrupt Pending_n_(EIP[_n_]) - Interrupt pending for extended interrupt_n_.||
|15:1|Interrupt Pending_n_(IP[_n_]) - Interrupt pending for interrupt_n_.||
|0|Reserved||



### **96.3.3 Interrupt Force Register (** **_ncpu_ = 1)** 

|_Table 1896._0x008|- IFORCE - Interrupt Force Register (_ncpu_= 1)|||
|---|---|---|---|
|31|16<br>15||1<br>0|
||RESERVED|IF[15:1]|R|
||0|0|0|
||r|rw|r|
|31:16|Reserved|||
|15:1|Interrupt Force_n_(IF[_n_]) - Force interrupt_n_for processor 0.|||
|0|Reserved|||



### **96.3.4 Interrupt Clear Register** 

_Table 1897._ 0x00C - ICLEAR - Interrupt Clear Register 

|31|16<br>15<br>1<br>0|
|---|---|
||EIC[31:16]<br>IC[15:1]<br>R|
||0<br>0<br>0|
||w<br>w<br>r|
|31:16|Extended Interrupt Clear_n_(EIC[_n_]) - Writing ‘1’ to EIC[_n_] will clear extended interrupt_n_<br>(IPEND.EIP[_n_] will be set to ‘0’). Writing ‘0’ has no effect.|
|15:1|Interrupt Clear_n_(IC[_n_]) - Writing ‘1’ to IC[_n_] will clear interrupt_n_(IPEND.IP[n] will be set to ‘0’).<br>Writing ‘0’ has no effect.|
|0|Reserved|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1561 

## GRLIB IP Core 





### **96.3.5 Multiprocessor Status Register** 

_Table 1898._ 0x010 - MPSTAT - Multiprocessor Status Register 

|31<br>2|8<br>27<br>26<br>25|20<br>19<br>16<br>15|0|
|---|---|---|---|
|NCPU|BA ER|RESERVED<br>EIRQ|STATUS[15:0]|
|*|*<br>*|0<br>*|*|
|r|r<br>r|r<br>r|rw|



31:28 Number of CPUs (NCPU) - Number of CPUs in the system minus 1. Computed from _ncpu_ -generic. 27 Broadcast Available (BA) - Set to ‘1’ if MPSTAT.NCPU>0. 26 Extended boot registers available (ER). Set to ‘1’ if _bootreg_ generic is 1. 25:20 Reserved 19:16 Extended IRQ (EIRQ) - Interrupt number (1 - 15) used for extended interrupts. Equal to the value of the _eirq_ generic. Fixed to 0 if extended interrupts are disabled. 15:0 Power-down status of processor _n_ (STATUS[ _n_ ]) - ‘1’ = power-down, ‘0’ = running. Write STATUS[ _n_ ] with ‘1’ to start processor _n_ . 

### **96.3.6 Broadcast Register (** **_ncpu_ > 1)** 

|_Table 1899._0x014|- BRDCST - Broadcast Register (_ncpu_> 1)|
|---|---|
|31|16<br>15<br>1<br>0|
||RESERVED<br>BM15:1]<br>R|
||0<br>0<br>0|
||r<br>rw<br>r|
|31:16|Reserved|
|15:1|Broadcast Mask_n_(BM[_n_]) - If BM[_n_] = ‘1’ then interrupt_n_is broadcast (written to the Force Regis-<br>ter of all CPUs), otherwise standard semantics apply (interrupt written to the IPEND register).|
|0|Reserved|



### **96.3.7 Error Mode Status Register** 

_Table 1900._ 0x018 - ERRSTAT - Error Mode Status Register 

|31<br>28<br>27|26<br>20<br>19<br>16<br>15<br>0|
|---|---|
||RESERVED<br>ERRMODE[15:0]|
||0<br>*|
||r<br>rw|
|31:16|Reserved|
|15:0|Read: Error mode status of CPU_n_(ERRMODE[_n_]) - ‘1’ = error mode, ‘0’ = other (debug/run/<br>power-down).|
||Write: Force CPU_n_into error mode|
||Register is read-only if_bootreg_generic is 0.|



### **96.3.8 Processor N Interrupt Mask Register** 

_Table 1901._ 0x040 + 4* _n_ - PIMASK _n_ - Processor _n_ Interrupt Mask Register 

|31|16<br>15<br>1<br>0|
|---|---|
||EIM[31:16]<br>IM15:1]<br>R|
||0<br>0<br>0|
||rw<br>rw<br>r|
|31:16|Extended Interrupt Mask_n_(EIM[_n_]) - Interrupt mask for extended interrupt_n_.|
|15:1|Interrupt Mask_n_(IM[n]) - If IM[_n_] = ‘0’ then interrupt_n_is masked, otherwise it is enabled.|
|0|Reserved|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1562 

## GRLIB IP Core 





### **96.3.9 Processor N Interrupt Force Register (** **_ncpu_ > 1)** 

_Table 1902._ 0x080 + 4* _n_ - PIFORCE _n_ - Processor _n_ Interrupt Force Register ( _ncpu_ > 1) 

|31|17<br>16<br>15||1<br>0|
|---|---|---|---|
||IFC[15:1]<br>R|IF15:1]|R|
||0<br>0|0|0|
||wc<br>r|rw*|r|
|31:17|Interrupt Force Clear_n_(IFC[_n_]) - Interrupt force clear f|or interrupt_n_.||
|16|Reserved|||
|15:1|Interrupt Force_n_(IF[_n_]) - Force interrupt_n_.|||
|0|Reserved|||



### **96.3.10 Processor N Extended Interrupt Acknowledge Register** 

_Table 1903._ 0x0C0 + 4* _n_ - PEXTACK _n_ - Processor _n_ Extended Interrupt Acknowledge Register 

|31|5<br>4<br>0|
|---|---|
||RESERVED<br>EID[4:0]|
||0<br>0|
||r<br>r|
|31:5|Reserved|
|4:0|Extended interrupt ID (EID) - ID (16-31) of the most recently acknowledged extended interrupt.|



If this field is 0, and support for extended interrupts exist, the last assertion of interrupt _eirq_ was not the result of an extended interrupt being asserted. If interrupt _eirq_ is forced, or asserted, this field will be cleared unless one, or more, of the interrupts 31 - 16 are enabled and set in the pending register. 

### **96.3.11 Processor N Boot Address Register (** **_bootreg_ = 1)** 

_Table 1904._ 0x200 + 0x4* _n_ - BADDR _n_ - Processor _n_ Boot Address register ( _bootreg_ = 1) 

|31<br>28<br>2|7<br>26<br>20<br>19<br>16<br>15|3<br>2<br>1|0|
|---|---|---|---|
||BOOTADDR[31:3]|RES|AS|
||-|-|-|
||w|-|w|
|31:3|Entry point for booting up processor_n_, 8-byte aligned|||
|2:1|Reserved (write 0)|||
|0|Start processor immediately after setting address|||



### **96.3.12 Interrupt Map Register N (** **_irqmap_ > 0)** 

_Table 1905._ 0x300+4* _n_ - IRQMAP _n_ - Interrupt map register _n_ 

|31<br>24<br>23|16<br>15<br>8|7<br>0|
|---|---|---|
|IRQMAP[_n_*4]|IRQMAP[_n_*4+1]<br>IRQMAP[_n_*4+2]|IRQMAP[_n_*4+3]|
|_n_*4|_n_*4+1<br>_n_*4+2|_n_*4+3|
|rw|rw<br>rw|rw|



b+7 : b Interrupt map (IRQMAP) - If the core has been implemented to support interrupt mapping then the Interrupt map register at offset 0x300 + 4* _n_ specifies the mapping for interrupt lines 4* _n_ to 4* _n_ +3. The bus interrupt line 4* _n_ + _x_ will be mapped to the interrupt controller interrupt line specified by the value of IRQMAP[ _n_ *4+ _x_ ]. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1563 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
ee<br>a<br>a<br>a<br>a<br>ee<br>ee<br>ee<br>ee<br>a<br>es<br>a<br>ee<br>a<br>a<br><!-- End of picture text -->



## GRLIB IP Core 





entity irqmp_ex is port ( clk : in std_ulogic; rstn : in std_ulogic; ...  -- other signals ); end; architecture rtl of irqmp_ex is constant NCPU : integer := 4; -- AMBA signals signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); signal ahbmi : ahb_mst_in_type; signal ahbmo : ahb_mst_out_vector := (others => ahbm_none); signal ahbsi : ahb_slv_in_type; -- GP Timer Unit input signals signal irqi   : irq_in_vector(0 to NCPU-1); signal irqo   : irq_out_vector(0 to NCPU-1); -- LEON3 signals signal leon3i : l3_in_vector(0 to NCPU-1); signal leon3o : l3_out_vector(0 to NCPU-1); begin -- 4 LEON3 processors are instantiated here cpu : for i in 0 to NCPU-1 generate u0 : leon3s generic map (hindex => i) port map (clk, rstn, ahbmi, ahbmo(i), ahbsi, irqi(i), irqo(i), dbgi(i), dbgo(i)); end generate; -- MP IRQ controller irqctrl0 : irqmp generic map (pindex => 2, paddr => 2, ncpu => NCPU) port map (rstn, clk, apbi, apbo(2), irqi, irqo); end 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1566 



# 1582-1606: L2C - Level 2 Cache controller

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br>| |<br><!-- End of picture text -->



# GRLIB IP Core 





## **98.2.2 Write policy** 

The cache can be configured to operate as write-through or copy-back cache. Before changing the write policy to write-through, the cache has to be disabled and flushed (to write back dirty cache lines to memory). This can be done by setting the Cache disable bit when issuing a flush all command. The write policy is controlled via the cache control register. More fine-grained control can also be obtained by enabling the MTRR registers (see text below). 

## **98.2.3 Memory type range registers** 

The memory type range registers (MTRR) are used to control the cache operation with respect to the address. Each MTRR can define an area in memory to be uncached, write-through or write-protected. The MTRR consist of a 14-bit address field, a 14-bit mask and two 2-bit control fields. The address field is compared to the 14 most significant bits of the cache address, masked by the mask field. If the unmasked bits are equal to the address, an MTRR hit is declared. The cache operation is then performed according to the control fields (see register descriptions). If no hit is declared or if the MTRR is disabled, cache operation takes place according to the cache control register. The number of MTRRs is configurable through the _mtrr_ VHDL generic. When changing the value of any MTRR register, cache must be disabled and flushed (This can be done by setting the Cache disable bit when issue a flush all command). 

Note that the write-protection provided via the MTRR registers is enforced even if the cache is disabled. 

## **98.2.4 Cachability** 

The core uses a VHDL generic CACHED to determine which address range is cachable. Each bit in this 16-bit value defines the cachability of a 256 Mbyte address block on the AMBA AHB bus. A value of 16#00F3# will thus define cachable areas in 0 - 0x20000000 and 0x40000000 - 0x80000000. When the VHDL generic CACHED is 0, the cachable areas is defined by the plug&play information on the backend bus. When implemented with a AXI backend bus the cachability needs to be defined with the VHDL generic CACHED. The core can also be configured to use the HPROT signal to override the cachable area defined by VHDL generic CACHED. A access can only be redefined as noncachable by the HPROT signal. See table 1933 for information on how HPROT can change the access cachability within a cachable address area. The AMBA AHB signal HPROT[3] defines the access cacheable when active high and the AMBA AHB signal HPROT[2] defines the access bufferable when active high. 

_Table 1933._ Access cachability using HPROT. 

|**HPROT:**|**non-cachable, non-bufferable**|**non-cachable, bufferable**|**cacheable**|
|---|---|---|---|
|Read hit|Cache access*|Cache access|Cache access|
|Read miss|Memory access|Memory access|Cache allocation and Memory access|
|Write hit|Cache and Memory access|Cache access|Cache access|
|Write miss|Memory access|Memory access|Cache allocation|



* When the HPROT-Read-Hit-Bypass bit is set in the cache control register this will generate a Memory access. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1583 

# GRLIB IP Core 





## **98.2.5 Cache tag entry** 

Table 1934 show the different fields of the cache tag entry for a cache with set size equal to 1 kbyte. The number of bits implemented is depending on the cache configuration. 

||_Table 1934._L2C Cache tag entry<br><br><br><br><br>|||
|---|---|---|---|
|31|10<br>9<br>8<br>7<br>6|5|4<br>0|
||TAG<br>Valid<br>Dirty|RES|LRU|
|31 : 10|Address Tag (TAG) - Contains the address of the data held in the cache line.|||
|9 : 8|Valid bits. When set, the corresponding sub-block of the cache line contains v<br>corresponds to the lower 16 bytes sub-block (with offset 1) in the cache line a<br>sponds to the upper 16 bytes sub-block (with offset 0) in the cache line.|alid data<br>nd valid|. Valid bit 0<br>bit 1 corre-|
|7 : 6|Dirty bits When set, this sub-block contains modified data.|||
|5|RESERVED|||
|4 : 0|LRU bits|||



## **98.2.6 AHB address mapping** 

The AHB slave interface occupies three AHB address ranges. The first AHB memory bar is used for memory/cache data access. The address and size of this bar is configured via VHDL generics. The second AHB memory bar is used for access to configuration registers and the diagnostic interface. This bar has a configurable address via VHDL generic but always occupies 4 MiB in the AHB address space. The third AHB memory bar is used to map the ioarea of the backend AHB bus (to access the plug&play information on that bus, not supported when AXI backend is selected). The address and size of the this bar is configured via VHDL generics. The address is available in the userdefined register 1 of the configuration record. 

## **98.2.7 Memory protection and Error handling** 

The ft VHDL generic enables the implementation of the Error Detection And Correction (EDAC) protection for the data and tag memory. One error can be corrected and two error can be detected with the use of a (39, 32, 7) BCH code. When implemented, the EDAC functionality can dynamically be enabled or disabled. Before being enabled the cache should be flushed. The dirty and valid bits fore each cache line is implemented with TMR. When EDAC error or backend AHB/AXI error or writeprotection hit in a MTRR register is detected the error status register is updated to store the error type. The address which cause the error is also saved in the error address register. The error types is prioritised in the way that a uncorrected EDAC error will overwrite any other previously stored error in the error status register. In all other cases, the error status register has to be cleared before a new error can be stored. Each error type (correctable-, uncorrectable EDAC error, write-protection hit, backend AHB/AXI error) has a pending register bit. When set and this error is unmasked, a interrupt is generated. When uncorrectable error is detected in the read data the core will respond with a AHB error. AHB error response can also be enabled for a access that match a stored error in the error status register. Error detection is done per cache line. The core also provide a correctable error counter accessible via the error status register. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1584 

# GRLIB IP Core 





_Table 1935._ Cache action on detected EDAC error 

|**Access/Error type**|**Cache-line not dirty**|**Cache-line dirty**|
|---|---|---|
|Read, Correctable<br>Tag error|Tag is corrected before read is handled, Error sta-<br>tus is updated with a correctable error.|Tag is corrected before read is handled, Error<br>status is updated with a correctable error.|
|Read, Uncorrectable<br>Tag error|Cache-line invalidated before read is handled,<br>Error status is updated with a correctable error.|Cache-line invalidated before read is handled,<br>Error status is updated with a uncorrectable<br>error. Cache data is lost.|
|Write, Correctable<br>Tag error|Tag is corrected before write is handed, Error sta-<br>tus is updated with a correctable error.|Tag is corrected before write is handled, Error<br>status is updated with a correctable error.|
|Write, Uncorrect-<br>able Tag error|Cache-line invalidated before write is handled,<br>Error status is updated with a correctable error.|Cache-line invalidated before write is handled,<br>Error status is updated with a uncorrectable<br>error. Cache data is lost.|
|Read, Correctable<br>Data error|Cache-data is corrected and updated, Error status<br>is updated with a correctable error. AHB access<br>is not affected.|Cache-data is corrected and updated, Error sta-<br>tus is updated with a correctable error. AHB<br>access is not affected.|
|Read, Uncorrectable<br>Data error|Cache-line is invalidated, Error status is updated<br>with a correctable error. AHB access is termi-<br>nated with retry.|By default the cache-line is NOT invalidated<br>(this can be configured by bit[10] EDI in the<br>Error Handling / Injection configuration regis-<br>ter), Error status is updated with a uncorrect-<br>able error. AHB access is terminated with<br>error.|
|Write (<32-bit), Cor-<br>rectable Data error|Cache-data is corrected and updated, Error status<br>is updated with a correctable error. AHB access<br>is not affected.|Cache-data is corrected and updated, Error sta-<br>tus is updated with a correctable error. AHB<br>access is not affected.|
|Write (<32-bit),<br>Uncorrectable Data<br>error|Cache-line is re-fetched from memory, Error sta-<br>tus is updated with a correctable error. AHB<br>access is not affected.|Cache-line is invalidated, Error status is<br>updated with a uncorrectable error. AHB<br>access write data and cache data is lost.|



## **98.2.8 Scrubber** 

When EDAC protection is implemented a cache scrubber is enabled. The scrubber is controlled via two register in the cache configuration interface. To scrub one specific cache line the index and way of the line is set in the scrub control register. To issue the scrub operation, the pending bit is set to 1. The scrubber can also be configured to continuously loop through and scrub each cache line by setting the enabled bit to 1. In this mode, the delay between the scrub operation on each cache line is determine by the scrub delay register (in clock cycles). 

## **98.2.9 Locked way** 

One or more ways can be configured to be locked (not replaced). The number of way that should be locked is configured by the locked-way field in the control register. The way to be locked is starting with the uppermost way (for a 4-way associative cache way 4 is the first locked way, way 3 the second, and so on). After a way is locked, this way has to be flushed with the “way flush” function to update the tag match the desired locked address. During this “way flush” operation, the data can also be fetched from memory. 

## **98.2.10 Data priming** 

Data can be loaded from one or two address ranges. Before triggering the priming operation, the start and stop address need to be configured. To specify if one or both address ranges should be loaded the respective enable bit (PSTART0/1.EN) need to be set. To trigger the operation, the pending bit (PSTART0.P) needs to be set to '1'. If only one address range should be loaded, the first set of priming register (PSTART0, PSTOP0) should be used. The cache lines are loaded from the start address to the 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1585 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **98.4.1 Control Register** 

|_Table 1_<br>31|_937._0x00 -<br>29<br>28<br>27|L2CC - L2C Control register<br>19<br>18<br>16<br>15<br>12<br>11<br>8<br>7<br>6<br>5<br>4<br>3<br>2<br>1<br>0|
|---|---|---|
|EN ED<br>AC|REPL|RESERVED<br>BBS<br>INDEX-WAY<br>LOCK<br>RES<br>HP<br>RH<br>B<br>HP<br>B<br>UC HC WP HP|
|0<br>0|0|0<br>-<br>0<br>0<br>0<br>0<br>0<br>0<br>0<br>0<br>0|
|rw<br>rw|rw|r<br>rw<br>rw<br>rw<br>r<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw|
||31|Cache enable (EN) - When set, the cache controller is enabled. When disabled, the cache is<br>bypassed.|
||30|EDAC enable (EDAC)|
||29: 28|Replacement policy (REPL) -<br>00: LRU<br>01: (pseudo-) random<br>10: Master-index using index-replace field<br>11: Master-index using the modulus function|
||27: 19|RESERVED|
||18: 16|Backend bus size configuration (BBS) -<br>“100”: Configure backend bus size to 128-bit.<br>“011”: Configure backend bus size to 64-bit.<br>“010”: Configure backend bus size to 32-bit.<br>“000”: No configuration update is done.<br>Other values: not supported.|
||15: 12|Master-index replacement (INDEX-WAY) - Way to replace when Master-index replacement policy<br>and master index is larger than number of ways in the cache.|
||11: 8|Locked ways (LOCK) - Number of locked ways.|
||7: 6|RESERVED|
||5|HPROT read hit bypass (HPRHB) - When set, a non-cacheable and non-bufferable read access will<br>bypass the cache on a cache hit and return data from memory. Only used with HPROT support.|
||4|HPROT bufferable (HPB) - When HPROT is used to determine cachability and this bit is set, all<br>accesses is marked bufferable.|
||3|Bus usage status mode (UC) - 0 = wrapping mode, 1 = shifting mode.|
||2|Hit rate status mode (HC) - 0 = wrapping mode, 1 = shifting mode.|
||1|Write policy (WP) - When set, the cache controller uses the write-through write policy. When not<br>set, the write policy is copy-back.|
||0|HPROT enable (HP) - When set, use HPROT to determine cachability.|



## **98.4.2 Status Register** 

_Table 1938._ 0x04 - L2CS - L2C Status register 

|31|25<br>24<br>23<br>22<br>21<br>16<br>15<br>13<br>12<br>2<br>1<br>0|
|---|---|
|RESERVED|DP LS AT MP<br>MTRR<br>BBUS-W<br>WAY-SIZE<br>WAY|
|0|*<br>*<br>*<br>*<br>*<br>1<br>*<br>*|
|r|r<br>r<br>r<br>r<br>r<br>r<br>r<br>r|
|31: 26|RESERVED|
|25|Data priming (DP) - 1 = supported.|
|24|Cache line size (LS) - 1 = 64 bytes, 0 = 32 bytes.|
|23|Access time (AT) - Access timing is simulated as if memory protection is implemented|
|22|Memory protection (MP) - implemented|
|21: 16|Memory Type Range Registers (MTRR) - Number of MTRR registers implemented|
|15: 13|Backend bus width (BBUS-W) 1 = 128-bit, 2 = 64-bit, 4 = 32-bit|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1591 

# GRLIB IP Core 





_Table 1938._ 0x04 - L2CS - L2C Status register 

12: 2 Cache way size (WAY-SIZE) - Size in kBytes 1: 0 Multi-Way configuration (WAY) “00“: Direct mapped “01“: 2-way “10“: 3-way “11“: 4-way 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1592 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **98.4.5 Access Counter Register** 



<!-- Start of picture text -->
Table 1941. 0x10 - L2CACC - Access counter register<br>31 0<br>Access counter<br>0<br>wc<br>31 : 0 Access counter. Write 0 to clear internal access/hit counter and update access/hit counter register.<br><!-- End of picture text -->

## **98.4.6 Hit Counter Register** 



<!-- Start of picture text -->
Table 1942. 0x14 - L2CHIT - Hit counter register<br>31 0<br>Hit counter<br>0<br>wc<br>31 : 0 Hit counter.<br><!-- End of picture text -->

## **98.4.7 Front-side Bus Cycle Counter Register** 



<!-- Start of picture text -->
Table 1943. 0x18 - L2CFSCCNT - Front-side bus cycle counter register<br>31 0<br>Bus cycle counter<br>0<br>wc<br>31 : 0 Bus cycle counter. Write 0 to clear internal bus cycle/usage counter and update bus cycle/usage<br>counter register.<br><!-- End of picture text -->

## **98.4.8 Front-side Bus Usage Counter Register** 

_Table 1944._ 0x1C - L2CFSUCNT - Front-side bus usage counter register (address offset 0x1C) 



<!-- Start of picture text -->
31 0<br>Bus usage counter<br>0<br>wc<br>31 : 0 Bus usage counter.<br><!-- End of picture text -->

## **98.4.9 Error Status/Control** 



<!-- Start of picture text -->
Table 1945. 0x20 - L2CERR - L2CError status/control register<br>31 28 27 26 24 23 22 21 20 19 18 16 15 12 11 8 7 6 5 4 3 2 1 0<br>AHB S TYPE T C M V D Correctable IRQ IRQ Select  Select  X R C R<br>master C A O U A I error pending mask CB TCB C C O S<br>index R G R L L S counter B B M T<br>U / / T I E P<br>B D U I D R<br>A C E<br>T O S<br>A R P<br>NR NR NR NR NR NR NR 0 NR NR 0 0 0 0 0 0 0<br>r r r r r r r rw r r rw rw rw rw rw rw w<br>31: 28 AHB master that generated the access<br>27 Scrub error (SCRUB) - Indicates that the error was trigged by the scrubber.<br><!-- End of picture text -->



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1594 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **98.4.13 Scrub Control/Status Register** 

_Table 1951._ 0x30 - L2CSCRUB - L2C Scrub control/status register 

|31||16<br>15|6<br>5<br>4|3<br>2|1<br>0|
|---|---|---|---|---|---|
||INDEX|RESERVED|WAY|RES|PE<br>N<br>EN|
||0|0|0|0|0<br>0|
||rw|r|rw|r|rw<br>rw|



31: 16 Scrub Index (INDEX) - Index for the next line scrub operation 15: 6 RESERVED 5: 4 Scrub Way (WAY) - Way for the next line scrub operation 3: 2 RESERVED 1 Scrub Pending (PEN) - Indicates when a line scrub operation is pending. When the scrubber is disabled, writing ‘1’ to this bit scrubs one line. 0 Scrub Enable (EN) - Enables / disables the automatic scrub functionality. 

## **98.4.14 Scrub Delay Register** 

_Table 1952._ 0x34 - L2CSDEL - L2C Scrub delay register 

|31|16<br>15<br>0|
|---|---|
||RESERVED<br>DEL|
||0<br>0|
||r<br>rw|
|31: 16|RESERVED|
|15: 0|Scrub Delay (DEL) - Delay the scrubber waits before issue the next line scrub operation|



## **98.4.15 Error Injection Register** 

_Table 1953._ 0x38 - L2CEINJ0 - L2C Error injection register (Mode 0) 

|31|2<br>1<br>0|
|---|---|
||ADDR<br>R<br>INJ|
||0<br>0<br>0|
||rw<br>r<br>rw|
|31: 2|Error Inject address (ADDR). ADDR specify address bits[31:2]. Address bit[1:0] is set to zero<br>(unused by the error injection functionality).|
|1:|RESERVED|
|0|Inject error (INJ) - Set to ‘1’ to inject a error at “address”.|



_Table 1954._ 0x38 - L2CEINJ1 - L2C Error injection register (Mode 1) 

|31<br>28<br>27||5<br>4<br>2|1<br>0|
|---|---|---|---|
|WAY|INDEX|OFFSET|R<br>INJ|
|0|0|0|0<br>0|
|rw|rw|rw|r<br>rw|
|31: 28|Error Inject cache-line way (WAY)|||
|27: 5|Error Injection cache-line Index (INDEX)|||
|4: 2|Error Injection cache-line offset (OFFSET)|||
|1:|RESERVED|||
|0|Inject error (INJ) - Set to ‘1’ to inject a error at “address”.|||





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1598 

# GRLIB IP Core 





## **98.4.16 Access control register** 

|_Table 1955._0x3C -|L2CACCC - L2C Access control register||||
|---|---|---|---|---|
|31|16<br>15<br>12|11<br>10<br>9<br>8<br>7<br>6<br>5<br>4|3<br>2|1<br>0|
||AxCACHE<br>R<br>E<br>S<br>D<br>S<br>C<br>SH RF<br>CL|PS SP<br>LIT<br>Q<br>NH<br>M<br>BE<br>RR<br>OA<br>PM<br>FLI<br>NE<br>DB<br>PF<br>128<br>WF|R<br>DB<br>PW<br>S|SP<br>LIT<br>R|
||0xFFEE<br>0<br>0<br>0<br>0|0<br>0<br>0<br>0<br>0<br>0<br>0<br>0|0<br>0|0<br>0|
||rw<br>r<br>rw<br>rw<br>rw|rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw<br>rw|r<br>rw|rw<br>r|
|31: 16<br>15|AXI CACHE configuration (AxCACHE) - (only avail<br>Bit[31:28]: ARCACHE (used when the cache fetches<br>Bit[27:24]: AWCACHE (used when the cache writes b<br>Bit[23:20]: ARCACHE (used for accesses that bypass<br>Bit[19:16]: AWCACHE (used for accesses that bypass<br>RESERVED|able when AXI backend bus is<br>a cache line from memory)<br>ack a cache line to memory)<br>the cache and reads from mem<br>the cache and writes to memor|implem<br>ory)<br>y)|ented)|
|14|Disable cancellation and reissue of scrubber operation<br>same index as an ongoing scrubber operation will can<br>set to ’1’ the scrubber operation will complete without|(DSC) - When set to ’0’, a writ<br>cel and reissue the scrubber ope<br>detection of the write access.|e acces<br>ration.|s to the<br>When|



- 13 Scrubber hold (SH) - When set to ’1’ the cache will delay any new access until the current scrubber operation is complete. 

- 12 Replace full cache line (RFCL) - When set and dirty cache line is updated, the entire cache line is written back to memory. 

- 11 Priming statistic (PS) - When set, priming operation is included in the access/hit/miss statistics. 10 SPLIT queue write order (SPLITQ) When set, all write accesses (except locked) will be placed in the split queue when the split queue is not empty 

- 9 No hit for cache misses (NHM) - When set, the unsplited read access for a read miss will not trig the access/hit counters. 

- 8 Bit error status (BERR) - When set, the error status signals will represent the actual error detected rather then if the error could be corrected by refetching data from memory. 

- 7 One access/master (OAPM) - When set, only one ongoing access per master is allowed to enter the cache. A second access would receive a SPLIT response 

- 6 (FLINE) - When set, a cache line fetched from memory can be replaced before it has been read out by the requesting master. 

- 5 Disable bypass prefetching (DBPF) - When set, bypass accesses will be performed as single accesses towards memory. 

- 4 128-bit write line fetch (128WF) - When set, a 128-bit write miss will fetch the rest of the cache from memory. 

- 3 RESERVED 

- 2 Disable wait-states for discarded bypass data (DBPWS) - When set, split response is given to a bypass read access which data has been discarded and needs to refetch data from memory. 

- 1 Enabled SPLIT response (SPLIT) - When set the cache will issue a AMBA SPLIT response on cache miss 

- 0 RESERVED 

## **98.4.17 Priming start register 0** 

_Table 1956._ 0x40 - PSTART0 - L2C priming start register 

|31||5<br>4|1<br>0|
|---|---|---|---|
||ADDR|RES|P<br>EN|
||0|0|0<br>0|
||rw|r|rw<br>rw|
|31: 2|Priming start address (ADDR)|||
|4: 2|RESERVED|||





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1599 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **98.4.18 Priming stop register 0** 

_Table 1957._ 0x40 - PSTOP0 - L2C priming stop register 

|31||5<br>4<br>0|
|---|---|---|
||ADDR|RES|
||0|0|
||rw|r|
|31: 5|Priming stop address (ADDR)||
|4: 0|RESERVED||



## **98.4.19 Priming start register 1** 

_Table 1958._ 0x48 - PSTART1 - L2C priming start (second area) register 

|31||5<br>4|1<br>0|
|---|---|---|---|
||ADDR|RES|P<br>EN|
||0|0|0<br>0|
||rw|r|r<br>rw|



31: 2 Priming start address (ADDR) 4: 2 RESERVED 1 Priming access pending (P) - This bit is read only and indicates that a priming operation on the second priming area is executing. 0 Priming enable (EN) - This indicates that the first area (defined by PSTART1.ADDR to PSTOP1.ADDR) should be primed. 

## **98.4.20 Priming stop register 1** 

_Table 1959._ 0x4C - PSTOP1 - L2C priming stop (second area) register 

|31||5<br>4<br>0|
|---|---|---|
||ADDR|RES|
||0|0|
||rw|r|
|31: 5|Priming stop address (ADDR)||
|4: 0|RESERVED||



## **98.4.21 Error Handling / Injection configuration** 

_Table 1960._ 0x50 - L2CEINJCFG - L2C injection configuration register 

|31|11<br>10|9|8<br>7||4<br>3||0|
|---|---|---|---|---|---|---|---|
|RESERVED|E<br>D<br>I<br><br>|T<br>E<br>R|I<br>M<br>D|RES|M|PI|DT CB|
|0|0|0|0|0|0|0|0<br>0|
|r|rw<br>r|w|rw|r|rw|rw|rw<br>rw|



- 31: 11 RESERVED 

10 (EDI) - Enable invalidation off cache line with un-correctable data error. When set to 1 and a un-correctable data error is detected, the cache line will be invalidated (removing the error form the cache). 

9 (TER) - Disable error response on un-correctable TAG error detection. 

When set to 0 the access detecting a un-correctable TAG error would generate a AMBA error response. When set to 1 this access would not generate an error response. 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

1601 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|se<br>pT<br>a<br>a<br>a<br>ee<br>ee<br>PT<br>are<br>pT<br>apT<br>a<br>a<br>a<br>a|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|a<br>eeee<br>ee<br>ee<br>aee<br>ee<br>a<br>apf|
|---|
|aee<br>aee<br>a<br>ee|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





<!-- Start of picture text -->
ee<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br>a<br><!-- End of picture text -->



FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->







# 2037-2046: SDCTRL - 32/64-bit PC133 SDRAM Controller

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->









FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





# GRLIB IP Core 





## **126.3 Registers** 

The memory controller is programmed through register(s) mapped into the AHB I/O space defined by the controllers AHB BAR1. Only 32-bit single-accesses to the registers are supported. 

_Table 2349._ SDRAM controller registers 

|**AHB address offset**|**Register**|
|---|---|
|0x0|SDRAM Configuration register|
|0x4|SDRAM Power-Saving configuration register|



|_Table 2_|_350._0x00 - SD|CFG1 - SDRAM configuration register|
|---|---|---|
|31|30<br>29<br>27|26<br>25<br>23<br>22<br>21<br>20<br>18<br>17<br>16<br>15<br>14<br>0|
|Refresh|tRP<br>tRFC|tCD<br>SDRAM<br>bank size<br>SDRAM<br>col. size<br>SDRAM<br>command<br>Page-<br>Burst<br>MS<br>D64<br>SDRAM refresh load value|
|0|1<br>0b111|1<br>0<br>0b10<br>0<br>*<br>*<br>*<br>NR|
|rw|rw<br>rw|rw<br>rw<br>rw<br>rw<br>rw*<br>r<br>r<br>rw|
||31|SDRAM refresh. If set, the SDRAM refresh will be enabled.|
||30|SDRAM tRP timing. tRP will be equal to 2 or 3 system clocks (0/1). When mobile SDRAM support<br>is enabled, this bit also represent the MSB in the tRFC timing.|
||29: 27|SDRAM tRFC timing. tRFC will be equal to 3 + field-value system clocks. When mobile SDRAM<br>support is enabled, this field is extended with the bit 30.|
||26|SDRAM CAS delay. Selects 2 or 3 cycle CAS delay (0/1). When changed, a LOAD-MODE-REGIS-<br>TER command must be issued at the same time. Also sets RAS/CAS delay (tRCD).|
||25: 23|SDRAM banks size. Defines the decoded memory size for each SDRAM chip select: “000”= 4<br>Mbyte, “001”= 8 Mbyte, “010”= 16 Mbyte .... “111”= 512 Mbyte.|
|||When configured for 64-bit wide SDRAM data bus (sdbits=64), the meaning of this field doubles so<br>that “000”=8 Mbyte, .., “111”=1024 Mbyte|
||22: 21|SDRAM column size. “00”=256, “01”=512, “10”=1024, “11”=2048 except when bit[25:23]=˘111˘<br>then ˘11˘=4096|
||20: 18|SDRAM command. Writing a non-zero value will generate an SDRAM command: “010”=PRE-<br>CHARGE, “100”=AUTO-REFRESH, “110”=LOAD-MODE-REGISTER, “111”=LOAD-EXT-<br>MODE-REGISTER. The field is reset after command has been executed.|
||17|1 = pageburst is used for read operations, 0 = line burst of length 8 is used for read operations. (Only<br>available when VHDL generic pageburst i set to 2)|
||16|Mobile SDR support enabled. ‘1’ = Enabled, ‘0’ = Disabled (read-only)|
||15|64-bit data bus (D64) - Reads ‘1’ if memory controller is configured for 64-bit data bus, otherwise<br>‘0’. Read-only.|
||14: 0|The period between each AUTO-REFRESH command - Calculated as follows: tREFRESH =<br>((reload value) + 1) / SYSCLK|



_Table 2351._ 0x04 - SDCFG2 - SDRAM Power-Saving configuration register 

|31<br>30<br>29<br>24<br>23<br>20<br>19|18<br>16<br>15|7<br>6<br>5|4<br>3|2<br>0|
|---|---|---|---|---|
|ME CE<br>RESERVED<br>tXSR<br>R|PMODE<br>RESERVED|DS|TCSR|PASR|
|*<br>*<br>0<br>*<br>0|0<br>0|0|0|0|
|rw* rw*<br>r<br>rw*<br>r|rw<br>r|rw|rw|rw|
|31<br>Mobile SDRAM functional<br>(support for standard SDRA|ity enabled. ‘1’ = Enabled (support for M<br>M)|obile SDRA|M), ‘0’|= disabled|
|30<br>Clock enable (CE). This va<br>correct operation. This regi<br>29: 24<br>Reserved|lue is driven on the CKE inputs of the SD<br>ster bit is read only when Power-Saving m|RAM. Shou<br>ode is othe|ld be set<br>r then no|to ‘1’ for<br>ne.|





Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

2041 

FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->





rRONTGRADc 



<!-- Start of picture text -->
rRONTGRADc<br><!-- End of picture text -->



|ee<br>——<br>aa<br>ee<br>a<br>aee<br>a<br>a<br>a<br>a<br>a|
|---|





FRONTGRADE 



<!-- Start of picture text -->
FRONTGRADE<br><!-- End of picture text -->



|ee<br>a<br>a<br>a<br>a<br>BSes<br>a<br>a<br>a<br>apo<br>re<br>ee<br>a<br>a|
|---|





# GRLIB IP Core 





The example design contains an AMBA bus with a number of AHB components connected to it including the SDRAM controller. The external SDRAM bus is defined on the example designs port map and connected to the SDRAM controller. System clock and reset are generated by GR Clock Generator and Reset Generator. 

SDRAM controller decodes SDRAM area:0x60000000 - 0x6FFFFFFF. SDRAM Configuration register is mapped into AHB I/O space on address (AHB I/O base address + 0x100). 

library ieee; use ieee.std_logic_1164.all; library grlib; use grlib.amba.all; use grlib.tech.all; library gaisler; use gaisler.memctrl.all; use gaisler.pads.all;   -- used for I/O pads use gaisler.misc.all; entity mctrl_ex is port ( clk : in std_ulogic; resetn : in std_ulogic; pllref : in  std_ulogic; sdcke    : out std_logic_vector ( 1 downto 0);  -- clk en sdcsn    : out std_logic_vector ( 1 downto 0);  -- chip sel sdwen    : out std_logic;                       -- write en sdrasn   : out std_logic;                       -- row addr stb sdcasn   : out std_logic;                       -- col addr stb sddqm    : out std_logic_vector (7 downto 0);  -- data i/o mask sdclk    : out std_logic;                       -- sdram clk output sa       : out std_logic_vector(14 downto 0); -- optional sdram address sd       : inout std_logic_vector(63 downto 0) -- optional sdram data ); end; architecture rtl of mctrl_ex is -- AMBA bus (AHB and APB) signal apbi  : apb_slv_in_type; signal apbo  : apb_slv_out_vector := (others => apb_none); signal ahbsi : ahb_slv_in_type; signal ahbso : ahb_slv_out_vector := (others => ahbs_none); signal ahbmi : ahb_mst_in_type; signal ahbmo : ahb_mst_out_vector := (others => ahbm_none); signal sdi   : sdctrl_in_type; signal sdo   : sdctrl_out_type; signal clkm, rstn : std_ulogic; signal cgi : clkgen_in_type; signal cgo : clkgen_out_type; signal gnd : std_ulogic; begin -- Clock and reset generators clkgen0 : clkgen generic map (clk_mul => 2, clk_div => 2, sdramen => 1, tech => virtex2, sdinvclk => 0) port map (clk, gnd, clkm, open, open, sdclk, open, cgi, cgo); cgi.pllctrl <= "00"; cgi.pllrst <= resetn; cgi.pllref <= pllref; rst0 : rstgen port map (resetn, clkm, cgo.clklock, rstn); -- SDRAM controller sdc : sdctrl generic map (hindex => 3, haddr => 16#600#, hmask => 16#F00#, ioaddr => 1, pwron => 0, invclk => 0) 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

2045 

# GRLIB IP Core 





port map (rstn, clkm, ahbsi, ahbso(3), sdi, sdo); 

-- input signals sdi.data(31 downto 0) <= sd(31 downto 0); -- connect SDRAM controller outputs to entity output signals sa <= sdo.address; sdcke <= sdo.sdcke; sdwen <= sdo.sdwen; sdcsn <= sdo.sdcsn; sdrasn <= sdo.rasn; sdcasn <= sdo.casn; sddqm <= sdo.dqm; --Data pad instantiation with scalar bdrive sd_pad : iopadv generic map (width => 32) port map (sd(31 downto 0), sdo.data, sdo.bdrive, sdi.data(31 downto 0)); end; --Alternative data pad instantiation with vectored bdrive sd_pad : iopadvv generic map (width => 32) port map (sd(31 downto 0), sdo.data, sdo.vbdrive, sdi.data(31 downto 0)); end; 



Frontgrade Gaisler AB Kungsgatan | SE-411 19 | Goteborg | Sweden +46 31 7758650 | frontgrade.com/gaisler 

GRIP Jun 2026, Version 2026.2 

2046 

