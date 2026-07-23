<!-- SPDX-License-Identifier: GFDL-1.3-no-invariants-or-later -->
<!-- SPDX-FileCopyrightText: 2020 Free Software Foundation, Inc. -->

[SIS manual](README.md)

# Networking

## Introduction

SIS supports the emulation of the GRLIB/GRETH 10/100 Mbit network interface,
for leon3 and RISC-V targets. The network interface creates a tun/tap interface
on the host, through which ethernet packets can be sent and received.

The tap device is automatically created when the application enables the GRETH
core. The tap can optionally be connected to a host bridge using -bridge br0 or
similar at invocation. Networking requires SIS to be run as root or with sudo.

Networking is currently only supported on 64-bit linux hosts. On other hosts,
the networking emulation is disabled during compilation.

## Emulation of GRETH

The 10/100 Mbit GRETH interface is emulated accurately and allows execution of
unmodified target applications using the network interface. An ethernet PHY
connected to the GRETH MDIO interface is also emulated and indicates 100 Mbit
connection when accessed.

The ethernet address of the host tap is equal to what the application programs
into the GRETH MAC registers. Care has to be taken so that a valid ethernet
address is chosen or the host can reject the address and a mismatch error will
occur. The ethernet address cannot be changed once it has been set.

DMA operation and interrupt generation operates as defined in the GRETH
specification. There is no support for multi-cast or the EDCL debug support
link.

## Usage

To simplify operation, a bridge should be created on the linux host using brctl
or similar. This will create an isolated environment for network applications.
Installing the lxc package on the host will in most cases automatically create
a bridge called lxcbr0 with subnet 10.0.3.1. The network applications should
then be configured to use an IP on the bridge subnet. Below is an example of
SIS running the ttcp performance application under RTEMS:

    $ sudo ./sis  -riscv ./ttcp.exe -bridge lxcbr0

     SIS - SPARC/RISCV instruction simulator 2.23
     RISCV emulation enabled, 1 cpus online, delta 50 clocks
     Loaded ttcp.exe, entry 0x40000000

    sis> run

    net: using tap0, ether 829991919191, bridge lxcbr0
    greth: driver attached
    **** PHY ****
    Vendor: 885   Device: 11   Revision: 2
    Current Operating Mode: 100 Mbit Full Duplex
    Autonegotiation Time: 0ms
    >>> ttcp -rs
    ttcp-r: buflen=8192, nbuf=2048, align=16384/0, port=5001  tcp
    ttcp-r: socket
    ttcp-r: accept from 10.0.3.1
    ttcp-r: 3012285 bytes in 0.53 real seconds = 5580.46 KB/sec +++
    ttcp-r: 453 I/O calls, msec/call = 1.19, calls/sec = 859.35
    ttcp-r: 0.0user 0.0sys 0:00real 100% 0i+0d 0maxrss 0+0pf 0+0csw
    -----------------------------------------------------------------------
                          CPU USAGE BY THREAD
    ------------+--------------------------------+---------------+---------
     ID         | NAME                           | SECONDS       | PERCENT
    ------------+--------------------------------+---------------+---------
     0x09010001 | IDLE                           |      5.643012 |  91.406
     0x0a010001 | UI1                            |      0.000000 |   0.000
     0x0a010002 | ntwk                           |      0.118958 |   1.926
     0x0a010003 | DCrx                           |      0.337919 |   5.472
     0x0a010004 | TTCP                           |      0.074758 |   1.210
    ------------+--------------------------------+---------------+---------
     TIME SINCE LAST CPU USAGE RESET IN SECONDS:                  6.174651
    -----------------------------------------------------------------------
    ************ MBUF STATISTICS ************
    mbufs:2048    clusters: 128    free:  96
    drops:   0       waits:   0  drains:   0
          free:2015          data:33          header:0           socket:0       
           pcb:0           rtable:0           htable:0           atable:0       
        soname:0           soopts:0           ftable:0           rights:0       
        ifaddr:0          control:0          oobdata:0       
    ************ INTERFACE STATISTICS ************
    ***** lo0 *****
    Address:127.0.0.1       Net mask:255.0.0.0       
    Flags: Up Loopback Running Multicast
    Send queue limit:50   length:0    Dropped:0       
    ***** gr_eth1 *****
    Ethernet Address: 82:99:91:91:91:91
    Address:10.0.3.2  Broadcast Address:10.0.3.255 Net mask:255.255.255.0   
    Flags: Up Broadcast Running Simplex
    Send queue limit:50   length:0    Dropped:0       
    Rx Interrupts:186   Rx Packets:2234  Length:0 Non-octet:0 
    Bad CRC:0  Overrun:0  Tx Interrupts:0  Maximal Frags:1 GBIT MAC:0       
    ************ IP Statistics ************
                 total packets received        2233
     datagrams delivered to upper level        2233
        total ip packets generated here         369
    ************ TCP Statistics ************
                   connections accepted           1
                connections established           1
          conn. closed (includes drops)           1
         segs where we tried to get rtt           2
                     times we succeeded           2
                      delayed acks sent           3
                     total packets sent         369
                  ack-only packets sent           5
        window update-only packets sent         363
     control (SYN|FIN|RST) packets sent           1
                 total packets received        2233
           packets received in sequence        2230
             bytes received in sequence     3012285
                       rcvd ack packets           2
               bytes acked by rcvd acks           2
     times hdr predict ok for data pkts        2228
    *** FATAL ***
    fatal source: 5 (RTEMS_FATAL_SOURCE_EXIT)
    fatal code: 0 (0x00000000)
    RTEMS version: 6.0.0.c1164b650a2754335b15910e6408a9b144aa5162
    RTEMS tools: 10.2.1 20200918 (RTEMS 6, RSB ed5030bc24dbfdfac52074ed78cf4231bf1f353d, Newlib 749cbcc)
    executing thread ID: 0x08a010004
    executing thread name: TTCP
    cpu 0 in error mode (tt = 0x101)
     445299412  4004af30:  00000073   ecall       
    sis> 
