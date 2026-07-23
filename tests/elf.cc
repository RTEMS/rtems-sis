/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for elf.cc, the program loader.

   The loader is fed synthetic ELF files rather than a committed binary, so
   that a case can state exactly which field it is varying and can build the
   malformed files that the error paths need. Layout and field meanings
   follow the System V ABI, and the ELF header is 32-bit big-endian SPARC,
   which is what the loader was written for.

   The entry point and the load address are ERC32 addresses so that the
   section data lands in emulated RAM at 0x2000000.  */

#include "doctest.h"
#include "support.h"

#include "config.h"
#include "sis.h"

#include <elf.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using sis_tests::stdout_capture;

namespace
{

const uint32 ERC32_RAM = 0x2000000;

typedef std::vector<unsigned char> bytes;

void
put16 (bytes &b, size_t off, uint16 value)
{
  b[off] = (unsigned char) (value >> 8);
  b[off + 1] = (unsigned char) value;
}

void
put32 (bytes &b, size_t off, uint32 value)
{
  b[off] = (unsigned char) (value >> 24);
  b[off + 1] = (unsigned char) (value >> 16);
  b[off + 2] = (unsigned char) (value >> 8);
  b[off + 3] = (unsigned char) value;
}

/* Offsets of the pieces a case may want to patch.  */
struct elf_layout
{
  size_t ehdr;
  size_t phdr;
  size_t text_data;
  size_t shstrtab_data;
  size_t shdr;	  /* section header 0, the null entry */
  size_t sh_text; /* section header 1 */
  size_t sh_str;  /* section header 2 */
  uint32 text_size;
};

const size_t EHDR_SIZE = sizeof (Elf32_Ehdr);
const size_t PHDR_SIZE = sizeof (Elf32_Phdr);
const size_t SHDR_SIZE = sizeof (Elf32_Shdr);

/* A minimal but well formed big-endian SPARC executable: one PT_LOAD
   segment, an allocated .text of eight bytes inside it, and the section
   name table.  */
bytes
build_elf (elf_layout *layout, uint16 machine = EM_SPARC,
	   uint32 entry = ERC32_RAM, unsigned char data = ELFDATA2MSB,
	   unsigned char cls = ELFCLASS32)
{
  static const char names[] = "\0.text\0.shstrtab";
  const size_t names_size = sizeof (names);
  const uint32 text_size = 8;

  elf_layout l;
  l.ehdr = 0;
  l.phdr = EHDR_SIZE;
  l.text_data = l.phdr + PHDR_SIZE;
  l.shstrtab_data = l.text_data + text_size;
  l.shdr = l.shstrtab_data + names_size;
  l.sh_text = l.shdr + SHDR_SIZE;
  l.sh_str = l.shdr + 2 * SHDR_SIZE;
  l.text_size = text_size;

  bytes b (l.shdr + 3 * SHDR_SIZE, 0);

  b[EI_MAG0] = ELFMAG0;
  b[EI_MAG1] = ELFMAG1;
  b[EI_MAG2] = ELFMAG2;
  b[EI_MAG3] = ELFMAG3;
  b[EI_CLASS] = cls;
  b[EI_DATA] = data;
  b[EI_VERSION] = EV_CURRENT;

  put16 (b, offsetof (Elf32_Ehdr, e_type), ET_EXEC);
  put16 (b, offsetof (Elf32_Ehdr, e_machine), machine);
  put32 (b, offsetof (Elf32_Ehdr, e_version), EV_CURRENT);
  put32 (b, offsetof (Elf32_Ehdr, e_entry), entry);
  put32 (b, offsetof (Elf32_Ehdr, e_phoff), (uint32) l.phdr);
  put32 (b, offsetof (Elf32_Ehdr, e_shoff), (uint32) l.shdr);
  put16 (b, offsetof (Elf32_Ehdr, e_ehsize), (uint16) EHDR_SIZE);
  put16 (b, offsetof (Elf32_Ehdr, e_phentsize), (uint16) PHDR_SIZE);
  put16 (b, offsetof (Elf32_Ehdr, e_phnum), 1);
  put16 (b, offsetof (Elf32_Ehdr, e_shentsize), (uint16) SHDR_SIZE);
  put16 (b, offsetof (Elf32_Ehdr, e_shnum), 3);
  put16 (b, offsetof (Elf32_Ehdr, e_shstrndx), 2);

  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_type), PT_LOAD);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_offset), (uint32) l.text_data);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_vaddr), ERC32_RAM);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_paddr), ERC32_RAM);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_filesz), text_size);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_memsz), text_size);

  /* Recognisable words, byte swapped by the loader on a little-endian
     host, so the values below are what memory should end up holding.  */
  put32 (b, l.text_data, 0x01020304);
  put32 (b, l.text_data + 4, 0x05060708);

  memcpy (&b[l.shstrtab_data], names, names_size);

  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_name), 1);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_type), SHT_PROGBITS);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_flags),
	 SHF_ALLOC | SHF_EXECINSTR);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_addr), ERC32_RAM);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_offset),
	 (uint32) l.text_data);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_size), text_size);

  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_name), 7);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_type), SHT_STRTAB);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_offset),
	 (uint32) l.shstrtab_data);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_size), (uint32) names_size);

  if (layout != NULL)
    *layout = l;

  return b;
}

