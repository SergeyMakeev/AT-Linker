#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace defgen::detail {

[[nodiscard]] int process_coff_object(const std::filesystem::path& path, std::vector<std::string>& export_funcs,
                                      std::vector<std::string>& export_data, std::string& err);

[[nodiscard]] int process_elf_object(const std::filesystem::path& path, std::vector<std::string>& export_funcs, std::string& err);

} // namespace defgen::detail
