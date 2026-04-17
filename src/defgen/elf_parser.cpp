#include "elf_types.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace defgen::detail {

#define ELF_ST_TYPE(info) ((info)&0xf)
#define ELF_ST_BIND(info) ((info) >> 4)

namespace {

template <typename TOffset>
struct OffsetAndSize {
    TOffset offset{};
    TOffset size{};
};

struct ElfImage {
    std::vector<std::uint8_t> image;
    unsigned e_shnum = 0;

    [[nodiscard]] int parse_ident(bool& is32bit, std::string& err) const {
        constexpr int EI_CLASS = 4;
        constexpr int EI_DATA = 5;
        constexpr int EI_VERSION = 6;
        constexpr int ELFDATA2LSB = 1;
        constexpr int ELFCLASS32 = 1;
        constexpr int ELFCLASS64 = 2;

        const ELFIdent& ident = *reinterpret_cast<const ELFIdent*>(image.data());
        if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
            err = "Invalid ELF header prefix";
            return -1;
        }
        if (ident[EI_DATA] != ELFDATA2LSB) {
            err = "Support only ELFDATA2LSB encoding";
            return -2;
        }
        if (ident[EI_CLASS] != ELFCLASS32 && ident[EI_CLASS] != ELFCLASS64) {
            err = "Invalid ELF class";
            return -3;
        }
        if (ident[EI_VERSION] != 1) {
            err = "Unknown ELF version";
            return -4;
        }
        is32bit = ident[EI_CLASS] == ELFCLASS32;
        return 0;
    }

    template <typename TOffset>
    [[nodiscard]] int get_string_table(const ElfHeader<TOffset>& header, byte4 sectionIndex, OffsetAndSize<TOffset>& stringTable, std::string& err) const {
        auto* sections = reinterpret_cast<const SectionHeader<TOffset>*>(&image[static_cast<size_t>(header.e_shoff)]);
        if (sectionIndex == 0 || static_cast<unsigned>(sectionIndex) >= e_shnum) {
            err = "Section index is out of range";
            return -1;
        }
        const SectionHeader<TOffset>& section = sections[sectionIndex];
        if (section.sh_size == 0) {
            err = "Section is empty";
            return -2;
        }
        stringTable.offset = section.sh_offset;
        stringTable.size = section.sh_size;
        return 0;
    }

    template <typename TOffset>
    [[nodiscard]] int get_object_string_and_symbol_tables(const ElfHeader<TOffset>& header, OffsetAndSize<TOffset> headersStringTable, int& stringTableIndex,
                                                            int& symbolTableIndex, std::string& err) const {
        constexpr int SHT_SYMTAB = 2;
        constexpr int SHT_STRTAB = 3;

        stringTableIndex = -1;
        symbolTableIndex = -1;

        auto* sections = reinterpret_cast<const SectionHeader<TOffset>*>(&image[static_cast<size_t>(header.e_shoff)]);
        for (int i = 0; i < static_cast<int>(e_shnum); i++) {
            const SectionHeader<TOffset>& section = sections[i];
            if (section.sh_name >= headersStringTable.size) {
                err = "Invalid section name index";
                return -1;
            }
            const char* sectionName = reinterpret_cast<const char*>(&image[static_cast<size_t>(headersStringTable.offset + section.sh_name)]);

            if (section.sh_type == SHT_STRTAB && std::strcmp(".strtab", sectionName) == 0) {
                if (stringTableIndex != -1) {
                    err = "Multiple string tables";
                    return -2;
                }
                stringTableIndex = i;
            } else if (section.sh_type == SHT_SYMTAB && std::strcmp(".symtab", sectionName) == 0) {
                if (symbolTableIndex != -1) {
                    err = "Multiple symbol tables";
                    return -3;
                }
                symbolTableIndex = i;
            }
        }

        if (stringTableIndex == -1) {
            err = "No string table";
            return -4;
        }
        if (symbolTableIndex == -1) {
            err = "No symbol table";
            return -5;
        }
        return 0;
    }