/* A file on disk that removes itself, holding whatever bytes a case built.  */
class elf_on_disk
{
public:
  explicit elf_on_disk (const bytes &b) : path ("/tmp/sis-test-elf-XXXXXX")
  {
    int fd = mkstemp (&path[0]);
    REQUIRE (fd >= 0);
    FILE *fp = fdopen (fd, "wb");
    REQUIRE (fp != NULL);
    if (!b.empty ())
      REQUIRE (fwrite (&b[0], b.size (), 1, fp) == 1);
    fclose (fp);
  }

  ~elf_on_disk () { unlink (path.c_str ()); }

  char *
  name ()
  {
    return &path[0];
  }

private:
  std::string path;
};

/* The simulator state elf_load reaches through: a board for the memory
   writes, and the globals the loader records the detected target in.  */
struct elf_fixture
{
  int saved_verbose;
  int saved_cputype;
  int saved_archtype;

  elf_fixture ()
      : saved_verbose (sis_verbose), saved_cputype (cputype),
	saved_archtype (archtype)
  {
    cputype = CPU_ERC32;
    archtype = CPU_SPARC;
    ms = &erc32sys;
    ebase.freq = 14;
    ebase.simtime = 0;
    ebase.simstart = 0;
    reset_all ();
    init_bpt (sregs);
    ms->init_sim ();
    sis_verbose = 0;
  }

  ~elf_fixture ()
  {
    sis_verbose = saved_verbose;
    cputype = saved_cputype;
    archtype = saved_archtype;
  }

  /* Load and swallow the loader's chatter.  */
  int
  load (char *name, int with_body, std::string *text = NULL)
  {
    stdout_capture capture;
    int res = elf_load (name, with_body);
    std::string out = capture.str ();
    if (text != NULL)
      *text = out;
    return res;
  }
};

} /* namespace */

