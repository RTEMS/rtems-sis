# GR740 user's manual -- table number to PDF page

`gr740-users-manual.md` is a lossy conversion: only 224 of the 587 tables in
the PDF survive it, because the rest are laid out in a way the converter
renders as a picture placeholder. The register tables `gr1553.cc` and
`grspw.cc` cite are among the ones lost, which is why those files once looked
unspecified.

They are all present in `gr740-users-manual.pdf`. Read the page below with the
`Read` tool's `pages` parameter, e.g. `pages: "254"` for table 292.

| Table | PDF page | Caption |
|---|---|---|
| 1 | 10 | Change record |
| 2 | 16 | Acronyms |
| 3 | 18 | <Address> - <Register acronym> - <Register name> |
| 4 | 18 | Reset value definitions |
| 5 | 18 | Field type definitions |
| 6 | 21 | Used IP cores |
| 7 | 22 | AMBA memory map, as seen from processors |
| 8 | 24 | AMBA address range 0xE0000000 - 0xEFFFFFFF on Debug AHB bus |
| 9 | 25 | Interrupt assignments |
| 10 | 26 | Plug & play information for masters on Processor AHB bus |
| 11 | 26 | Plug & play information for slaves on Processor AHB bus |
| 12 | 26 | Plug & play information for masters on Memory AHB bus |
| 13 | 26 | Plug & play information for slaves on Memory AHB bus |
| 14 | 27 | Plug & play information for masters on Debug AHB bus |
| 15 | 27 | Plug & play information for slaves on Debug AHB bus |
| 16 | 27 | Plug & play information for masters on Slave I/O AHB bus |
| 17 | 27 | Plug & play information for slaves on Slave I/O AHB bus |
| 18 | 28 | Bus index information for masters on Master I/O AHB bus |
| 19 | 28 | Bus index information for slaves on Master I/O AHB bus |
| 20 | 28 | Plug & play information for APB slaves connected via the first APB bridge on Slave I/O A |
| 21 | 29 | Plug & play information for APB slaves connected via the second APB bridge on Slave I/O  |
| 22 | 29 | Plug & play information for APB slaves connected via APB bridge on Debug AHB bus |
| 23 | 12 | 7.3 |
| 24 | 33 | Multiplexed PROM/IO interface pins with alternative functions and control register bit p |
| 25 | 33 | Shared GPIO interface pins with slow interfaces |
| 26 | 34 | Selection between SDRAM, PCI and Ethernet 1 |
| 27 | 34 | Multiplexed SDRAM interface pins with PCI or Ethernet interfaces |
| 28 | 35 | All external signals, before pin sharing |
| 29 | 41 | Clock inputs |
| 30 | 44 | Supported SYSPLL configurations |
| 31 | 44 | Supported MEMPLL configurations |
| 32 | 44 | Supported SPWPLL configurations |
| 33 | 46 | Devices with gatable clock |
| 34 | 52 | Events that can invert GPIO output |
| 35 | 56 | Instruction timing |
| 36 | 57 | Event timing |
| 37 | 59 | Trap allocation and priority |
| 38 | 60 | Processor reset values |
| 39 | 62 | LEON4 Data caching behavior |
| 40 | 65 | LEON4 MMU Fault Status Register, fault type values |
| 41 | 66 | GRFPU instruction timing with GRFPC |
| 42 | 66 | HPROT values |
| 43 | 69 | ASI usage |
| 44 | 71 | ASI 2 (system registers) address map |
| 45 | 73 | MMU registers (ASI = 0x19) |
| 46 | 74 | %psr- Processor state register |
| 47 | 74 | %wim - Window Invalid Mask |
| 48 | 74 | %tbr - Trap Base Register |
| 49 | 75 | %asr16 - LEON4FT register file protection register |
| 50 | 76 | %asr17 - LEON4 configuration register |
| 51 | 77 | %asr22 - LEON4 Up-counter MSbs |
| 52 | 77 | %asr23 - LEON4 Up-counter LSbs |
| 53 | 78 | %asr24, %asr26, %asr28, %asr30 - Watchpoint address register(s) |
| 54 | 78 | %asr25, %asr27, %asr29, %asr31 - Watchpoint mask register(s) |
| 55 | 79 | ASI 0x2, 0x00 - CCR - Cache control register |
| 56 | 80 | ASI 0x2, 0x08 and 0x0C - CCFG - Cache configuration registers |
| 57 | 81 | ASI 0x19, 0x00- MMUCTRL - MMU control register |
| 58 | 82 | ASI 0x19, offset 0x100 - MMUCTXP - MMU context pointer register |
| 59 | 82 | ASI 0x19, offset 0x200 - MMUCTX - MMU context register |
| 60 | 83 | ASI 0x19, offset 0x300 - FSR - MMU Fault Status Register |
| 61 | 83 | ASI 0x19, offset 0x400 - FAR - MMU Fault Address Register |
| 62 | 88 | : GRFPU operations |
| 63 | 89 | : Throughput and latency |
| 64 | 90 | : Operations on NaNs |
| 65 | 92 | Access cachability using HPROT. |
| 66 | 92 | L2C Cache tag entry |
| 67 | 93 | Cache action on detected EDAC error |
| 68 | 97 | L2C: AHB registers |
| 69 | 98 | 0x00 - L2CC - L2C Control register |
| 70 | 98 | 0x04 - L2CS - L2C Status register |
| 71 | 99 | 0x08 - L2CFMA - L2C Flush (Memory address) register |
| 72 | 99 | 0x0C - L2CFSI - L2C Flush (Set, Index) register |
| 73 | 100 | 0x20 - L2CERR - L2CError status/control register |
| 74 | 101 | 0x24 - L2CERRA - L2C Error address register |
| 75 | 101 | 0x28 - L2CTCB - L2C TAG-Check-Bits register |
| 76 | 101 | 0x2C - L2CCB - L2C Data-Check-Bits register |
| 77 | 101 | 0x30 - L2CSCRUB - L2C Scrub control/status register |
| 78 | 102 | 0x34 - L2CSDEL - L2C Scrub delay register |
| 79 | 102 | 0x38 - L2CEINJ - L2C Error injection register |
| 80 | 103 | 0x3C - L2CACCC - L2C Access control register |
| 81 | 104 | 0x50 - L2CEINJCFG - L2C injection configuration register |
| 82 | 104 | 0x80-FC - L2CMTRR - L2C Memory type range register |
| 83 | 106 | SDRAM programmable minimum timing parameters |
| 84 | 107 | SDRAM example programming |
| 85 | 107 | Mapping of chip selects to I/Os |
| 86 | 106 | The SDRAM controller |
| 87 | 109 | Length-4 read access command sequence |
| 88 | 109 | Length-4 write access command sequence |
| 89 | 110 | Mode Ax4 interleaving pattern (64-bit data width) |
| 90 | 110 | Mode Bx2 interleaving pattern (64-bit data width) |
| 91 | 110 | Mode Ax4 interleaving pattern (32-bit data width) |
| 92 | 110 | Mode Bx2 interleaving pattern (32-bit data width) |
| 93 | 112 | DATAMUX configurations |
| 94 | 113 | MMCTRL Registers |
| 95 | 114 | 0x00 - SDCFG1 - SDRAM configuration register 1 |
| 96 | 115 | 0x04 - SDCFG2 - SDRAM configuration register 2 |
| 97 | 116 | 0x20 - MUXCFG - Mux configuration register |
| 98 | 116 | 0x24 - FTDA - FT diagnostic address register |
| 99 | 117 | 0x28 - FTDC - FT diagnostic checkbits register |
| 100 | 117 | 0x2C - FTDD - FT diagnostic data register |
| 101 | 117 | 0x30 - FTBND - FT boundary address register |
| 102 | 121 | Memory scrubber registers |
| 103 | 122 | 0x00 - AHBS - AHB Status register |
| 104 | 122 | 0x04 - AHBFAR - AHB Failing Address Register |
| 105 | 123 | 0x08 - AHBERC - AHB Error configuration register |
| 106 | 123 | 0x10 - STAT - Status register |
| 107 | 124 | 0x14 - CONFIG - Configuration register |
| 108 | 124 | 0x18 - RANGEL - Range low address register |
| 109 | 124 | 0x1C - RANGEH - Range high address register |
| 110 | 125 | 0x20 - POS - Position register |
| 111 | 125 | 0x24 - ETHRES - Error threshold register |
| 112 | 125 | 0x28 - INIT - Initialisation data register |
| 113 | 126 | 0x2C - RANGEL2 - Second range low address register |
| 114 | 126 | 0x30 - RANGEH2 - Second range high address register |
| 115 | 128 | Read and write combining |
| 116 | 129 | Example of single read |
| 117 | 130 | Access latencies |
| 118 | 131 | Access protection check latencies |
| 119 | 131 | Bit vector size vs. page size |
| 120 | 132 | Cache line size vs. physical address bits |
| 121 | 132 | Set address/ TAG arrangement |
| 122 | 133 | Group set addressing: Set address/TAG arrangement |
| 123 | 134 | Effects of IOMMU Translation Range setting |
| 124 | 134 | IOMMU Page Table Entry (IOPTE) |
| 125 | 135 | TLB entry size, page size |
| 126 | 136 | Set address/TAG arrengement |
| 127 | 136 | Group set address: Set address bits < (group ID bits) + (Physical address bits) |
| 128 | 137 | IOMMU Statistics |
| 129 | 138 | GRIOMMU registers |
| 130 | 139 | 0x00 - CAP0 - Capability register 0 |
| 131 | 140 | 0x04 - CAP1 - Capability register 1 |
| 132 | 140 | 0x08 - CAP2 - Capability register 2 |
| 133 | 141 | 0x10 - CTRL - Control register |
| 134 | 142 | 0x14 - FLUSH - TLB/cache flush register |
| 135 | 143 | 0x18 - STATUS - Status register |
| 136 | 143 | 0x1c - IMASK - Interrupt mask register |
| 137 | 144 | 0x20 - AHBFAS - AHB failing access register |
| 138 | 144 | 0x40 - 0x64 - MSTCFG0-10 - Master configuration register 0 - 10 |
| 139 | 145 | 0x80 - 0x9C - GRPCTRL - Group control register 0 - 7 |
| 140 | 145 | 0xC0 - DIAGCTRL - Diagnostic cache access register |
| 141 | 146 | 0xC4 - 0xE0 - DIAGD - Diagnostic cache access data register 0 - 7 |
| 142 | 146 | 0xE4 - DIAGT - Diagnostic cache access tag register |
| 143 | 146 | 0xE8 - DERRI - Data RAM error injection register |
| 144 | 146 | 0xEC - TERRI - Tag RAM error injection register |
| 145 | 147 | 0x100 - 0x10C - ASMPCTRL - ASMP access control registers 0 - 3 |
| 146 | 164 | RXDMA receive descriptor word 0 (address offset 0x0) |
| 147 | 165 | RXDMA receive descriptor word 1 (address offset 0x4) |
| 148 | 167 | TXDMA transmit descriptor word 0 (address offset 0x0) |
| 149 | 168 | TXDMA transmit descriptor word 1 (address offset 0x4) |
| 150 | 168 | TXDMA transmit descriptor word 2 (address offset 0x8) |
| 151 | 168 | TXDMA transmit descriptor word 3(address offset 0xC) |
| 152 | 170 | The order of error detection in case of multiple errors. The error detected first has nu |
| 153 | 172 | AMBA port hardware RMAP handling of different packet type and command fields. |
| 154 | 175 | AMBA port registers |
| 155 | 176 | 0x00 - RTR.AMBACTRL - AMBA port Control |
| 156 | 177 | 0x04 - RTR.AMBASTS - AMBA port Status |
| 157 | 177 | 0x08 - RTR.AMBADEFADDR - AMBA port Default address |
| 158 | 177 | 0x10 - RTR.AMBADKEY - AMBA port Destination key |
| 159 | 178 | 0x14 - RTR.AMBATC - AMBA port Time-code |
| 160 | 179 | 0x20,0x40,0x60,0x80 - RTR.AMBADMACTRL - AMBA port DMA control/status |
| 161 | 180 | 0x24,0x44,0x64,0x84 - RTR.AMBADMAMAXLEN - AMBA port DMA RX maximum length |
| 162 | 181 | 0x28,0x48,0x68,0x88 - RTR.AMBADMATXDESC - AMBA port DMA transmit descriptor table addres |
| 163 | 181 | 0x2C,0x4C,0x6C,0x8C - RTR.AMBADMARXDESC - AMBA port DMA receive descriptor table address |
| 164 | 181 | 0x30,0x50,0x70,0x90 - RTR.AMBADMAADDR - AMBA port DMA address |
| 165 | 181 | 0xA0 - RTR.AMBAINTCTRL - AMBA port Distributed interrupt control |
| 166 | 182 | 0xA4 - RTR.AMBAINTRX - AMBA port Interrupt receive |
| 167 | 182 | 0xA8 - RTR.AMBAACKRX - AMBA port Interrupt acknowledgement / extended interrupt receive |
| 168 | 183 | 0xAC - RTR.AMBAINTTO0 - AMBA port Interrupt timeout, interrupt 0-31 |
| 169 | 183 | 0xAC - RTR.AMBAINTTO1 - AMBA port Interrupt timeout, interrupt 32-63 |
| 170 | 183 | 0xB0 - RTR.AMBAINTMSK0 - AMBA port Interrupt mask, interrupt 0-31 |
| 171 | 184 | 0xB0 - RTR.AMBAINTMSK1 - AMBA port Interrupt mask, interrupt 32-63 |
| 172 | 185 | RMAP command decoding and handling. |
| 173 | 186 | RMAP target error detection order |
| 174 | 188 | GRSPWROUTER registers |
| 175 | 189 | 0x00000004-0x00000030, 0x00000080-0x000003FC - RTR.RTPMAP - Routing table port mapping,  |
| 176 | 190 | 0x00000404-0x00000430, 0x00000480-0x000007FC - RTR.RTACTRL - Routing table address contr |
| 177 | 191 | 0x00000800 - RTR.PCTRLCFG - Port control, port 0 (configuration port) |
| 178 | 191 | 0x00000804-0x00000830 - RTR.PCTRL - Port control, ports 1-12 (SpaceWire ports and AMBA p |
| 179 | 193 | 0x00000880 - RTR.PSTSCFG - Port status, port 0 (configuration port) |
| 180 | 194 | 0x00000884-0x000008B0 - RTR.PSTS - Port status, ports 1-12 (SpaceWire ports and AMBA por |
| 181 | 195 | 0x00000900-0x00000930 - RTR.PTIMER - Port timer reload, ports 0-12 |
| 182 | 195 | 0x00000980 - RTR.PCTRL2CFG - Port control 2, port 0 (configuration port) |
| 183 | 196 | 0x00000984-0x00000930 - RTR.PCTRL2 - Port control 2, ports 1-12 (SpaceWire ports and AMB |
| 184 | 196 | 0x00000A00 - RTR.RTRCFG - Router configuration / status |
| 185 | 197 | 0x00000A04 - RTR.TC - Time-code |
| 186 | 198 | 0x00000A08 - RTR.VER - Version / instance ID |
| 187 | 198 | 0x00000A0C - RTR.IDIV - Initialization divisor |
| 188 | 198 | 0x00000A10 - RTR.CFGWE - Configuration port write enable |
| 189 | 198 | 0x00000A14 - RTR.PRESCALER - Timer prescaler reload |
| 190 | 199 | 0x00000A18 - RTR.IMASK - Interrupt mask |
| 191 | 199 | 0x00000A1C - RTR.IPMASK - Interrupt port mask |
| 192 | 199 | 0x00000A20 - RTR.PIP - Port interrupt pending |
| 193 | 200 | 0x00000A24 - RTR.ICODEGEN - Interrupt code generation |
| 194 | 200 | 0x00000A28 - RTR.ISR0 - Interrupt code distribution ISR register, interrupt 0-31 |
| 195 | 201 | 0x00000A2C - RTR.ISR1 - Interrupt code distribution ISR register, interrupt 32-63 |
| 196 | 201 | 0x00000A30 - RTR.ISRTIMER - Interrupt code distribution ISR timer reload |
| 197 | 201 | 0x00000A34 - RTR.AITIMER - Interrupt code distribution ACK-to-INT timer reload |
| 198 | 201 | 0x00000A38 - RTR.ISRCTIMER - Interrupt code distribution ISR change timer reload |
| 199 | 202 | 0x00000A40 - RTR.LRUNSTAT - Link running status |
| 200 | 202 | 0x00000A44 - RTR.CAP - Capability |
| 201 | 203 | 0x00000A50 - RTR.PNPVEND - SpaceWire Plug-and-Play - Device Vendor and Product ID |
| 202 | 203 | 0x00000A54 - RTR.PNPUVEND - SpaceWire Plug-and-Play - Unit Vendor and Product ID |
| 203 | 203 | 0x00000A58 - RTR.PNPUSN - SpaceWire Plug-and-Play - Unit Serial Number |
| 204 | 203 | 0x00000E00-0x00000E30 - RTR.MAXPLEN - Maximum packet length, ports 0-12 |
| 205 | 204 | 0x00000E84-0x00000EA0 - RTR.CREDCNT - Credit counter, ports 1-8 |
| 206 | 204 | 0x00001004-0x000013FC - RTR.RTCOMB - Routing table, combined port mapping and address co |
| 207 | 205 | SpaceWire Plug-and-Play address encoding |
| 208 | 205 | SpaceWire Plug-and-Play status codes |
| 209 | 206 | SpaceWire Plug-and-Play support |
| 210 | 206 | 0x00000000 - RTR.PNPVEND - SpaceWire Plug-and-Play - Device Vendor and Product ID |
| 211 | 207 | 0x00000001 - RTR.PNPVER - SpaceWire Plug-and-Play - Version |
| 212 | 207 | 0x00000002 - RTR.PNPDEVSTS - SpaceWire Plug-and-Play - Device Status |
| 213 | 207 | 0x00000003 - RTR.PNPACTLNK - SpaceWire Plug-and-Play - Active Links |
| 214 | 208 | 0x00000004 - RTR.PNPLNKINFO -SpaceWire Plug-and-Play - Link Information |
| 215 | 208 | 0x00000005 - RTR.PNPOA0 - SpaceWire Plug-and-Play - Owner Address 0 |
| 216 | 208 | 0x00000006 - RTR.PNPOA1 - SpaceWire Plug-and-Play - Owner Address 1 |
| 217 | 208 | 0x00000007 - RTR.PNPOA2 - SpaceWire Plug-and-Play - Owner Address 2 |
| 218 | 209 | 0x00000008 - RTR.PNPDEVID - SpaceWire Plug-and-Play - Device ID |
| 219 | 209 | 0x00000009 - RTR.PNPUVEND - SpaceWire Plug-and-Play - Unit Vendor and Product ID |
| 220 | 209 | 0x0000000A - RTR.PNPUSN - SpaceWire Plug-and-Play - Unit Serial Number |
| 221 | 209 | 0x00004000 - RTR.PNPVSTRL - SpaceWire Plug-and-Play - Vendor String Length |
| 222 | 210 | 0x00006000 - RTR.PNPPSTRL - SpaceWire Plug-and-Play - Product String Length |
| 223 | 210 | 0x00008000 - RTR.PNPPCNT - SpaceWire Plug-and-Play - Protocol Count |
| 224 | 210 | 0x0000C000 - RTR.PNPACNT - SpaceWire Plug-and-Play - Application Count |
| 225 | 212 | Address offset 0x0 - GRETH_GBIT transmit descriptor word 0 |
| 226 | 213 | Address offset 0x4 - GRETH_GBIT transmit descriptor word 1 |
| 227 | 215 | Address offset 0x0 - GRETH_GBIT receive descriptor word 0 |
| 228 | 215 | Address offset 0x4 - GRETH_GBIT receive descriptor word 1 |
| 229 | 218 | The IP packet expected by the EDCL. |
| 230 | 218 | The EDCL application layer fields in received frames. |
| 231 | 218 | The EDCL application layer fields in transmitted frames. |
| 232 | 219 | EDCL addresses |
| 233 | 219 | EDCL buffer size limitations |
| 234 | 220 | Signals in GMII and MII. |
| 235 | 220 | GRETH_GBIT registers |
| 236 | 221 | 0x0 - GRETH_GBIT control register |
| 237 | 222 | 0x4 - GRETH_GBIT status register. |
| 238 | 222 | 0x8 - GRETH_GBIT MAC address MSB. |
| 239 | 222 | 0xC - GRETH_GBIT MAC address LSB. |
| 240 | 223 | 0x10 - GRETH_GBIT MDIO control/status register. |
| 241 | 223 | 0x14 - GRETH_GBIT transmitter descriptor table base address register. |
| 242 | 223 | 0x18 - GRETH_GBIT receiver descriptor table base address register. |
| 243 | 224 | 0x1C - GRETH_GBIT EDCL IP register |
| 244 | 224 | 0x20 - GRETH_GBIT Hash table msb register |
| 245 | 224 | 0x24 - GRETH_GBIT Hash table lsb register |
| 246 | 224 | 0x28 - GRETH_GBIT EDCL MAC address MSB. |
| 247 | 224 | 0x2C - GRETH_GBIT EDCL MAC address LSB. |
| 248 | 226 | GRPCI2: Implemented registers in the PCI Configuration Space Header |
| 249 | 226 | 0x00 - Device ID and Vendor ID register |
| 250 | 227 | 0x04 - Status and Command register |
| 251 | 228 | 0x08 - Class Code and Revision ID register |
| 252 | 228 | 0x0C - BIST, Header Type, Latency Timer, and Cache Line Size register |
| 253 | 228 | 0x10-0x24 - Base Address Registers |
| 254 | 229 | 0x34 - Capabilities Pointer Register |
| 255 | 229 | 0x3C - Max_Lat, Min_Gnt, Interrupt Pin and Interrupt Line register |
| 256 | 230 | GRPCI2: Internal capabilities of the Extended PCI Configuration Space |
| 257 | 230 | 0x00 - Length, Next pointer and ID |
| 258 | 230 | 0x04-0x18 - PCI BAR to AHB address mapping register |
| 259 | 230 | 0x1C - Extended PCI Configuration Space to AHB address mapping register |
| 260 | 231 | 0x20 - AHB IO base address and PCI bus config (endianess register) |
| 261 | 231 | 0x24-0x38 - PCI BAR size and prefetch register |
| 262 | 231 | 0x3C - AHB master burst limit |
| 263 | 232 | AHB address/size <=> PCI byte enable combinations. |
| 264 | 234 | GRPCI2 Mapping of AHB I/O address to PCI configuration cycle, type 0 |
| 265 | 235 | GRPCI2 Mapping of AHB I/O address to PCI configuration cycle, type 1 |
| 266 | 237 | GRPCI2: DMA channel descriptor structure |
| 267 | 237 | GRPCI2 DMA channel control |
| 268 | 237 | GRPCI2: DMA data descriptor structure |
| 269 | 238 | GRPCI2 DMA data control |
| 270 | 239 | GRPCI2 PCI control signal trace (32-bit word) |
| 271 | 240 | GRPCI2: APB registers |
| 272 | 241 | 0x00 - CTRL - Control register |
| 273 | 242 | 0x04 - STATCAP - Status and Capability register |
| 274 | 243 | 0x08 - BCIM - PCI master prefetch burst limit |
| 275 | 243 | 0x0C - AHB2PCI - AHB to PCI mapping for PCI IO |
| 276 | 243 | 0x10 - DMACTRL - DMA control and status register |
| 277 | 244 | 0x14 - DMABASE - DMA descriptor base address register |
| 278 | 244 | 0x18 - DMACHAN - DMA channel active register |
| 279 | 244 | 0x20-0x34 - PCI2AHB - PCI BAR to AHB address mapping register |
| 280 | 244 | 0x40-0x7C - AHBM2PCI - AHB master to PCI memory address mapping register |
| 281 | 245 | 0x80 - TCTRC - PCI trace Control and Status register |
| 282 | 245 | 0x84 - TMODE - PCI trace counter and mode register |
| 283 | 245 | 0x88 - TADP - PCI trace AD pattern register |
| 284 | 246 | 0x8C - TADM - PCI trace AD mask register |
| 285 | 246 | 0x90 - TCP - PCI trace Ctrl signal pattern register |
| 286 | 246 | 0x94 - TCM - PCI trace Ctrl signal mask register |
| 287 | 247 | 0x98 - TADS - PCI trace PCI AD state register |
| 288 | 247 | 0x9C - TCS - PCI trace PCI Ctrl signal state register |
| 289 | 252 | GR1553B transfer descriptor format |
| 290 | 253 | GR1553B BC transfer descriptor word 0 (offset 0x00) |
| 291 | 253 | GR1553B BC transfer descriptor word 1 (offset 0x04) |
| 292 | 254 | GR1553B transfer descriptor result word (offset 0x0C) |
| 293 | 254 | GR1553B BC Transfer configuration bits for different transfer types |
| 294 | 255 | GR1553B branch condition word (offset 0x00) |
| 295 | 257 | RT Mode Codes |
| 296 | 258 | GR1553B RT Event Log entry format |
| 297 | 258 | GR1553B RT Subaddress table entry for subaddress number N, 0<N<31 |
| 298 | 258 | GR1553B RT Subaddress table control word (offset 0x00) |
| 299 | 259 | GR1553B RT Descriptor format |
| 300 | 259 | GR1553B RT Descriptor control/status word (offset 0x00) |
| 301 | 260 | GR1553B BM Log entry word 0 (offset 0x00) |
| 302 | 260 | GR1553B BM Log entry word 1 (offset 0x04) |
| 303 | 261 | MIL-STD-1553B interface registers |
| 304 | 261 | MIL-STD-1553B interface BC-specific registers |
| 305 | 262 | MIL-STD-1553B interface RT-specific registers |
| 306 | 262 | MIL-STD-1553B interface BM-specific registers |
| 307 | 262 | 0x00 - IRQ - GR1553B IRQ Register |
| 308 | 263 | 0x04 - IRQE - GR1553B IRQ Enable Register |
| 309 | 263 | 0x10 - HC - GR1553B Hardware Configuration Register |
| 310 | 263 | 0x40 - BCSC - GR1553B BC Status and Config Register |
| 311 | 264 | 0x44 - BCA - GR1553B BC Action Register |
| 312 | 264 | 0x48 - BCTNP - GR1553B BC Transfer list next pointer register |
| 313 | 264 | 0x4C - BCANP- GR1553B BC Asynchronous list next pointer register |
| 314 | 264 | 0x50 - BCT - GR1553B BC Timer register |
| 315 | 264 | 0x58 - BCRP - GR1553B BC Transfer-triggered IRQ ring position register |
| 316 | 265 | 0x5C - BCBS - GR1553B BC per-RT Bus swap register |
| 317 | 265 | 0x68 - BCTCP - GR1553B BC Transfer list current slot pointer |
| 318 | 265 | 0x6C - BCACP - GR1553B BC Asynchronous list current slot pointer |
| 319 | 265 | 0x80 - RTS - GR1553B RT Status register |
| 320 | 266 | 0x84 - RTC - GR1553B RT Config register |
| 321 | 266 | 0x88 - RTBS - GR1553B RT Bus status register |
| 322 | 266 | 0x8C - RTSW - GR1553B RT Status words register |
| 323 | 266 | 0x90 - RTSY - GR1553B RT Sync register |
| 324 | 267 | 0x94 - RTSTBA - GR1553B RT Subaddress table base address register |
| 325 | 267 | 0x98 - RTMCC- GR1553B RT Mode code control register |
| 326 | 267 | 0xA4 - RTTTC - GR1553B RT Time tag control register |
| 327 | 267 | 0xAC - RTELM - GR1553B RT Event log size mask register |
| 328 | 268 | 0xB0 - RTELP - GR1553B RT Event log position register |
| 329 | 268 | 0xB4 - RTELIP - GR1553B RT Event Log interrupt position register |
| 330 | 268 | 0xC0 - BMS - GR1553B BM Status register |
| 331 | 268 | 0xC4- BMC - GR1553B BM Control register |
| 332 | 268 | 0xC8 - BMRTAF - GR1553B BM RT Address filter register |
| 333 | 269 | 0xCC - BMRTSF - GR1553B BM RT Subaddress filter register |
| 334 | 269 | 0xD0 - BMRTMC - GR1553B BM RT Mode code filter register |
| 335 | 269 | 0xD4 -BMLBS - GR1553B BM Log buffer start |
| 336 | 270 | 0xD8 - BMLBE - GR1553B BM Log buffer end |
| 337 | 270 | 0xDC - BMLBP - GR1553B BM Log buffer position |
| 338 | 270 | 0xE0 - BMTTC - GR1553B BM Time tag control register |
| 339 | 279 | GRCAN registers |
| 340 | 280 | 0x000 - CanCONF - Configuration Register |
| 341 | 281 | 0x004 - CanSTAT - Status Register |
| 342 | 281 | 0x008 - CanCTRL - Control Register |
| 343 | 282 | 0x018- CanMASK - SYNC Mask Filter Register |
| 344 | 282 | 0x01C- CanCODE - SYNC Code Filter Register |
| 345 | 282 | 0x200 - CanTxCTRL - Transmit Channel Control Register |
| 346 | 283 | 0x204 - CanTxADDR - Transmit Channel Address Register |
| 347 | 283 | 0x208 - CanTxSIZE - Transmit Channel Size Register |
| 348 | 283 | 0x20C - CanTxWR - Transmit Channel Write Register |
| 349 | 284 | 0x210- CanTxRD - Transmit Channel Read Register |
| 350 | 284 | 0x214 - CanTxIRQ - Transmit Channel Interrupt Register |
| 351 | 284 | 0x300 - CanRxCTRL - Receive Channel Control Register |
| 352 | 285 | 0x304 - CanRxADDR - Receive Channel Address Register |
| 353 | 285 | 0x308 - CanRxSIZE - Receive Channel Size Register |
| 354 | 285 | 0x30C - CanRxWR - Receive Channel Write Register |
| 355 | 286 | 0x310 - CanRxRD - Receive Channel Read Register |
| 356 | 286 | 0x314 - CanRxIRQ - Receive Channel Interrupt Register |
| 357 | 286 | 0x318 - CanRxMASK - Receive Channel Mask Register |
| 358 | 286 | 0x31C - CanRxCODE - Receive Channel Code Register |
| 359 | 287 | Interrupt Registers |
| 360 | 289 | CAN message representation in memory. |
| 361 | 291 | Read and write combining |
| 362 | 292 | Example of single read |
| 363 | 293 | Access latencies |
| 364 | 301 | FTMCTRL memory controller registers |
| 365 | 302 | Memory configuration register 1 |
| 366 | 302 | Memory configuration register 3 |
| 367 | 303 | Memory configuration register 5 |
| 368 | 303 | Memory configuration register 7 |
| 369 | 306 | General Purpose Timer Unit registers |
| 370 | 306 | 0x00 - SCALER - Scaler value register |
| 371 | 307 | 0x04 - SRELOAD- Scaler reload value register |
| 372 | 307 | 0x08 - CONFIG- Configuration register |
| 373 | 307 | 0x0C - LATCHCFG - Timer latch configuration register |
| 374 | 308 | 0xn0 where n selects the timer - TCNTVALn - Timer n counter value register |
| 375 | 308 | 0xn4 where n selects the timer - TRLDVALn - Timer n counter reload value register |
| 376 | 308 | 0xn8 where n selects the timer - TCTRLn - Timer n control register |
| 377 | 309 | 0xnC where n selects the timer - TLATCHn - Timer n latch register |
| 378 | 315 | Interrupt Controller registers |
| 379 | 316 | 0x000 - ILEVEL - Interrupt level register |
| 380 | 316 | 0x004 - IPEND - Interrupt pending register |
| 381 | 317 | 0x008 - IFORCE0 - Interrupt force register for processor 0 |
| 382 | 317 | 0x00C - ICLEAR - Interrupt clear register |
| 383 | 318 | 0x010 - MPSTAT - Multiprocessor status register |
| 384 | 318 | 0x014 - BRDCST - Broadcast register |
| 385 | 318 | 0x018 - ERRSTAT - Error Mode Status Register |
| 386 | 319 | 0x01C - WDOGCTRL - Watchdog control register |
| 387 | 319 | 0x020 - ASMPCTRL - Asymmetric multiprocessing control register |
| 388 | 319 | 0x024 - ICSELR - Interrupt controller select register |
| 389 | 320 | 0x040, 0x044, 0x048, 0x04C - PIMASK0-3 - Processor 0, 1, 2, 3 interrupt mask register |
| 390 | 320 | 0x080, 0x084, 0x088, 0x08C - PIFORCE0-3 - Processor 0, 1, 2, 3 interrupt force register |
| 391 | 320 | 0x0C0, 0x0C4, 0x0C8, 0x0CC - PEXTACK0-3 - Processor 0, 1, 2, 3 extended interrupt acknow |
| 392 | 320 | 0x100, 0x110 - ITCNT - Interrupt timestamp counter register |
| 393 | 321 | 0x1n4 - ITSTMPCn - Interrupt timestamp n control register |
| 394 | 321 | 0x1n8 - ITSTMPASn - Interrupt Assertion Timestamp n register |
| 395 | 322 | 0x1nC - ITSTMPACn - Interrupt Acknowledge Timestamp n register |
| 396 | 322 | 0x200 + n*4 - BADDRn - Processor n Boot Address register |
| 397 | 323 | 0x300 + 4*n - IRQMAPn - Interrupt map register n |
| 398 | 325 | General Purpose I/O Port registers |
| 399 | 326 | 0x00 - DATA - I/O port data register |
| 400 | 326 | 0x04 - OUTPUT - I/O port output register |
| 401 | 326 | 0x08 - DIRECTION - I/O port direction register |
| 402 | 327 | 0x0C - IMASK - Interrupt mask register |
| 403 | 327 | 0x10 - IPOL - Interrupt polarity register |
| 404 | 327 | 0x14- IEDGE - Interrupt edge register |
| 405 | 328 | 0x1C- CAP - Capability register |
| 406 | 328 | 0x20+4*n- IRQMAPRn - Interrupt map register n, where n = 0 .. 3 |
| 407 | 328 | 0x40 - IAVAIL - Interrupt available register |
| 408 | 329 | 0x44 - IFLAG - Interrupt flag register |
| 409 | 329 | 0x4C - PULSE - Pulse register |
| 410 | 330 | 0x54-0x7C - LOR/LAND/LXOR - Logical-OR/AND/XOR registers |
| 411 | 334 | UART registers |
| 412 | 335 | UART data register |
| 413 | 335 | UART status register |
| 414 | 336 | UART control register |
| 415 | 336 | UART scaler reload register |
| 416 | 336 | UART FIFO debug register |
| 417 | 340 | SPI controller registers |
| 418 | 341 | 0x00 - CAP - Capability register |
| 419 | 341 | 0x20 - MODE - Mode register |
| 420 | 343 | 0x24 - EVENT - Event register |
| 421 | 344 | 0x28 - MASK - Mask register |
| 422 | 344 | 0x2C - CMD - Command register |
| 423 | 345 | 0x30 - TX - Transmit register |
| 424 | 345 | 0x34 - RX - Receive register |
| 425 | 345 | 0x38 - SLVSEL - Slave select register |
| 426 | 346 | 0x38 - ASLVSEL - Automatic slave select register |
| 427 | 347 | Clocks controlled by CLKGATE unit |
| 428 | 348 | Clock gating unit registers |
| 429 | 349 | 0x00 - UNLOCK - Unlock register |
| 430 | 349 | 0x04 - CLKEN - Clock enable register |
| 431 | 349 | 0x08 - RESET - Reset register |
| 432 | 350 | 0x0c - OVERRIDE - CPU/FPU override register |
| 433 | 351 | Event types and IDs |
| 434 | 354 | L4STAT counter control register |
| 435 | 355 | 0x00-0x3C - CVAL0-15 - Counter 0-15 value register |
| 436 | 356 | 0x80-0xCC - CCTRL0-15 - Counter 0-15 control register |
| 437 | 357 | 0x100-0x13C - CSVAL0-15 - Counter 0-15 max/latch register |
| 438 | 357 | 0x180 - TSTAMP - Timestamp register |
| 439 | 359 | Handling of new events with filtering and AHB status register sets |
| 440 | 359 | AHB Status registers |
| 441 | 359 | 0x00 - AHBS - AHB Status register |
| 442 | 360 | 0x04 - AHBFAR - AHB Failing address register |
| 443 | 361 | General purpose register registers |
| 444 | 362 | 0x00 - BOOTSTRAP - Bootstrap register |
| 445 | 364 | Temperature sensor registers |
| 446 | 364 | 0x00 - CTRL - Control register |
| 447 | 365 | 0x04 - STATUS - Status register |
| 448 | 365 | 0x08 - THRES - Threshold register |
| 449 | 366 | Mapping between register bit and pin function |
| 450 | 367 | Groups for pad drive strength control |
| 451 | 368 | General purpose register registers |
| 452 | 368 | 0x00 - FTMFUNC - FTMCTRL function enable register |
| 453 | 368 | 0x04 - ALTFUNC - Alternative function enable register |
| 454 | 369 | 0x08 - LVDSMCLK - LVDS and memory clock pad enable register |
| 455 | 369 | 0x0C - PLLNEWCFG - PLL new configuration register |
| 456 | 369 | 0x10 - PLLRECFG - PLL reconfigure command register |
| 457 | 370 | 0x14 - PLLCURCFG - PLL current configuration register |
| 458 | 371 | 0x18 - DRVSTR1 - Drive strength configuration register 1 |
| 459 | 371 | 0x1C - DRVSTR2 - Drive strength configuration register 2 |
| 460 | 372 | 0x20 - LOCKDOWN - Configuration lockdown register |
| 461 | 374 | CCSDS Unsegmented Code P-Field definition |
| 462 | 374 | Example CCSDS Unsegmented Code T-Field with 32 bit coarse and 24 bit fine time |
| 463 | 375 | Example values of ETINC and FSINC for corresponding frequencies |
| 464 | 376 | Example Local ET counter with Mapping values |
| 465 | 379 | Input Events on which time stamp occurs. |
| 466 | 380 | Registers |
| 467 | 382 | 0x000 - CONF0 - Configuration 0 |
| 468 | 383 | 0x004 - CONF 1 - Configuration 1 |
| 469 | 384 | 0x008 - CONF 2 - Configuration 2 |
| 470 | 384 | 0x00C - CONF3 - Configuration 3 |
| 471 | 384 | 0x010 - STAT 0 - Status Register 0 |
| 472 | 385 | 0x014 - STAT 1 - Status Register 1 |
| 473 | 385 | 0x020 - CTRL - Control |
| 474 | 385 | 0x024 - CET0 - Command Elapsed Time 0 |
| 475 | 386 | 0x028 - CET1 - Command Elapsed Time 1 |
| 476 | 386 | 0x02C - CET2 - Command Elapsed Time 2 |
| 477 | 386 | 0x030 - CET3 - Command Elapsed Time 3 |
| 478 | 386 | 0x034 - CET4 - Command Elapsed Time 4 |
| 479 | 386 | 0x040 - DPF - Datation Preamble Field |
| 480 | 386 | 0x044 - DET0 - Datation Elapsed Time 0 |
| 481 | 386 | 0x048 - DET1 - Datation Elapsed Time 1 |
| 482 | 387 | 0x060 - TRPFRX - Time-Stamp Preamble Field Rx |
| 483 | 387 | 0x064 - TR0 - Time Stamp Elapsed Time 0 Rx |
| 484 | 387 | 0x068 - TR1 - Time Stamp Elapsed Time 1 Rx |
| 485 | 387 | 0x080 - TTPFTX - Time-Stamp SpaceWire Time-Code and Preamble Field Tx |
| 486 | 387 | 0x084 - TT0 - Time Stamp Elapsed Time 0 Tx |
| 487 | 388 | 0x088 - TT1 - Time Stamp Elapsed Time 1 Tx |
| 488 | 388 | 0x0A0 - LPF- Latency Preamble Field |
| 489 | 388 | 0xA4 - LE0 -Latency Elapsed Time 0 |
| 490 | 388 | 0xA8 - LE1 -Latency Elapsed Time 1 |
| 491 | 389 | 0x0C0 - IE - Interrupt Enable |
| 492 | 389 | 0xC4 - IS -Interrupt Status |
| 493 | 390 | 0xC8 - DC - Delay Count |
| 494 | 390 | 0xCC - DS - Disable Sync |
| 495 | 390 | 0x100 - EDM0 - External Datation 0 Mask |
| 496 | 390 | 0x110 - EDPF0 - External Datation 0 Preamble Field |
| 497 | 391 | 0x114 - ED0ET0 - External Datation 0 Elapsed Time 0 |
| 498 | 391 | 0x118 - ED0ET1 - External Datation 0 Elapsed Time 1 |
| 499 | 393 | Read and write combining |
| 500 | 394 | Example of single read |
| 501 | 394 | Access latencies |
| 502 | 397 | AHB Trace buffer data allocation |
| 503 | 398 | AHB events |
| 504 | 399 | Instruction trace buffer data allocation |
| 505 | 400 | Trace filter operation |
| 506 | 400 | DSU memory map |
| 507 | 402 | 0x000000- CTRL - DSU control register |
| 508 | 403 | 0x000008 - DTTC - DSU time tag counter register |
| 509 | 403 | 0x000020 - BRSS - DSU break and single step register |
| 510 | 403 | 0x000024 - DBGM - DSU debug mode mask register |
| 511 | 404 | 0x400020 - DTR - DSU trap register |
| 512 | 404 | 0x400024 - DASI- DSU ASI diagnostic access register |
| 513 | 405 | 0x000040 -  ATBC - AHB trace buffer control register |
| 514 | 405 | 0x000044 -  ATBI - AHB trace buffer index register |
| 515 | 406 | 0x000048 -  ATBFC - AHB trace buffer filter control register |
| 516 | 406 | 0x00004C -  ATBFM - AHB trace buffer filter mask register |
| 517 | 407 | 0x000050, 0x000058 -  ATBBA - AHB trace buffer break address registers |
| 518 | 407 | 0x000054, 0x00005C -  ATBBM - AHB trace buffer break mask registers |
| 519 | 408 | 0x000070 -  ICNT - Instruction trace count register |
| 520 | 409 | 0x000080 -  AHBWPC - AHB watchpoint control register |
| 521 | 410 | 0x000090 to 0x00009C,  0x0000B0 to 0x0000BC-  AHBWPD0-7 - AHB watchpoint data registers |
| 522 | 410 | 0x0000A0 to 0x0000AC,  0x0000C0 to 0x0000CC-  AHBWPM0-7 - AHB watchpoint mask registers |
| 523 | 411 | 0x110000 -  ITBC0 - Instruction trace buffer control register 0 |
| 524 | 411 | 0x110004 -  ITBC1 - Instruction trace buffer control register 1 |
| 525 | 412 | JTAG debug link Command/Address register |
| 526 | 412 | JTAG debug link Data register |
| 527 | 420 | GRSPW receive descriptor word 0 (address offset 0x0) |
| 528 | 421 | GRSPW receive descriptor word 1 (address offset 0x4) |
| 529 | 423 | GRSPW transmit descriptor word 0 (address offset 0x0) |
| 530 | 424 | GRSPW transmit descriptor word 1 (address offset 0x4) |
| 531 | 424 | GRSPW transmit descriptor word 2 (address offset 0x8) |
| 532 | 424 | GRSPW transmit descriptor word 3(address offset 0xC) |
| 533 | 426 | The order of error detection in case of multiple errors in the GRSPW. The error detected |
| 534 | 428 | GRSPW hardware RMAP handling of different packet type and command fields. |
| 535 | 430 | GRSPW registers |
| 536 | 431 | 0x00 - SPW2.CTRL - Control |
| 537 | 432 | 0x04 - SPW2.STS - Status |
| 538 | 432 | 0x08 - SPW2.DEFADDR - Default address |
| 539 | 432 | 0x0C - SPW2.CLKDIV - Clock divisor |
| 540 | 433 | 0x10 - SPW2.DKEY - Destination key |
| 541 | 433 | 0x14 - SPW2.TC - Time-code |
| 542 | 434 | 0x20 - SPW2.DMACTRL - DMA control/status, channel 1 |
| 543 | 435 | 0x24 - SPW2.DMAMAXLEN - DMA RX maximum length, channel 1 |
| 544 | 435 | 0x28 - SPW2.DMATXDESC - DMA transmitter descriptor table address, channel 1 |
| 545 | 435 | 0x2C - SPW2.DMARXDESC - DMA receiver descriptor table address, channel 1 |
| 546 | 435 | 0x30 - SPW2.DMAADDR - DMA address, channel 1 |
| 547 | 436 | AHB Trace buffer data allocation |
| 548 | 437 | AHB events |
| 549 | 438 | Trace buffer address space |
| 550 | 438 | 0x000000 - CTRL - Trace buffer control register |
| 551 | 439 | 0x000004 - INDEX - Trace buffer index register |
| 552 | 440 | 0x000008 - TIMETAG - Trace buffer time tag register |
| 553 | 440 | 0x00000C - MSFILT - Trace buffer master/slave filter register |
| 554 | 441 | 0x000010, 0x000018 -  TBBA -  Trace buffer break address registers |
| 555 | 441 | 0x000014, 0x00001C -  TBBM - Trace buffer break mask registers |
| 556 | 445 | Absolute maximum ratings |
| 557 | 446 | Recommended operating conditions |
| 558 | 447 | Input and output DC characteristics |
| 559 | 448 | Supply currents |
| 560 | 449 | Recommended operating conditions and characteristics for device acting as cold spare |
| 561 | 451 | Levels and thresholds for AC parameter tests |
| 562 | 453 | Recommended AC operating conditions |
| 563 | 454 | Timing parameters |
| 564 | 454 | Timing parameters |
| 565 | 455 | Timing parameters - SDRAM accesses |
| 566 | 456 | Timing parameters |
| 567 | 456 | Timing parameters |
| 568 | 457 | Timing parameters |
| 569 | 458 | Timing parameters |
| 570 | 459 | Timing parameters |
| 571 | 460 | Timing parameters |
| 572 | 461 | Timing parameters |
| 573 | 462 | Timing parameters |
| 574 | 465 | Timing parameters - PROM and I/O accesses |
| 575 | 465 | Timing parameters |
| 576 | 466 | Timing parameters |
| 577 | 467 | Timing parameters |
| 578 | 468 | Timing parameters |
| 579 | 469 | Timing parameters |
| 580 | 472 | Pin assignment |
| 581 | 487 | CLGA drawing dimensions |
| 582 | 12 | Updated note 1 in Table 581 |
| 583 | 491 | PBGA drawing dimensions 5) 6) 7) |
| 584 | 493 | Temperature limits |
| 585 | 493 | Thermal resistance |
| 587 | 494 | Table 586.Ordering information, available models |
| 588 | 496 | Errata |
