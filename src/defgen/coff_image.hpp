#pragma once

#include "coff_pe_constants.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace defgen::detail
{

using word = std::uint16_t;
using dword = std::uint32_t;
using byte = std::uint8_t;

#pragma pack(push, 1)
using SCoffName = char[8];

struct SCoffImage
{
    struct SCoffHeaderBigObj
    {
        word Sig1;
        word Sig2;
        word Version;
        word machine;
        dword timeStamp;
        unsigned char classID[16];
        dword sizeOfData;
        dword flags;
        dword metaDataSize;
        dword metaDataOffset;
        dword nSections;
        dword pSymbols;
        dword nSymbols;
    };

    struct SCoffSymbolBigObj
    {
        SCoffName szName;
        int nValue;
        dword nSection;
        word nType;
        byte nStorageClass;
        byte nAuxSymbols;
    };

    struct SCoffSectionDefinitionBigObj
    {
        dword dwSize;
        word nRelocs;
        word nLineNumbers;
        dword dwChecksum;
        word nNumber;
        byte nSelection;
        byte reserved;
        word highNumber;
        byte unused[2];
    };

    struct SCoffHeader
    {
        word machine;
        word nSections;
        dword timeStamp;
        dword pSymbols;
        dword nSymbols;
        word nOptionalHeaderSize;
        word flags;
    };

    struct SCoffSection
    {
        SCoffName szName;
        dword dwImageSize;
        dword dwAddrStart;
        dword dwSize;
        dword pData;
        dword pRelocs;
        dword pLineNumbers;
        word nRelocs;
        word nLineNumbers;
        dword flags;
    };

    struct SCoffSymbol
    {
        SCoffName szName;
        int nValue;
        word nSection;
        word nType;
        byte nStorageClass;
        byte nAuxSymbols;
    };

    struct SCoffSectionDefinition
    {
        dword dwSize;
        word nRelocs;
        word nLineNumbers;
        dword dwChecksum;
        word nNumber;
        byte nSelection;
        byte unused[3];
    };

    dword timeStamp = 0;
    int numSymbols = 0;
    int numSections = 0;
    int nameOffset = 0;
    SCoffSection* pSections = nullptr;
    std::vector<byte> image;

    SCoffSymbol* pSymbolsStd = nullptr;
    SCoffSymbolBigObj* pSymbolsBig = nullptr;

    [[nodiscard]] byte GetNumAuxSymbols(int index) const
    {
        if (pSymbolsStd != nullptr)
        {
            return pSymbolsStd[index].nAuxSymbols;
        }
        return pSymbolsBig[index].nAuxSymbols;
    }

    void GetSymbol(int index, SCoffSymbolBigObj* result) const
    {
        if (pSymbolsStd != nullptr)
        {
            result->nAuxSymbols = pSymbolsStd[index].nAuxSymbols;
            result->nSection = pSymbolsStd[index].nSection;
            result->nStorageClass = pSymbolsStd[index].nStorageClass;
            result->nType = pSymbolsStd[index].nType;
            result->nValue = pSymbolsStd[index].nValue;
            std::memcpy(result->szName, pSymbolsStd[index].szName, sizeof(SCoffName));
            return;
        }
        result->nAuxSymbols = pSymbolsBig[index].nAuxSymbols;
        result->nSection = pSymbolsBig[index].nSection;
        result->nStorageClass = pSymbolsBig[index].nStorageClass;
        result->nType = pSymbolsBig[index].nType;
        result->nValue = pSymbolsBig[index].nValue;
        std::memcpy(result->szName, pSymbolsBig[index].szName, sizeof(SCoffName));
    }

    [[nodiscard]] byte GetSectionSelectionFromSectionDefinition(int index) const
    {
        if (pSymbolsStd != nullptr)
        {
            auto& def = *reinterpret_cast<SCoffSectionDefinition*>(&pSymbolsStd[index]);
            return def.nSelection;
        }
        auto& def = *reinterpret_cast<SCoffSectionDefinitionBigObj*>(&pSymbolsBig[index]);
        return def.nSelection;
    }

    [[nodiscard]] bool parse_coff(SCoffHeader* header, std::string_view fileName, std::string& err);

    [[nodiscard]] bool parse_coff_bigobj(SCoffHeaderBigObj* header, std::string_view fileName, std::string& err);

    /// Load COFF image from raw bytes (entire `.obj` file).
    [[nodiscard]] bool load(std::span<const std::uint8_t> data, std::string_view file_label, std::string& err);
};
#pragma pack(pop)

inline void GetName(const SCoffImage& image, const SCoffName& a, std::string* pRes)
{
    const int* pA = reinterpret_cast<const int*>(&a);
    if (pA[0] != 0)
    {
        char szBuf[16]{};
        static_assert(sizeof(a) < sizeof(szBuf));
        std::memcpy(szBuf, &a, sizeof(a));
        *pRes = szBuf;
        return;
    }
    *pRes = reinterpret_cast<const char*>(&image.image[static_cast<size_t>(image.nameOffset) + static_cast<size_t>(pA[1])]);
}

} // namespace defgen::detail
