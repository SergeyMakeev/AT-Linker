#include "defgen/defgen.hpp"
#include "parsers.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace defgen {

namespace {

[[nodiscard]] ObjectFormat resolve_format(const std::filesystem::path& path, ObjectFormat f) {
    if (f != ObjectFormat::Auto) {
        return f;
    }
    const auto ext = path.extension().string();
    std::string lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == ".o") {
        return ObjectFormat::Elf;
    }
    return ObjectFormat::Coff;
}

void merge_unique_sort(std::vector<std::string>& v) {
    std::unordered_set<std::string> seen;
    for (const auto& s : v) {
        seen.insert(s);
    }
    v.assign(seen.begin(), seen.end());
    std::sort(v.begin(), v.end());
}

[[nodiscard]] bool is_ignored(std::string_view name, const std::vector<std::string>& ignores) {
    for (const auto& ign : ignores) {
        if (ign.empty()) {
            continue;
        }
        if (name.find(ign) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

GenerateResult generate_def(const std::vector<std::filesystem::path>& object_files, ObjectFormat format, const GenerateOptions& options) {
    GenerateResult gr;
    std::vector<std::string> export_funcs;
    std::vector<std::string> export_data;

    for (const auto& path : object_files) {
        const ObjectFormat fmt = resolve_format(path, format);
        std::string err;
        if (fmt == ObjectFormat::Coff) {
            const int code = detail::process_coff_object(path, export_funcs, export_data, err);
            if (code != 0) {
                gr.ec = Errc::Parse;
                gr.message = err;
                return gr;
            }
        } else {
            const int code = detail::process_elf_object(path, export_funcs, err);
            if (code != 0) {
                gr.ec = Errc::Parse;
                gr.message = err;
                return gr;
            }
        }
    }

    merge_unique_sort(export_funcs);
    merge_unique_sort(export_data);

    std::vector<std::string> filtered;
    filtered.reserve(export_funcs.size());
    for (const auto& name : export_funcs) {
        if (!is_ignored(name, options.ignore_substrings)) {
            filtered.push_back(name);
        }
    }

    std::vector<std::string>& lines = gr.out.lines;

    if (options.object_count_line.has_value()) {
        lines.push_back(*options.object_count_line);
    }

    if (options.elf_style_export_block) {
        if (!options.library_basename.empty()) {
            lines.push_back(std::string("Library: ") + options.library_basename + " {");
        }
        lines.push_back("export: {");
        for (const auto& s : filtered) {
            lines.push_back(s);
        }
        lines.push_back("}");
        if (!options.library_basename.empty()) {
            lines.push_back("}");
        }
    } else {
        lines.push_back("EXPORTS");
        for (const auto& s : filtered) {
            lines.push_back(s);
        }
    }

    gr.ec = Errc::Ok;
    return gr;
}

bool def_first_line_is(const std::filesystem::path& def_path, std::string_view expected) {
    std::ifstream f(def_path);
    if (!f) {
        return false;
    }
    std::string line;
    if (!std::getline(f, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line == expected;
}

bool def_file_matches(const std::filesystem::path& def_path, const std::vector<std::string>& new_lines) {
    std::ifstream f(def_path);
    if (!f) {
        return false;
    }
    std::size_t index = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (index >= new_lines.size()) {
            return false;
        }
        if (line != new_lines[index]) {
            return false;
        }
        ++index;
    }
    return index == new_lines.size();
}

} // namespace defgen
