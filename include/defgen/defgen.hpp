#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace defgen
{

/// MSVC COFF `.obj` or GNU-style ELF relocatable (`.o`). `Auto` picks by file extension: `.o` -> ELF, otherwise COFF.
enum class ObjectFormat
{
    Auto,
    Coff,
    Elf
};

struct GenerateOptions
{
    /// Substrings; if any match an export name, that name is omitted (same idea as legacy `DefBuildIgnores.txt` lines).
    std::vector<std::string> ignore_substrings;
    /// When true, emit SN Linker EMD text (`Library:` / `export:`) for PS4 PRX instead of a MSVC `.def` `EXPORTS` section.
    bool elf_style_export_block = false;
    /// Basename (no extension) used for `Library: <name> {` when `elf_style_export_block` is true.
    std::string library_basename;
    /// First line of the file, e.g. `;ObjectCount=42` or `//ObjectCount=42`, for incremental rebuild fingerprints.
    std::optional<std::string> object_count_line;
};

struct GenerateOutput
{
    std::vector<std::string> lines;
};

enum class Errc
{
    Ok = 0,
    Io = 1,
    Parse = 2
};

struct GenerateResult
{
    Errc ec = Errc::Ok;
    std::string message;
    GenerateOutput out;
};

/// Read object files, collect public symbols, and build `.def` (or ELF-style export block) lines.
[[nodiscard]] GenerateResult generate_def(const std::vector<std::filesystem::path>& object_files, ObjectFormat format,
                                          const GenerateOptions& options = {});

/// Line-by-line compare with an existing file; avoids rewriting when identical.
[[nodiscard]] bool def_file_matches(const std::filesystem::path& def_path, const std::vector<std::string>& new_lines);

/// True if the first line of `def_path` equals `expected` (after opening the file).
[[nodiscard]] bool def_first_line_is(const std::filesystem::path& def_path, std::string_view expected);

} // namespace defgen