TEST_CASE_FIXTURE (elf_fixture, "elf_load reports a missing file")
{
  std::string text;
  CHECK (load ((char *) "/nonexistent/sis-test.elf", 1, &text) == -1);
  CHECK (text.find ("file not found") != std::string::npos);
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load reads the header without loading")
{
  elf_layout l;
  elf_on_disk file (build_elf (&l));

  CHECK (load (file.name (), 0) == (int) ERC32_RAM);
  CHECK (ebase.arch == CPU_SPARC);
  CHECK (ebase.cpu == CPU_ERC32);
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load loads the allocated sections")
{
  elf_layout l;
  elf_on_disk file (build_elf (&l));

  std::string text;
  REQUIRE (load (file.name (), 1, &text) == (int) ERC32_RAM);
  CHECK (text.find ("Loaded") != std::string::npos);
  CHECK (text.find ("entry 0x02000000") != std::string::npos);

  /* The section data reaches emulated memory in target byte order.  */
  uint32 word = 0;
  ms->sis_memory_read (ERC32_RAM, (char *) &word, 4);
  CHECK (word == 0x01020304);
  ms->sis_memory_read (ERC32_RAM + 4, (char *) &word, 4);
  CHECK (word == 0x05060708);
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load rejects a file that is not ELF")
{
  elf_layout l;

  SUBCASE ("truncated below a header")
  {
    bytes b = build_elf (&l);
    b.resize (8);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }

  SUBCASE ("wrong first magic byte")
  {
    bytes b = build_elf (&l);
    b[EI_MAG0] = 0;
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }

  SUBCASE ("wrong magic string")
  {
    bytes b = build_elf (&l);
    b[EI_MAG2] = 'X';
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load rejects what it cannot run")
{
  elf_layout l;

  SUBCASE ("an unknown machine")
  {
    elf_on_disk file (build_elf (&l, EM_386));
    std::string text;
    CHECK (load (file.name (), 1, &text) == -1);
    CHECK (text.find ("Unknown architecture") != std::string::npos);
  }

  SUBCASE ("a 64-bit class")
  {
    elf_on_disk file (
	build_elf (&l, EM_SPARC, ERC32_RAM, ELFDATA2MSB, ELFCLASS64));
    std::string text;
    CHECK (load (file.name (), 1, &text) == -1);
    CHECK (text.find ("Only 32-bit ELF supported") != std::string::npos);
  }
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load derives the board from the entry")
{
  elf_layout l;

  struct
  {
    uint16 machine;
    uint32 entry;
    int arch;
    int cpu;
  } cases[] = {
    { EM_SPARC, 0, CPU_SPARC, CPU_LEON4 },
    { EM_SPARC, 0x40000000, CPU_SPARC, CPU_LEON3 },
    { EM_SPARC, 0x2000000, CPU_SPARC, CPU_ERC32 },
    { EM_RISCV, 0x40000000, CPU_RISCV, CPU_LEON3 },
    { EM_RISCV, 0x80000000, CPU_RISCV, CPU_RISCV },
  };

  for (auto &c : cases)
    {
      CAPTURE (c.entry);
      elf_on_disk file (build_elf (&l, c.machine, c.entry));
      CHECK (load (file.name (), 0) == (int) c.entry);
      CHECK (ebase.arch == c.arch);
      CHECK (ebase.cpu == c.cpu);
    }
}

TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load keeps the previous board for an unknown entry")
{
  elf_layout l;

  /* Neither architecture claims this entry, so the loader leaves the cpu
     it last recorded rather than guessing.  */
  elf_on_disk sparc (build_elf (&l, EM_SPARC, 0x2000000));
  REQUIRE (load (sparc.name (), 0) == 0x2000000);
  REQUIRE (ebase.cpu == CPU_ERC32);

  elf_on_disk odd (build_elf (&l, EM_SPARC, 0x12345678));
  CHECK (load (odd.name (), 0) == 0x12345678);
  CHECK (ebase.arch == CPU_SPARC);
  CHECK (ebase.cpu == CPU_ERC32);

  elf_on_disk odd_riscv (build_elf (&l, EM_RISCV, 0x12345678));
  CHECK (load (odd_riscv.name (), 0) == 0x12345678);
  CHECK (ebase.arch == CPU_RISCV);
  CHECK (ebase.cpu == CPU_ERC32);
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load reports a truncated body")
{
  elf_layout l;

  SUBCASE ("no section header table")
  {
    bytes b = build_elf (&l);
    b.resize (l.shdr);
    elf_on_disk file (b);
    std::string text;
    CHECK (load (file.name (), 1, &text) == -1);
    CHECK (text.find ("File read error") != std::string::npos);
  }

  SUBCASE ("the name table runs past the end")
  {
    bytes b = build_elf (&l);
    put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_size), 0x10000);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }

  SUBCASE ("a program header runs past the end")
  {
    bytes b = build_elf (&l);
    put32 (b, offsetof (Elf32_Ehdr, e_phoff), (uint32) b.size ());
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }

  SUBCASE ("a section header runs past the end")
  {
    bytes b = build_elf (&l);
    put16 (b, offsetof (Elf32_Ehdr, e_shnum), 64);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }

  SUBCASE ("section contents run past the end")
  {
    bytes b = build_elf (&l);
    put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_size), 0x10000);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == -1);
  }
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load skips sections it must not load")
{
  elf_layout l;

  SUBCASE ("SHT_NOBITS, which is bss")
  {
    bytes b = build_elf (&l);
    put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_type), SHT_NOBITS);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == (int) ERC32_RAM);
  }

  SUBCASE ("an empty section")
  {
    bytes b = build_elf (&l);
    put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_size), 0);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == (int) ERC32_RAM);
  }

  SUBCASE ("a section that is not allocated")
  {
    bytes b = build_elf (&l);
    put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_flags), 0);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == (int) ERC32_RAM);
  }

  SUBCASE ("an allocated section of a type that carries no data")
  {
    /* Allocated and non-empty, so it is considered, but SHT_STRTAB is
       none of the three types the loader copies.  */
    bytes b = build_elf (&l);
    put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_type), SHT_STRTAB);
    elf_on_disk file (b);
    CHECK (load (file.name (), 1) == (int) ERC32_RAM);
  }
}

