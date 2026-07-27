# SIS reference documentation knowledge base

> **Check `/opt/eb-docs` first.** It is an indexed device documentation
> corpus (OKF v0.1) that covers most of what this directory holds, in a far
> better form: per-chapter chunks with page citations, a `REGISTERS.md`
> mapping every register name to a chunk and page, errata, and a
> `whereis.py` search tool. Read `/opt/eb-docs/CLAUDE.md` for how to
> navigate it, and the `README.md` of a directory before its contents.
> `SoC/Gaisler/GR740` carries GR740-UM-DS-2-10, newer than the 2-9 here,
> split into 58 chunks with 286 registers mapped.
>
> It matters most for the two files this directory could not specify.
> `gr1553.cc` and `grspw.cc` cite GR740 register tables which the markdown
> conversion here drops (see `gr740-table-index.md`); the corpus has them as
> readable text in `SoC/Gaisler/GR740/text/datasheet/16-MIL-STD-1553B-*`
> and `13-SpaceWire-router-05-Registers.md`.
>
> Keep using this directory for what the corpus does not carry: the SPARC
> V8 and RISC-V instruction set manuals, the ERC32 chipset, and the two
> documents added for the RV32 board below.

Reference material for SIS's emulated ISAs and peripherals (see
`../CLAUDE.md`). Provenance for every file is in `SOURCES.md`. Each
document has a matching `<name>.toc.md` table of contents.

Downloaded and converted 2026-07-18. Scope: acquisition + conversion +
indexing only — no test suite here yet (planned as a separate follow-up).

## Document -> board/architecture map

