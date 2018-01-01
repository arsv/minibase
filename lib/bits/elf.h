#include <bits/types.h>

/* elfhdr.tag */
#define ELFTAG "\x7F" "ELF"

/* elfhdr.class */
#define ELF_32 1
#define ELF_64 2
/* elfhdr.data */
#define ELF_LSB 1
#define ELF_MSB 2
/* elfhdr.osabi */
#define ELF_ABI_SYSV           0
#define ELF_ABI_HPUX           1
#define ELF_ABI_NETBSD         2
#define ELF_ABI_GNU            3
#define ELF_ABI_SOLARIS        6
#define ELF_ABI_AIX            7
#define ELF_ABI_IRIX           8
#define ELF_ABI_FREEBSD        9
#define ELF_ABI_TRU64          10
#define ELF_ABI_MODESTO        11
#define ELF_ABI_OPENBSD        12
#define ELF_ABI_ARM_AEABI      64
#define ELF_ABI_ARM            97
#define ELF_ABI_STANDALONE    255
/* elf?hdr.type */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4

struct elfhdr {
	byte tag[4];
	byte class;
	byte data;
	byte version;
	byte osabi;
	byte abiversion;
	byte _pad[7];
} __attribute__((packed));

/* elf*hdr.phnum */
#define PN_XNUM 0xffff

struct elf32hdr {
	struct elfhdr ident;
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed));

struct elf64hdr {
	struct elfhdr ident;
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed));

/* elf?shdr.name */
#define SHN_UNDEF       0x0000
#define SHN_ABS         0xfff1
#define SHN_COMMON      0xfff2
#define SHN_XINDEX      0xffff
/* elf?shdr.type */
#define SHT_NULL           0
#define SHT_PROGBITS       1
#define SHT_SYMTAB         2
#define SHT_STRTAB         3
#define SHT_RELA           4
#define SHT_HASH           5
#define SHT_DYNAMIC        6
#define SHT_NOTE           7
#define SHT_NOBITS         8
#define SHT_REL            9
#define SHT_SHLIB          10
#define SHT_DYNSYM         11
#define SHT_INIT_ARRAY     14
#define SHT_FINI_ARRAY     15
#define SHT_PREINIT_ARRAY  16
#define SHT_GROUP          17
#define SHT_SYMTAB_SHNDX   18
#define SHT_NUM            19
/* elf?shdr.type platform extensions */
#define SHT_GNU_ATTRIBUTES 0x6ffffff5
#define SHT_GNU_HASH       0x6ffffff6
#define SHT_GNU_LIBLIST    0x6ffffff7
#define SHT_CHECKSUM       0x6ffffff8
#define SHT_GNU_verdef     0x6ffffffd
#define SHT_GNU_verneed    0x6ffffffe
#define SHT_GNU_versym     0x6fffffff
/* elf*shdr.flags */
#define SHF_WRITE         (1<<0)
#define SHF_ALLOC         (1<<1)
#define SHF_EXECINSTR     (1<<2)
#define SHF_MERGE         (1<<4)
#define SHF_STRINGS       (1<<5)
#define SHF_INFO_LINK     (1<<6)
#define SHF_LINK_ORDER    (1<<7)
#define SHF_OS_NONSTD     (1<<8)
#define SHF_GROUP         (1<<9)
#define SHF_TLS           (1<<10)
#define SHF_COMPRESSED    (1<<11)

struct elf32shdr {
	uint32_t name;
	uint32_t type;
	uint32_t flags;
	uint32_t addr;
	uint32_t offset;
	uint32_t size;
	uint32_t link;
	uint32_t info;
	uint32_t align;
	uint32_t entsize;
} __attribute__((packed));

struct elf64shdr {
	uint32_t name;
	uint32_t type;
	uint64_t flags;
	uint64_t addr;
	uint64_t offset;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t align;
	uint64_t entsize;
} __attribute__((packed));

/* elf?phdr.type */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7
#define PT_NUM          8
/* elf?phdr.type platform extensions */
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552
/* elf*phdr.flags */
#define PF_X  (1 << 0)
#define PF_W  (1 << 1)
#define PF_R  (1 << 2)

struct elf32phdr {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
} __attribute__((packed));

struct elf64phdr {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t vaddr;
	uint64_t paddr;
	uint64_t filesz;
	uint64_t memsz;
	uint64_t align;
} __attribute__((packed));

/* elf*dyn.tag */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RUNPATH      29
#define DT_FLAGS        30
#define DT_ENCODING     32

struct elf32dyn {
	uint32_t tag;
	uint32_t val;
} __attribute__((packed));

struct elf64dyn {
	uint64_t tag;
	uint64_t val;
} __attribute__((packed));

#define ELF_SYM_BIND(val) ((val >> 4) & 0x0F)
#define ELF_SYM_TYPE(val) ((val >> 0) & 0x0F)

/* elf*sym.info:bind */
#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STB_WEAK        2
#define STB_NUM         3

/* elf*sym.info:type */
#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4
#define STT_COMMON      5
#define STT_TLS         6
#define STT_NUM         7
#define STT_LOOS        10
#define STT_GNU_IFUNC   10
#define STT_HIOS        12
#define STT_LOPROC      13
#define STT_HIPROC      15

struct elf32sym {
	uint32_t name;
	uint32_t value;
	uint32_t size;
	byte info;
	byte other;
	uint16_t shndx;
} __attribute__((packed));

struct elf64sym {
	uint32_t name;
	byte info;
	byte other;
	uint16_t shndx;
	uint64_t value;
	uint64_t size;
} __attribute__((packed));
