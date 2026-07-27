/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 embedded brains GmbH & Co. KG */

/* Tests for elf.cc, the program loader.

   The loader is fed synthetic ELF files rather than a committed binary, so
   that a case can state exactly which field it is varying and can build the
   malformed files that the error paths need. Layout and field meanings
   follow the System V ABI. The files are 32-bit SPARC and big-endian by
   default, which is what the loader was written for; a case that needs the
   loader's unswapped path asks build_elf for ELFDATA2LSB instead.

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
#include <dirent.h>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

using sis_tests::stdout_capture;

namespace
{

const uint32 ERC32_RAM = 0x2000000;

typedef std::vector<unsigned char> bytes;

/* ELFDATA2MSB and ELFDATA2LSB order the fields of every ELF structure,
   System V ABI figure 4-3 "ELF Data Encoding".  */
void
put16 (bytes &b, size_t off, uint16 value, bool msb = true)
{
  b[off + (msb ? 0 : 1)] = (unsigned char) (value >> 8);
  b[off + (msb ? 1 : 0)] = (unsigned char) value;
}

void
put32 (bytes &b, size_t off, uint32 value, bool msb = true)
{
  b[off + (msb ? 0 : 3)] = (unsigned char) (value >> 24);
  b[off + (msb ? 1 : 2)] = (unsigned char) (value >> 16);
  b[off + (msb ? 2 : 1)] = (unsigned char) (value >> 8);
  b[off + (msb ? 3 : 0)] = (unsigned char) value;
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

/* A minimal but well formed SPARC executable: one PT_LOAD segment, an
   allocated .text of eight bytes inside it, and the section name table.
   Big-endian unless DATA says otherwise; every multi-byte field follows
   DATA, which is what the loader keys its byte swapping off.  */
bytes
build_elf (elf_layout *layout, uint16 machine = EM_SPARC,
	   uint32 entry = ERC32_RAM, unsigned char data = ELFDATA2MSB,
	   unsigned char cls = ELFCLASS32)
{
  const bool msb = (data == ELFDATA2MSB);
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

  put16 (b, offsetof (Elf32_Ehdr, e_type), ET_EXEC, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_machine), machine, msb);
  put32 (b, offsetof (Elf32_Ehdr, e_version), EV_CURRENT, msb);
  put32 (b, offsetof (Elf32_Ehdr, e_entry), entry, msb);
  put32 (b, offsetof (Elf32_Ehdr, e_phoff), (uint32) l.phdr, msb);
  put32 (b, offsetof (Elf32_Ehdr, e_shoff), (uint32) l.shdr, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_ehsize), (uint16) EHDR_SIZE, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_phentsize), (uint16) PHDR_SIZE, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_phnum), 1, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_shentsize), (uint16) SHDR_SIZE, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_shnum), 3, msb);
  put16 (b, offsetof (Elf32_Ehdr, e_shstrndx), 2, msb);

  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_type), PT_LOAD, msb);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_offset), (uint32) l.text_data,
	 msb);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_vaddr), ERC32_RAM, msb);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_paddr), ERC32_RAM, msb);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_filesz), text_size, msb);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_memsz), text_size, msb);

  /* Recognisable words, byte swapped by the loader on a little-endian
     host, so the values below are what memory should end up holding.  */
  put32 (b, l.text_data, 0x01020304, msb);
  put32 (b, l.text_data + 4, 0x05060708, msb);

  memcpy (&b[l.shstrtab_data], names, names_size);

  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_name), 1, msb);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_type), SHT_PROGBITS, msb);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_flags),
	 SHF_ALLOC | SHF_EXECINSTR, msb);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_addr), ERC32_RAM, msb);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_offset), (uint32) l.text_data,
	 msb);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_size), text_size, msb);

  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_name), 7, msb);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_type), SHT_STRTAB, msb);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_offset),
	 (uint32) l.shstrtab_data, msb);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_size), (uint32) names_size,
	 msb);

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

/* How many descriptors the process holds, for the cases that assert a
   stream was closed.  */
int
count_open_files ()
{
  DIR *d = opendir ("/proc/self/fd");
  REQUIRE (d != NULL);
  int n = 0;
  while (readdir (d) != NULL)
    n++;
  closedir (d);
  return n;
}

/* Caps the process address space just above what it already uses, so that
   the one huge allocation of the case below fails while every ordinary
   allocation around it still succeeds.  The limit is restored on the way
   out, so nothing outside the case sees it.  */
class address_space_cap
{
public:
  explicit address_space_cap (size_t headroom)
  {
    REQUIRE (getrlimit (RLIMIT_AS, &saved) == 0);

    FILE *fp = fopen ("/proc/self/statm", "r");
    REQUIRE (fp != NULL);
    unsigned long pages = 0;
    REQUIRE (fscanf (fp, "%lu", &pages) == 1);
    fclose (fp);

    struct rlimit rl = saved;
    rl.rlim_cur = (rlim_t) pages * (rlim_t) sysconf (_SC_PAGESIZE) + headroom;
    REQUIRE (setrlimit (RLIMIT_AS, &rl) == 0);
  }

