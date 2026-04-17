#pragma once

#include <cstdint>

namespace defgen::detail
{

using byte1 = std::uint8_t;
using byte2 = std::uint16_t;
using byte4 = std::uint32_t;
using byte8 = std::uint64_t;

using ELFIdent = byte1[16];

template <typename TOffset> struct ElfHeader
{
    ELFIdent e_ident;
    byte2 e_type;
    byte2 e_machine;
    byte4 e_version;
    TOffset e_entry;
    TOffset e_phoff;
    TOffset e_shoff;
    byte4 e_flags;
    byte2 e_ehsize;
    byte2 e_phentsize;
    byte2 e_phnum;
    byte2 e_shentsize;
    byte2 e_shnum;
    byte2 e_shstrndx;
};

template <typename TOffset> struct SectionHeader
{
    byte4 sh_name;
    byte4 sh_type;
    TOffset sh_flags;
    TOffset sh_addr;
    TOffset sh_offset;
    TOffset sh_size;
    byte4 sh_link;
    byte4 sh_info;
    TOffset sh_addralign;
    TOffset sh_entsize;
};

template <typename TOffset> struct SymbolHeader;

template <> struct SymbolHeader<byte4>
{
    byte4 st_name;
    byte4 st_value;
    byte4 st_size;
    byte1 st_info;
    byte1 st_other;
    byte2 st_shndx;
};

template <> struct SymbolHeader<byte8>
{
    byte4 st_name;
    byte1 st_info;
    byte1 st_other;
    byte2 st_shndx;
    byte8 st_value;
    byte8 st_size;
};

} // namespace defgen::detail