| Document | Boards / archs it covers | Role |
|---|---|---|
| `sparc-v8-architecture-manual.{pdf,md,toc.md}` | ERC32, LEON2, LEON3, GR740 (all SPARC V8) | Shared SPARC V8 instruction set, trap model, register windows -- the base ISA reference for every SPARC board `sis` emulates |
| `erc32-tsc691e-integer-unit-manual.{pdf,md,toc.md}` | ERC32 | ERC32-specific integer unit implementation: trap table, cache, interrupt levels, that aren't part of the generic SPARC V8 manual. The IU half of `erc32.cc` |
| `erc32-tsc693e-memory-controller-manual.{pdf,md,toc.md}` | ERC32 | The MEC: every on-chip peripheral `erc32.cc` emulates, with bit-level register tables and reset values -- interrupt controller, RTC and general purpose timers, watchdog, UART, memory configuration and write protection, error and status handling, the `0x01F80000` register map. The peripheral half of `erc32.cc`, and the only source for it |
| `leon2-ip-core-manual.{pdf,md,toc.md}` | LEON2 | LEON2 processor core and its integrated peripherals (UART, timers, interrupt controller, memory controller). Corresponds to `leon2.c`. Referenced in `sis.texi` as "the LEON2 IP manual" |
| `grlib-ip-core-manual-scoped.{md,toc.md}` (+ full `grlib-ip-core-manual.pdf`) | LEON3, GR740 | GRLIB peripheral reference, **scoped** to only the IP cores `leon3.c`/`gr740.c` actually instantiate: APBCTRL (AHB/APB bridge), APBUART, DSU3, GPTIMER, GRETH, IRQMP, L2C, SDCTRL. The full 2449-page PDF covers ~150 IP cores total; the rest aren't modeled by `sis` and were left out (see `SOURCES.md`) |
| `gr740-users-manual.{pdf,md,toc.md}` | GR740/LEON4 | GR740-specific data sheet and user's manual: quad-core LEON4 SoC memory map, SpaceWire/PCI/CAN/Ethernet interfaces, fault-tolerance features. Corresponds to `gr740.c` |
| `riscv-current-unprivileged-isa-scoped.adoc` (+ `.toc.md`) | `-griscv` (GRLIB RISC-V), `-rv32` | Current ratified RISC-V unprivileged ISA, scoped to base RV32I + Zicsr/Zifencei + M/A/F/D/C -- the extensions `riscv.c` implements (RV32IMAFDC) |
| `riscv-current-privileged-isa-scoped.adoc` (+ `.toc.md`) | `-griscv`, `-rv32` | Current ratified RISC-V privileged ISA, scoped to Machine mode only -- `riscv.c` has no S-mode/U-mode/hypervisor CSRs |
| `riscv-unprivileged-isa-v2.0-draft-1.9-era.{pdf,md,toc.md}` | `-griscv`, `-rv32` | Historical unprivileged ISA v2.0 (UCB/EECS-2014-54), paired with the privileged v1.9 draft below. This is the version pairing `sis.texi` cites ("RISC-V specification 1.9") |
| `riscv-privileged-isa-v1.9-draft.{pdf,md,toc.md}` | `-griscv`, `-rv32` | Historical privileged ISA v1.9 draft (UCB/EECS-2016-129) -- what `sis.texi` says SIS's RISC-V mode targets. Compare against the current spec to spot where `riscv.c` may have drifted from either version |
| `sifive-fu540-c000-manual.{pdf,md,toc.md}` | `-rv32` | The SiFive platform conventions the generic RV32 board follows. `rv32.cc` places the CLINT at `0x0200_0000` and the PLIC at `0x0C00_0000`, which is this part's map (table 35), not anything the RISC-V privileged spec fixes. Chapter 9 gives the CLINT register map (table 36: `msip` at offset 0, `mtimecmp` at `0x4000`, `mtime` at `0xbff8`) and chapter 10 the PLIC. Use it for the memory-map layer only; the *semantics* of `mtime`/`mtimecmp`/`msip` belong to the privileged ISA above. **Does not cover the `ns16550` UART** -- the FU540's own UART is a SiFive UART at different offsets, and `grlib.cc`'s core is a 16550 |
| `pc16550d-uart-datasheet.{pdf,md,toc.md}` | `-rv32` | The `ns16550` core in `grlib.cc`. Section 8.0 REGISTERS has the register table and every bit field: IER (`ETBEI` is bit 1), LCR (`DLAB` is bit 7), MCR (`OUT 2` is bit 3, and gates the interrupt output), LSR (master reset value `0110 0000`). SIS maps these at a 4-byte stride, so datasheet register *n* is at offset *4n*. Note the identifier `uart_txctrl` in `grlib.cc` is SiFive naming on what this datasheet calls IIR/FCR |

## Reading order for a new session

1. Identify the board/arch you're working on (`sis -help`, or the `-<board>`
   flag in the invocation you're debugging).
2. Check this table for the relevant document(s).
3. Open the matching `.toc.md` first to find the right section, then read
   that section of the `.md`/`.adoc` file (or the original `.pdf` via the
   `Read` tool's page-range support if the conversion looks lossy for a
   table/diagram you need).
4. `SOURCES.md` has exact version/date for citing a spec section precisely.

## Known limitations

The `.toc.md` files are generated from font-size heuristics during PDF-to-
markdown conversion, not from each document's authoritative outline, so
they're a navigation aid, not a guarantee of completeness:

- `sparc-v8-architecture-manual.toc.md`: a handful of subsection titles in
  the original 1992 PDF (e.g. most of Appendix G's subsections, one entry
  in Appendix C) weren't rendered in a font distinct enough to be picked up
  as headings, even though the section content itself is present in the
  full `.md`. If a TOC lookup comes up short, grep the `.md` directly.
- `grlib-ip-core-manual-scoped.md`: converted as 8 independent page-range
  extracts (one per relevant peripheral chapter), which broke pymupdf4llm's
  per-document heading-size heuristic for the chapter-title lines; those 8
  top-level headings were reinserted by hand from the manual's own PDF
  outline (`grip.pdf`'s bookmarks) rather than detected automatically.
  Two garbled OCR artifacts from stylized chapter-title graphics were
  removed during that same pass.
