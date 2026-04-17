#pragma once

#include <cstdint>

// PE/COFF constants (winnt.h) kept local for a portable static lib.
namespace defgen::coff
{

inline constexpr std::uint16_t IMAGE_FILE_MACHINE_I386 = 0x014c;
inline constexpr std::uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;

inline constexpr std::uint8_t IMAGE_SYM_CLASS_EXTERNAL = 2;
inline constexpr std::uint8_t IMAGE_SYM_CLASS_STATIC = 3;

inline constexpr std::uint32_t IMAGE_SCN_LNK_COMDAT = 0x00001000;

inline constexpr std::uint8_t IMAGE_COMDAT_SELECT_NODUPLICATES = 1;

} // namespace defgen::coff