  ~address_space_cap () { setrlimit (RLIMIT_AS, &saved); }

private:
  struct rlimit saved;
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

    /* reset_all leaves emulated memory alone, so a case that asserts the
       loader wrote a word would otherwise pass on what an earlier case
       left at the same address.  */
    const char zeros[16] = { 0 };
    ms->sis_memory_write (ERC32_RAM, zeros, sizeof (zeros));
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

/* Every load closes the file, whichever way it ends. The shell's load
   command and sis.cc's board detection both call elf_load more than once
   per session, so a stream kept open is a descriptor lost for good.  */
TEST_CASE_FIXTURE (elf_fixture, "elf_load closes the file it opened")
{
  elf_layout l;
  elf_on_disk good (build_elf (&l));

  bytes b = build_elf (&l);
  b[EI_MAG0] = 0;
  elf_on_disk bad (b);

  int before = count_open_files ();

  for (int i = 0; i < 8; i++)
    {
      REQUIRE (load (good.name (), 0) == (int) ERC32_RAM);
      REQUIRE (load (good.name (), 1) == (int) ERC32_RAM);
      REQUIRE (load (bad.name (), 1) == -1);
    }

  CHECK (count_open_files () == before);
}

/* The loader byte swaps a file whose ELFDATA encoding differs from the
   host's, System V ABI figure 4-3, and leaves it alone when they agree.
   Every field the body reads goes through that decision: the section name
   table header, the program headers, the section headers and the section
   contents.  The file below matches the little-endian host, so nothing may
   be swapped, and it carries a load address separate from its virtual
   address so that the program header fields are load bearing.  */
TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load loads a little-endian file unswapped")
{
  elf_layout l;
  bytes b = build_elf (&l, EM_SPARC, ERC32_RAM, ELFDATA2LSB);

  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_vaddr), 0x40000000, false);
  put32 (b, l.phdr + offsetof (Elf32_Phdr, p_paddr), ERC32_RAM, false);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_addr), 0x40000000, false);

  elf_on_disk file (b);

  std::string text;
  sis_verbose = 1;
  REQUIRE (load (file.name (), 1, &text) == (int) ERC32_RAM);

  /* The name table header was read straight, so the section name resolves.  */
  CHECK (text.find ("section: .text at 0x2000000") != std::string::npos);

  /* The contents reach memory exactly as the file holds them.  */
  unsigned char raw[8] = { 0 };
  REQUIRE (ms->sis_memory_read (ERC32_RAM, (char *) raw, 8) == 8);
  CHECK (raw[0] == 0x04);
  CHECK (raw[1] == 0x03);
  CHECK (raw[2] == 0x02);
  CHECK (raw[3] == 0x01);
  CHECK (raw[4] == 0x08);
  CHECK (raw[7] == 0x05);
}

/* The section name table header is read from the file too, so its size is
   as malformable as any other.  A size no allocation can satisfy must end
   the load rather than reach the read with a null buffer.  */
TEST_CASE_FIXTURE (elf_fixture,
		   "elf_load reports a name table it cannot buffer")
{
  elf_layout l;
  bytes b = build_elf (&l);
  put32 (b, l.sh_str + offsetof (Elf32_Shdr, sh_size), 0xfffffff0);
  elf_on_disk file (b);

  std::string text;
  int res;
  {
    address_space_cap cap (128 * 1024 * 1024);
    res = load (file.name (), 1, &text);
  }

  CHECK (res == -1);
  CHECK (text.find ("File read error") != std::string::npos);
}

/* A section header may name a size no allocation can satisfy, and the
   loader answers with a read error rather than dereferencing null.  The
   size below asks calloc for four gigabytes, which the capped address
   space refuses.  */
TEST_CASE_FIXTURE (elf_fixture, "elf_load reports a section it cannot buffer")
{
  elf_layout l;
  bytes b = build_elf (&l);
  put32 (b, l.sh_text + offsetof (Elf32_Shdr, sh_size), 0xfffffffc);
  elf_on_disk file (b);

  std::string text;
  int res;
  {
    address_space_cap cap (128 * 1024 * 1024);
    res = load (file.name (), 1, &text);
  }

  CHECK (res == -1);
  CHECK (text.find ("File read error") != std::string::npos);
}

/* read_elf_header assigns efile.bswap on every load rather than only setting
   it, so the flag from an earlier load does not carry over.  efile is static,
   so a big-endian load leaves bswap set; the little-endian file below must
   still load with its header read straight, not swapped as though big-endian.

   The big-endian load comes first inside the case rather than being left to
   whatever ran before it, so the outcome does not depend on test order.  */
TEST_CASE_FIXTURE (
    elf_fixture, "elf_load reads a little-endian file after a big-endian one")
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