    template <typename TOffset>
    [[nodiscard]] int get_symbols(const ElfHeader<TOffset>& header, int objectSymbolTableIndex, OffsetAndSize<TOffset> objectStringTable, std::vector<std::string>& result,
                                  std::string& err) const {
        constexpr int STT_FUNC = 2;
        constexpr int STB_GLOBAL = 1;

        auto* sections = reinterpret_cast<const SectionHeader<TOffset>*>(&image[static_cast<size_t>(header.e_shoff)]);
        const SectionHeader<TOffset>& objSymbolTableSec = sections[objectSymbolTableIndex];
        auto* symbols = reinterpret_cast<const SymbolHeader<TOffset>*>(&image[static_cast<size_t>(objSymbolTableSec.sh_offset)]);

        const TOffset symbolsCount = objSymbolTableSec.sh_size / objSymbolTableSec.sh_entsize;
        if (symbolsCount * sizeof(SymbolHeader<TOffset>) > objSymbolTableSec.sh_size + 1) {
            err = "Invalid sh_entsize";
            return -1;
        }
        if (symbolsCount == 0) {
            err = "Symbol table is empty";
            return -2;
        }

        const char* table = reinterpret_cast<const char*>(&image[static_cast<size_t>(objectStringTable.offset)]);

        for (TOffset i = 0; i < symbolsCount; i++) {
            const SymbolHeader<TOffset>& symbol = symbols[i];
            const int stType = ELF_ST_TYPE(symbol.st_info);
            const int stBind = ELF_ST_BIND(symbol.st_info);

            if (stType != STT_FUNC || stBind != STB_GLOBAL) {
                continue;
            }
            if (symbol.st_name >= objectStringTable.size) {
                err = "Invalid function name offset";
                return -3;
            }
            result.emplace_back(&table[symbol.st_name]);
        }
        return 0;
    }

    template <typename TOffset>
    [[nodiscard]] int parse(std::vector<std::string>& result, std::string& err) {
        const ElfHeader<TOffset>& header = *reinterpret_cast<const ElfHeader<TOffset>*>(image.data());
        auto* sections = reinterpret_cast<const SectionHeader<TOffset>*>(&image[static_cast<size_t>(header.e_shoff)]);
        e_shnum = header.e_shnum;
        if (e_shnum == 0) {
            e_shnum = static_cast<unsigned>(sections[0].sh_size);
        }
        byte4 shstrndx = header.e_shstrndx;
        if (shstrndx == 0xffff) {
            shstrndx = sections[0].sh_link;
        }

        OffsetAndSize<TOffset> headersStringTable{};
        int ec = get_string_table(header, shstrndx, headersStringTable, err);
        if (ec != 0) {
            return -20 + ec;
        }

        int objStringTableIndex = 0;
        int objectSymbolTableIndex = 0;
        ec = get_object_string_and_symbol_tables(header, headersStringTable, objStringTableIndex, objectSymbolTableIndex, err);
        if (ec != 0) {
            return -30 + ec;
        }

        OffsetAndSize<TOffset> objectStringTable{};
        ec = get_string_table(header, static_cast<byte4>(objStringTableIndex), objectStringTable, err);
        if (ec != 0) {
            return -40 + ec;
        }

        ec = get_symbols(header, objectSymbolTableIndex, objectStringTable, result, err);
        if (ec != 0) {
            return -500 + ec;
        }
        return 0;
    }

    [[nodiscard]] int read_and_parse(const std::vector<std::uint8_t>& file_bytes, std::vector<std::string>& result, std::string& err) {
        image = file_bytes;
        bool is32bit = false;
        int ec = parse_ident(is32bit, err);
        if (ec != 0) {
            return -10 + ec;
        }
        return is32bit ? parse<byte4>(result, err) : parse<byte8>(result, err);
    }
};

[[nodiscard]] bool read_file_bytes(const std::filesystem::path& p, std::vector<std::uint8_t>& out, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        err = "cannot open object file";
        return false;
    }
    f.seekg(0, std::ios::end);
    const auto sz = f.tellg();
    if (sz < 0) {
        err = "cannot size object file";
        return false;
    }
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    if (sz > 0) {
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    }
    return true;
}

} // namespace

[[nodiscard]] int process_elf_object(const std::filesystem::path& path, std::vector<std::string>& export_funcs, std::string& err) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(path, bytes, err)) {
        return -1;
    }
    ElfImage img{};
    return img.read_and_parse(bytes, export_funcs, err);
}

} // namespace defgen::detail
