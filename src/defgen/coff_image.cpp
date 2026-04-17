#include "coff_image.hpp"

#include <sstream>

namespace defgen::detail {

bool SCoffImage::parse_coff(SCoffHeader* header, std::string_view fileName, std::string& err) {
    using namespace coff;
    if (header->machine != IMAGE_FILE_MACHINE_I386 && header->machine != IMAGE_FILE_MACHINE_AMD64) {
        std::ostringstream os;
        os << fileName << " is not a COFF file (invalid machine type)";
        err = os.str();
        return false;
    }
    if (header->nOptionalHeaderSize != 0) {
        std::ostringstream os;
        os << "optional header is not supported " << fileName;
        err = os.str();
        return false;
    }

    pSections = reinterpret_cast<SCoffSection*>(&image[sizeof(SCoffHeader)]);
    pSymbolsStd = reinterpret_cast<SCoffSymbol*>(&image[header->pSymbols]);
    pSymbolsBig = nullptr;
    nameOffset = static_cast<int>(header->pSymbols + header->nSymbols * sizeof(SCoffSymbol));
    numSymbols = static_cast<int>(header->nSymbols);
    numSections = static_cast<int>(header->nSections);
    timeStamp = header->timeStamp;
    return true;
}

bool SCoffImage::parse_coff_bigobj(SCoffHeaderBigObj* header, std::string_view fileName, std::string& err) {
    using namespace coff;
    static const unsigned char bigObjclassID[16] = {0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xa9, 0x4b, 0xAF, 0x20,
                                                      0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8};

    if (header->machine != IMAGE_FILE_MACHINE_I386 && header->machine != IMAGE_FILE_MACHINE_AMD64) {
        std::ostringstream os;
        os << fileName << " is not a COFF file (invalid machine type)";
        err = os.str();
        return false;
    }

    for (int i = 0; i < 16; i++) {
        if (header->classID[i] != bigObjclassID[i]) {
            std::ostringstream os;
            os << fileName << " has invalid class ID for bigobj";
            err = os.str();
            return false;
        }
    }

    pSections = reinterpret_cast<SCoffSection*>(&image[sizeof(SCoffHeaderBigObj)]);
    pSymbolsStd = nullptr;
    pSymbolsBig = reinterpret_cast<SCoffSymbolBigObj*>(&image[header->pSymbols]);
    nameOffset = static_cast<int>(header->pSymbols + header->nSymbols * sizeof(SCoffSymbolBigObj));
    numSymbols = static_cast<int>(header->nSymbols);
    numSections = static_cast<int>(header->nSections);
    timeStamp = header->timeStamp;
    return true;
}

bool SCoffImage::load(std::span<const std::uint8_t> data, std::string_view file_label, std::string& err) {
    image.assign(data.begin(), data.end());
    if (image.size() < sizeof(SCoffHeader)) {
        err = "COFF file too small";
        return false;
    }
    auto* pBigObjHeader = reinterpret_cast<SCoffHeaderBigObj*>(image.data());
    auto* pHeader = reinterpret_cast<SCoffHeader*>(image.data());
    // Same detection as legacy AT-Linker (bigobj vs normal COFF).
    if (pBigObjHeader->Sig1 == 0 && pBigObjHeader->Sig2 == 0xFFFF && pHeader->machine != coff::IMAGE_FILE_MACHINE_I386 &&
        pHeader->machine != coff::IMAGE_FILE_MACHINE_AMD64) {
        return parse_coff_bigobj(pBigObjHeader, file_label, err);
    }
    return parse_coff(pHeader, file_label, err);
}

} // namespace defgen::detail
