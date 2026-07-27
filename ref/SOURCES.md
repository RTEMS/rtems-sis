# Sources

Reference documentation for the SPARC V8 / LEON / RISC-V ISAs and peripherals
emulated by SIS. Downloaded 2026-07-18. Untracked (see `.gitignore`); local
knowledge base only, not redistributed with the repo.

| File | Source | Version / Date | Primary or mirror |
|---|---|---|---|
| `sparc-v8-architecture-manual.pdf` | https://www.ece.lsu.edu/ee4720/sam.pdf | Revision SAV080SI9308 (1992) | Mirror (SPARC International's own site is defunct) |
| `erc32-tsc691e-integer-unit-manual.pdf` | https://web.archive.org/web/20250401000649/http://microelectronics.esa.int/erc32/chipset/TSC691E-IU-UserManual-RevI-1998-09-23.pdf | Rev I, 1998-09-23 | Mirror (archive.org snapshot; `microelectronics.esa.int` is offline) |
| `erc32-tsc693e-memory-controller-manual.pdf` | https://web.archive.org/web/20250401000826id_/http://microelectronics.esa.int/erc32/chipset/TSC693E-MEC-UserManual-RevD-1997-04-10.pdf | Rev D, 1997-04-10 | Mirror (archive.org snapshot; `microelectronics.esa.int` is offline). Added 2026-07-26: the MEC register map that `erc32.cc` implements is here, not in the TSC691E IU manual |
| `leon2-ip-core-manual.pdf` | https://raw.githubusercontent.com/Galland/LEON2/master/leon2-1.0.30-xst/doc/leon2-1.0.30-xst.pdf | Version 1.0.30, XST Edition, July 2005 | Mirror (Gaisler no longer distributes LEON2-specific docs; superseded by GRLIB) |
| `grlib-ip-core-manual.pdf` | https://download.gaisler.com/products/GRLIB/doc/grip.pdf | Version 2026.2, Jun 2026 | Primary (Frontgrade Gaisler). Converted/indexed only for the chapters covering peripherals SIS models: AHB/APB bridge, SDRAM controller, DSU, L2 cache controller, APB UART, IRQMP, GPTIMER, GRETH |
| `gr740-users-manual.pdf` | https://download.gaisler.com/products/gr740/doc/GR740-UM-DS-2-9.pdf | Version 2.9, Nov 2025 | Primary (Frontgrade Gaisler) |
| `riscv-current-unprivileged-isa-scoped.adoc` | https://github.com/riscv/riscv-isa-manual (`src/unpriv/`) | commit `3adf59955bd35b8e46eae723ca03d2b3b40ac863`, 2026-07-16 | Primary. Concatenated from the current ratified spec's asciidoc source, scoped to base RV32 + Zicsr/Zifencei + M/A/F/D/C (matches what `riscv.c` implements) |
| `riscv-current-privileged-isa-scoped.adoc` | https://github.com/riscv/riscv-isa-manual (`src/priv/`) | commit `3adf59955bd35b8e46eae723ca03d2b3b40ac863`, 2026-07-16 | Primary. Concatenated from the current ratified spec's asciidoc source, scoped to Machine mode only (SIS implements no S-mode/U-mode/hypervisor CSRs) |
| `riscv-unprivileged-isa-v2.0-draft-1.9-era.pdf` | https://www2.eecs.berkeley.edu/Pubs/TechRpts/2014/EECS-2014-54.pdf | Version 2.0, UCB/EECS-2014-54, 2014-05-06 | Primary (UC Berkeley EECS technical report) |
| `riscv-privileged-isa-v1.9-draft.pdf` | https://www2.eecs.berkeley.edu/Pubs/TechRpts/2016/EECS-2016-129.pdf | Version 1.9, UCB/EECS-2016-129, 2016-07-08 | Primary (UC Berkeley EECS technical report). This is the version `sis.texi` says SIS's RISC-V mode targets |
| `sifive-fu540-c000-manual.pdf` | https://static.dev.sifive.com/FU540-C000-v1.0.pdf | Version 1.0, 2018-04 | Primary (SiFive). Added 2026-07-27 for the CLINT and PLIC memory maps `rv32.cc` follows, which no RISC-V specification fixes |
| `pc16550d-uart-datasheet.pdf` | https://www.scs.stanford.edu/10wi-cs140/pintos/specs/pc16550d.pdf | National Semiconductor PC16550D, June 1995 | Mirror (Stanford CS140 course materials; TI's own `lit/ds/symlink/pc16550d.pdf` now 404s and National is long absorbed). Added 2026-07-27 as the only specification for `grlib.cc`'s `ns16550` core |

## Notes

- No document required substituting an older public revision in place of a
  gated latest release; all nine documents above are the newest freely
  available revisions.
- `erc32-tsc693e-memory-controller-manual.*` was added on 2026-07-26, after
  the original 2026-07-18 acquisition pass, because the MEC peripheral
  registers SIS emulates are documented only there.  Its `.md` came from the
  same pymupdf4llm conversion as the rest; the register bit tables survive
  the conversion intact.  The single-chip TSC695F manual
  (https://ww1.microchip.com/downloads/en/DeviceDoc/doc4148.pdf) documents
  the same peripherals for the integrated part and was deliberately left out:
  SIS models the chipset MEC.
- `pc16550d-uart-datasheet.md` converts with mangled punctuation: the 1995
  original's mid-dot separators come through as `�`, so section 8.1 reads
  `8�1` and `TA` ranges are garbled.  The register tables in section 8.0
  survive intact, which is the part SIS needs; read the `.pdf` if a bit
  field looks wrong.
- `grip.pdf` (GRLIB IP core manual) and the two RISC-V `-scoped.adoc` files
  are intentionally not full documents; see the table above and
  `INDEX.md` for what was kept and why.
