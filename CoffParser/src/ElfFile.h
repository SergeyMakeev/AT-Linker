#pragma once

typedef unsigned char byte1;
typedef unsigned short byte2;
typedef unsigned int byte4;
typedef unsigned long long byte8;

#pragma pack(push, 1)

typedef byte1 ELFIdent[16];

template<typename TOffset>
struct ElfHeader
{
	ELFIdent e_ident;       /* ELF "magic number" */
	byte2    e_type;        /* Identifies object file type */
	byte2    e_machine;     /* Specifies required architecture */
	byte4    e_version;     /* Identifies object file version */
	TOffset  e_entry;       /* Entry point virtual address */
	TOffset  e_phoff;       /* Program header table file offset */
	TOffset  e_shoff;       /* Section header table file offset */
	byte4    e_flags;       /* Processor-specific flags */
	byte2    e_ehsize;      /* ELF header size in bytes */
	byte2    e_phentsize;   /* Program header table entry size */
	byte2    e_phnum;       /* Program header table entry count */
	byte2    e_shentsize;   /* Section header table entry size */
	byte2    e_shnum;       /* Section header table entry count */
	byte2    e_shstrndx;    /* Section header string table index */
};

template<typename TOffset>
struct SectionHeader
{
	byte4	sh_name;          /* Section name, index in string tbl */
	byte4	sh_type;          /* Type of section */
	TOffset	sh_flags;       /* Miscellaneous section attributes */
	TOffset	sh_addr;        /* Section virtual addr at execution */
	TOffset	sh_offset;      /* Section file offset */
	TOffset	sh_size;        /* Size of section in bytes */
	byte4	sh_link;          /* Index of another section */
	byte4	sh_info;          /* Additional section information */
	TOffset	sh_addralign;   /* Section alignment */
	TOffset	sh_entsize;     /* Entry size if section holds table */
};

template<typename TOffset>
struct SymbolHeader
{
	SymbolHeader() { static_assert(false, "Invalid SymbolHeader size"); }
};

template<>
struct SymbolHeader<byte4>
{
	byte4	st_name;          /* Symbol name, index in string tbl */
	byte4	st_value;         /* Value of the symbol */
	byte4	st_size;          /* Associated symbol size */
	byte1	st_info;          /* Type and binding attributes */
	byte1	st_other;         /* No defined meaning, 0 */
	byte2	st_shndx;         /* Associated section index */
};

template<>
struct SymbolHeader<byte8>
{
	byte4	st_name;          /* Symbol name, index in string tbl */
	byte1	st_info;          /* Type and binding attributes */
	byte1	st_other;         /* No defined meaning, 0 */
	byte2	st_shndx;         /* Associated section index */
	byte8	st_value;         /* Value of the symbol */
	byte8	st_size;          /* Associated symbol size */
};

#pragma pack(pop)