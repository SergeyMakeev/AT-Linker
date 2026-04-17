#include "coff_image.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace defgen::detail {

namespace {

using namespace coff;

[[nodiscard]] std::string get_export_name(const std::string& szName) {
    bool alnum_only = true;
    for (char c : szName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            alnum_only = false;
            break;
        }
    }
    if (alnum_only && !szName.empty() && szName[0] == '_') {
        return szName.substr(1);
    }
    return szName;
}

[[nodiscard]] bool starts_with(const std::string& sz, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    return sz.size() > n && std::strncmp(sz.c_str(), prefix, n) == 0;
}

void gather_public_symbols(SCoffImage* p, std::vector<std::string>* pResFunc, std::vector<std::string>* pResData) {
    std::vector<char> sections(static_cast<size_t>(p->numSections), 0);

    for (int k = 0; k < p->numSymbols; k += p->GetNumAuxSymbols(k) + 1) {
        SCoffImage::SCoffSymbolBigObj symb{};
        p->GetSymbol(k, &symb);
        int nSection = static_cast<int>(symb.nSection);
        if (nSection > 0 && nSection <= static_cast<int>(sections.size())) {
            if (symb.nType == 0 && symb.nStorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
                std::string szName;
                GetName(*p, symb.szName, &szName);
                if (starts_with(szName, "??")) {
                    continue;
                }
                if (starts_with(szName, "__real")) {
                    continue;
                }
                pResData->push_back(get_export_name(szName));
            }
            if (std::strncmp(symb.szName, ".text", 5) == 0 && symb.nAuxSymbols >= 1 && symb.nStorageClass == IMAGE_SYM_CLASS_STATIC) {
                byte nSelection = p->GetSectionSelectionFromSectionDefinition(k + 1);
                if (nSelection == IMAGE_COMDAT_SELECT_NODUPLICATES) {
                    sections[static_cast<size_t>(nSection - 1)] = 1;
                }
            }
        }
    }

    for (int k = 0; k < p->numSymbols; k += p->GetNumAuxSymbols(k) + 1) {
        SCoffImage::SCoffSymbolBigObj symb{};
        p->GetSymbol(k, &symb);
        int nSection = static_cast<int>(symb.nSection);
        if (nSection > 0 && nSection <= static_cast<int>(sections.size()) && symb.nType == 0x20 &&
            symb.nStorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
            if ((p->pSections[nSection - 1].flags & IMAGE_SCN_LNK_COMDAT) != 0) {
                if (sections[static_cast<size_t>(nSection - 1)] == 0) {
                    continue;
                }
            }
            std::string szName;
            GetName(*p, symb.szName, &szName);
            pResFunc->push_back(get_export_name(szName));
        }
    }
}

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

[[nodiscard]] int process_coff_object(const std::filesystem::path& path, std::vector<std::string>& export_funcs,
                                      std::vector<std::string>& export_data, std::string& err) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(path, bytes, err)) {
        return -1;
    }
    SCoffImage src{};
    const std::string label = path.filename().string();
    if (!src.load(bytes, label, err)) {
        return -1;
    }
    if (src.timeStamp == 0) {
        // Legacy: skip placeholder objects with zero timestamp.
        return 0;
    }
    gather_public_symbols(&src, &export_funcs, &export_data);
    return 0;
}

} // namespace defgen::detail