TEST_CASE_FIXTURE (elf_fixture, "elf_load copies the array section types")
{
  elf_layout l;

  for (uint32 type : { (uint32) SHT_INIT_ARRAY, (uint32) SHT_FINI_ARRAY })
    {
      CAPTURE (type);
      bytes b = build_elf (&l);
      put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_type), type);
      elf_on_disk file (b);

      REQUIRE (load (file.name (), 1) == (int) ERC32_RAM);

      uint32 word = 0;
      ms->sis_memory_read (ERC32_RAM, (char *) &word, 4);
      CHECK (word == 0x01020304);
    }
}

TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load translates virtual to physical addresses")
{
  elf_layout l;
  bytes b = build_elf (&l);

  /* A segment loaded at a physical address below its virtual one, which is
     what a linker script with a separate load address produces.  */
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_vaddr), 0x40000000);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_paddr), ERC32_RAM);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_addr), 0x40000000);

  elf_on_disk file (b);
  REQUIRE (load (file.name (), 1) == (int) ERC32_RAM);

  uint32 word = 0;
  ms->sis_memory_read (ERC32_RAM, (char *) &word, 4);
  CHECK (word == 0x01020304);
}

TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load leaves a section outside every segment alone")
{
  elf_layout l;
  bytes b = build_elf (&l);

  /* The section is allocated but no program header covers it, so the
     search falls out of the loop and the address is used unchanged.  */
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_vaddr), 0x40000000);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_paddr), 0x40000000);

  elf_on_disk file (b);
  REQUIRE (load (file.name (), 1) == (int) ERC32_RAM);

  uint32 word = 0;
  ms->sis_memory_read (ERC32_RAM, (char *) &word, 4);
  CHECK (word == 0x01020304);
}

TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load describes what it loads when verbose")
{
  elf_layout l;
  elf_on_disk file (build_elf (&l));

  sis_verbose = 1;

  std::string text;
  REQUIRE (load (file.name (), 1, &text) == (int) ERC32_RAM);
  CHECK (text.find ("SPARC executable") != std::string::npos);
  CHECK (text.find ("section: .text") != std::string::npos);
}

TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load names the RISCV architecture when verbose")
{
  elf_layout l;
  elf_on_disk file (build_elf (&l, EM_RISCV, 0x80000000));

  sis_verbose = 1;

  std::string text;
  REQUIRE (load (file.name (), 0, &text) == (int) 0x80000000);
  CHECK (text.find ("RISCV executable") != std::string::npos);
}

/* Marked as expected to fail: see BUGS.md, "The ELF byte order is remembered
   across loads". read_elf_header sets efile.bswap for a big-endian file and
   never clears it, and efile is static, so the little-endian file below is
   swapped as though it were big-endian and its header reads as garbage.

   The big-endian load comes first inside the case rather than being left to
   whatever ran before it, so the outcome does not depend on test order.
   Fixing elf.cc makes this pass, and the decorator has to go with it.  */
TEST_CASE_FIXTURE (
    elf_fixture, "elf_load reads a little-endian file after a big-endian one" *
		     doctest::should_fail ())
{
  elf_layout big;
  elf_on_disk big_endian (build_elf (&big));
  REQUIRE (load (big_endian.name (), 0) == (int) ERC32_RAM);

  /* Nothing should byte swap when the file matches the host, which is the
     other arm of every swap in the loader.  */
  bytes b (EHDR_SIZE, 0);

  b[EI_MAG0] = ELFMAG0;
  b[EI_MAG1] = ELFMAG1;
  b[EI_MAG2] = ELFMAG2;
  b[EI_MAG3] = ELFMAG3;
  b[EI_CLASS] = ELFCLASS32;
  b[EI_DATA] = ELFDATA2LSB;

  Elf32_Ehdr *e = (Elf32_Ehdr *) &b[0];
  e->e_machine = EM_SPARC;
  e->e_entry = ERC32_RAM;

  elf_on_disk file (b);
  CHECK (load (file.name (), 0) == (int) ERC32_RAM);
  CHECK (ebase.cpu == CPU_ERC32);
}
