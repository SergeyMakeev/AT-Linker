// SPDX-License-Identifier: MIT
// Windows-only proxy: emulates an "export all symbols" workflow for MSVC link (similar in intent to
// -Wl,--export-dynamic / broad ELF visibility), by generating a .def and invoking the real link.exe.

#include "defgen/defgen.hpp"

#include <Shlwapi.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace fs = std::filesystem;

namespace {

constexpr int kMaxCmdLine = 32768;

bool verbose_out = false;

bool is_quote(wchar_t c) { return c == L'\"'; }

struct ImportantParams {
    std::vector<std::wstring> obj_list;
    std::vector<std::wstring> olst_files;
    std::wstring def_name;
    std::wstring linker_path;
    std::wstring obj_list_path;
    bool has_def = false;
    bool has_emd = false;
    bool gen_obj_list = false;
};

enum class PrmKind {
    None = -1,
    EmptyPrm = 0,
    ResponseFile = 1,
    Defgen = 2,
    DefFile = 3,
    OriginLinker = 4,
    GenerateObjectList = 5,
    Implib = 6,
    O = 7,
    ObjFile = 8,
    ObjListFile = 9,
    OFile = 10,
    EmdFile = 11,
};

[[nodiscard]] PrmKind classify_param(const wchar_t* raw_param) {
    std::wstring param = raw_param;
    param.erase(std::remove_if(param.begin(), param.end(), is_quote), param.end());

    const auto n = param.size();
    if (n == 0) {
        return PrmKind::EmptyPrm;
    }
    if (param[0] == L'@') {
        return PrmKind::ResponseFile;
    }

    if (param[0] == L'/' && n > 4) {
        if (wcsncmp(&param[1], L"DEF", 3) == 0) {
            if (param[4] == L':') {
                return PrmKind::DefFile;
            }
            if (wcsncmp(&param[4], L"GEN", 3) == 0) {
                return PrmKind::Defgen;
            }
        }
        if (wcsncmp(&param[1], L"lorig:", 6) == 0) {
            return PrmKind::OriginLinker;
        }
        if (wcsncmp(&param[1], L"objlist", 7) == 0) {
            return PrmKind::GenerateObjectList;
        }
        if (wcsncmp(&param[1], L"IMPLIB:", 7) == 0) {
            return PrmKind::Implib;
        }
    } else {
        if (wcsncmp(&param[0], L"-o", 2) == 0) {
            return PrmKind::O;
        }
        if (n > 2 && wcsncmp(&param[n - 2], L".o", 2) == 0) {
            return PrmKind::ObjFile;
        }
        if (n > 4 && wcsncmp(&param[n - 4], L".emd", 4) == 0) {
            return PrmKind::EmdFile;
        }
        if (n > 4 && wcsncmp(&param[n - 4], L".obj", 4) == 0) {
            return PrmKind::ObjFile;
        }
        if (n > 5 && wcsncmp(&param[n - 5], L".olst", 5) == 0) {
            return PrmKind::ObjListFile;
        }
    }

    return PrmKind::None;
}

void extract_important_params(std::vector<std::wstring>& command_line_params, ImportantParams& prm) {
    bool has_def_option = false;
    bool has_emf_option = false;
    bool has_def_gen_option = false;
    bool has_obj_path = false;

    bool need_erase = false;
    for (auto it = command_line_params.begin(); it != command_line_params.end();
         it = need_erase ? command_line_params.erase(it) : it + 1) {
        const std::wstring& param = *it;
        const PrmKind kind = classify_param(param.c_str());

        if (verbose_out && kind != PrmKind::None) {
            std::printf("param: '%d' in string '%ls'\n", static_cast<int>(kind), param.c_str());
        }

        need_erase = false;
        switch (kind) {
        case PrmKind::ResponseFile:
            need_erase = true;
            break;

        case PrmKind::Defgen:
            has_def_gen_option = true;
            need_erase = true;
            break;

        case PrmKind::DefFile:
            prm.def_name = param.substr(5);
            prm.def_name.erase(std::remove_if(prm.def_name.begin(), prm.def_name.end(), is_quote), prm.def_name.end());
            has_def_option = true;
            break;

        case PrmKind::EmdFile: {
            const int start_pos = (param[0] == L'\"') ? 1 : 0;
            prm.def_name = param.substr(static_cast<size_t>(start_pos));
            prm.def_name.erase(std::remove_if(prm.def_name.begin(), prm.def_name.end(), is_quote), prm.def_name.end());
            has_emf_option = true;
        } break;

        case PrmKind::OriginLinker:
            prm.linker_path = param.substr(7);
            prm.linker_path.erase(std::remove_if(prm.linker_path.begin(), prm.linker_path.end(), is_quote), prm.linker_path.end());
            need_erase = true;
            break;

        case PrmKind::GenerateObjectList:
            prm.gen_obj_list = true;
            need_erase = true;
            break;

        case PrmKind::Implib: {
            const int start_pos = (param.size() > 8 && param[8] == L'\"') ? 9 : 8;
            prm.obj_list_path = param.substr(static_cast<size_t>(start_pos));
            has_obj_path = true;
        } break;

        case PrmKind::O:
            if (std::next(it) == command_line_params.end()) {
                break;
            }
            ++it;
            {
                const int start_pos = ((*it)[0] == L'\"') ? 1 : 0;
                prm.obj_list_path = (*it).substr(static_cast<size_t>(start_pos));
            }
            has_obj_path = true;
            break;

        case PrmKind::OFile:
        case PrmKind::ObjFile: {
            std::wstring cleaned = param;
            cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), L'\"'), cleaned.end());
            prm.obj_list.push_back(std::move(cleaned));
        } break;

        case PrmKind::ObjListFile: {
            std::wstring cleaned = param;
            cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), L'\"'), cleaned.end());
            prm.olst_files.push_back(std::move(cleaned));
            need_erase = true;
        } break;

        default:
            break;
        }
    }

    if (has_obj_path) {
        std::size_t ind = prm.obj_list_path.find_last_of(L'.');
        if (ind == std::wstring::npos) {
            ind = prm.obj_list_path.size();
        }
        prm.obj_list_path.resize(ind + 5);
        prm.obj_list_path.replace(ind, 5, L".olst");
        if (verbose_out) {
            std::printf("/OUT: = found, objListPath = '%ls'\n", prm.obj_list_path.c_str());
        }
    }

    prm.has_def = has_def_option && has_def_gen_option;
    prm.has_emd = has_emf_option && has_def_gen_option;
}

void parse_response_file_utf16(const wchar_t* rsp_text, DWORD chars_count, std::vector<std::wstring>& out_params) {
    bool inside_quotes = false;
    std::wstring argument;
    for (DWORD i = 0; i < chars_count; i++) {
        const wchar_t chr = rsp_text[i];

        if (chr == L'\n' && i + 1 < chars_count && rsp_text[i + 1] == L'\r') {
            i++;
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == L'\r' && i + 1 < chars_count && rsp_text[i + 1] == L'\n') {
            i++;
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == L' ' && !inside_quotes) {
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == L'\r' || chr == L'\n') {
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == L'\"') {
            inside_quotes = !inside_quotes;
        }
        argument += chr;
    }
    if (!argument.empty()) {
        out_params.push_back(argument);
    }
}

void parse_response_file_ansi(const char* rsp_text, DWORD chars_count, std::vector<std::wstring>& out_params) {
    bool inside_quotes = false;
    std::wstring argument;
    for (DWORD i = 0; i < chars_count; i++) {
        const char chr = rsp_text[i];

        if (chr == '\n' && i + 1 < chars_count && rsp_text[i + 1] == '\r') {
            i++;
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == '\r' && i + 1 < chars_count && rsp_text[i + 1] == '\n') {
            i++;
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == ' ' && !inside_quotes) {
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == '\r' || chr == '\n') {
            if (!argument.empty()) {
                out_params.push_back(argument);
                argument.clear();
            }
            continue;
        }
        if (chr == '\"') {
            inside_quotes = !inside_quotes;
        }
        argument += static_cast<wchar_t>(static_cast<unsigned char>(chr));
    }
    if (!argument.empty()) {
        out_params.push_back(argument);
    }
}

[[nodiscard]] int parse_response_file_buffer(const unsigned char* rsp_file, DWORD rsp_size, std::vector<std::wstring>& out_params, bool& is_ansi) {
    if (rsp_size < 2) {
        std::printf("Can't parse file - file too small\n");
        return -1;
    }
    if (rsp_file[0] == 0xFE && rsp_file[1] == 0xFF) {
        std::printf("Can't parse file - invalid BOM (wrong endianness)\n");
        return -2;
    }
    if (rsp_file[0] == 0xFF && rsp_file[1] == 0xFE) {
        const DWORD chars_count = (rsp_size - 2) / sizeof(wchar_t);
        const wchar_t* rsp_text = reinterpret_cast<const wchar_t*>(rsp_file + 2);
        is_ansi = false;
        parse_response_file_utf16(rsp_text, chars_count, out_params);
        return 0;
    }
    is_ansi = true;
    parse_response_file_ansi(reinterpret_cast<const char*>(rsp_file), rsp_size, out_params);
    return 0;
}

[[nodiscard]] int read_response_file(const wchar_t* filename, std::vector<std::wstring>& lines, bool& is_ansi) {
    const HANDLE h = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("Can't open response file, error: %lu\n", GetLastError());
        return -10;
    }
    const DWORD sz = GetFileSize(h, nullptr);
    std::vector<unsigned char> buf(sz);
    DWORD read_bytes = 0;
    if (!ReadFile(h, buf.data(), sz, &read_bytes, nullptr)) {
        CloseHandle(h);
        std::printf("Can't read response file, error: %lu\n", GetLastError());
        return -30;
    }
    CloseHandle(h);
    return parse_response_file_buffer(buf.data(), read_bytes, lines, is_ansi);
}

void load_def_build_ignores(defgen::GenerateOptions& opt) {
    std::ifstream f("DefBuildIgnores.txt");
    if (!f) {
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            opt.ignore_substrings.push_back(line);
        }
    }
}

[[nodiscard]] std::vector<fs::path> to_paths(const std::vector<std::wstring>& w) {
    std::vector<fs::path> out;
    out.reserve(w.size());
    for (const auto& s : w) {
        out.emplace_back(s);
    }
    return out;
}

[[nodiscard]] bool should_regenerate_def(const fs::path& def_path, std::string_view object_count_line, const std::vector<std::wstring>& obj_wpaths) {
    if (!fs::exists(def_path)) {
        return true;
    }
    if (!defgen::def_first_line_is(def_path, object_count_line)) {
        return true;
    }
    const auto def_time = fs::last_write_time(def_path);
    for (const auto& w : obj_wpaths) {
        const fs::path p(w);
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            std::printf("Warning: can't stat object file '%ls'\n", w.c_str());
            return true;
        }
        const auto t = fs::last_write_time(p, ec);
        if (ec) {
            return true;
        }
        if (t > def_time) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int write_def_lines(const fs::path& def_path, const std::vector<std::string>& lines) {
    std::ofstream out(def_path, std::ios::binary);
    if (!out) {
        std::printf("Can't create def file '%ls'\n", def_path.c_str());
        return -1;
    }
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return 0;
}

[[nodiscard]] int generate_def_file(const fs::path& def_path, const std::vector<std::wstring>& obj_wpaths, bool use_elf_style) {
    std::printf("Generate DEF file '%ls'\n", def_path.c_str());

    char obj_count_buf[128] = {};
    if (use_elf_style) {
        std::snprintf(obj_count_buf, sizeof(obj_count_buf), "//ObjectCount=%zu", obj_wpaths.size());
    } else {
        std::snprintf(obj_count_buf, sizeof(obj_count_buf), ";ObjectCount=%zu", obj_wpaths.size());
    }
    const std::string object_count_line(obj_count_buf);

    if (!should_regenerate_def(def_path, object_count_line, obj_wpaths)) {
        std::printf("DEFGEN: Skip def file update\n");
        return 0;
    }

    defgen::GenerateOptions opt;
    load_def_build_ignores(opt);
    opt.elf_style_export_block = use_elf_style;
    if (use_elf_style) {
        if (def_path.has_stem()) {
            opt.library_basename = def_path.stem().string();
        }
    }
    opt.object_count_line = object_count_line;

    const auto obj_paths = to_paths(obj_wpaths);
    const defgen::ObjectFormat fmt = use_elf_style ? defgen::ObjectFormat::Elf : defgen::ObjectFormat::Coff;

    const defgen::GenerateResult gr = defgen::generate_def(obj_paths, fmt, opt);
    if (gr.ec != defgen::Errc::Ok) {
        std::printf("DEFGEN: %s\n", gr.message.c_str());
        return -800;
    }

    if (defgen::def_file_matches(def_path, gr.out.lines)) {
        std::printf("DEFGEN: No new exports (def unchanged)\n");
        return 0;
    }

    std::printf("DEFGEN: Write to DEF\n");
    return write_def_lines(def_path, gr.out.lines);
}

void join_lines_mbs(const std::vector<std::wstring>& lines, std::vector<std::byte>& content) {
    static int code_page = GetACP();
    bool insert_nl = false;
    for (const auto& w : lines) {
        if (insert_nl) {
            content.push_back(std::byte{0x0D});
            content.push_back(std::byte{0x0A});
        }
        const int len = static_cast<int>(w.size());
        if (len == 0) {
            insert_nl = true;
            continue;
        }
        const int need = len * 4 + 16;
        std::string narrow(static_cast<size_t>(need), '\0');
        const int n = WideCharToMultiByte(code_page, 0, w.c_str(), len, narrow.data(), need, nullptr, nullptr);
        if (n > 0) {
            narrow.resize(static_cast<size_t>(n));
            for (unsigned char c : narrow) {
                content.push_back(std::byte{c});
            }
        }
        insert_nl = true;
    }
}

[[nodiscard]] int write_binary_file(const fs::path& filename, const void* data, std::size_t bytes_total) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::printf("Can't create file '%ls'\n", filename.c_str());
        return -1;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes_total));
    return out ? 0 : -2;
}

[[nodiscard]] int write_lines_to_file(const fs::path& filename, const std::vector<std::wstring>& lines, int line_capacity_hint) {
    std::vector<std::byte> content;
    content.reserve(static_cast<size_t>(lines.size()) * static_cast<size_t>(line_capacity_hint));
    join_lines_mbs(lines, content);
    return write_binary_file(filename, content.data(), content.size());
}

[[nodiscard]] int make_temp_rsp_path(wchar_t* temp_file_path) {
    wchar_t temp_dir[kMaxCmdLine] = {};
    if (GetTempPathW(static_cast<DWORD>(std::size(temp_dir)), temp_dir) == 0) {
        std::printf("Can't get temp directory path\n");
        return -1;
    }
    if (GetTempFileNameW(temp_dir, L"lst", 0, temp_file_path) == 0) {
        std::printf("Can't generate temp filename\n");
        return -2;
    }
    PathRemoveExtensionW(temp_file_path);
    if (PathAddExtensionW(temp_file_path, L".rsp") != TRUE) {
        std::printf("Can't change extension temp filename\n");
        return -3;
    }
    return 0;
}

[[nodiscard]] DWORD run_original_linker(wchar_t* command_line, const wchar_t* current_directory) {
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, TRUE, CREATE_DEFAULT_ERROR_MODE, nullptr, current_directory, &si, &pi)) {
        std::printf("Can't execute original link.exe (CreateProcess failed), error: %lu\n", GetLastError());
        return static_cast<DWORD>(-700);
    }
    DWORD exit_code = 0;
    if (pi.hProcess) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit_code;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
#ifdef _WIN64
    const char* const arch = "x64";
#else
    const char* const arch = "x86";
#endif
    std::printf("link-export-all (%s) — MSVC link proxy (export-all-style .def generation)\n", arch);

    std::vector<std::wstring> cmd_line_params;
    const wchar_t* response_file_name = nullptr;

    for (int i = 1; i < argc; i++) {
        wchar_t* p = argv[i];
        if (p[0] == L'@') {
            if (response_file_name == nullptr) {
                response_file_name = p + 1;
            } else {
                std::printf("Warning: multiple response files detected!\n");
            }
        }
        cmd_line_params.emplace_back(argv[i]);
        if (verbose_out) {
            std::printf("[%d] %ls\n", i, cmd_line_params.back().c_str());
        }
    }

    ImportantParams prms;
    extract_important_params(cmd_line_params, prms);

    std::vector<std::wstring> response_params;
    bool is_ansi = false;
    int err = 0;
    if (response_file_name != nullptr) {
        err = read_response_file(response_file_name, response_params, is_ansi);
        if (err != 0) {
            std::wprintf(L"Error reading response file %ls\n", response_file_name);
            return -100 + err;
        }
        extract_important_params(response_params, prms);
    }

    if (!prms.olst_files.empty() && !prms.gen_obj_list) {
        for (const auto& path : prms.olst_files) {
            err = read_response_file(path.c_str(), response_params, is_ansi);
            if (err != 0) {
                std::wprintf(L"Error reading object list file %ls\n", path.c_str());
                return -200 + err;
            }
        }
    }

    if (!prms.def_name.empty() && (prms.has_def || prms.has_emd)) {
        const auto t0 = std::chrono::steady_clock::now();
        const fs::path def_path(prms.def_name);
        err = generate_def_file(def_path, prms.obj_list, prms.has_emd);
        if (err != 0) {
            std::printf("DEFGEN: failed (%d)\n", err);
        }
        const auto sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("DefGen time %3.2f sec\n", sec);
    }

    wchar_t current_dir[kMaxCmdLine] = {};
    GetCurrentDirectoryW(static_cast<DWORD>(std::size(current_dir)), current_dir);

    if (verbose_out) {
        std::printf("Current dir: %ls\n", current_dir);
    }

    constexpr int reserved_line_cap = 100;
    if (prms.gen_obj_list) {
        err = write_lines_to_file(fs::path(prms.obj_list_path), prms.obj_list, reserved_line_cap);
        if (err != 0) {
            std::wprintf(L"Error writing object list %ls\n", prms.obj_list_path.c_str());
            return -300 + err;
        }
        std::wprintf(L"   Creating object list %ls\n", prms.obj_list_path.c_str());
        return 0;
    }

    if (prms.linker_path.empty()) {
        std::printf("No /lorig: path to the real linker (link.exe)\n");
        return -400;
    }

    wchar_t command_line[kMaxCmdLine] = {};
    wcscpy_s(command_line, prms.linker_path.c_str());

    wchar_t copy_rsp[kMaxCmdLine] = {};
    copy_rsp[0] = L'\0';

    for (const auto& param : cmd_line_params) {
        wcscat_s(command_line, L" \"");
        wcscat_s(command_line, param.c_str());
        wcscat_s(command_line, L"\"");
    }

    if (!response_params.empty()) {
        err = make_temp_rsp_path(copy_rsp);
        if (err != 0) {
            std::printf("Error creating temp rsp path\n");
            return -500 + err;
        }
        err = write_lines_to_file(fs::path(copy_rsp), response_params, reserved_line_cap);
        if (err != 0) {
            std::wprintf(L"Error writing merged response file %ls\n", copy_rsp);
            return -600 + err;
        }
        wcscat_s(command_line, L" \"@");
        wcscat_s(command_line, copy_rsp);
        wcscat_s(command_line, L"\"");
    }

    if (verbose_out) {
        std::wprintf(L"%ls\n", command_line);
    }

    _flushall();
    const auto t1 = std::chrono::steady_clock::now();
    const DWORD exit_code = run_original_linker(command_line, current_dir);
    const auto sec1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("Original linker time %3.2f sec\n", sec1);

    if (copy_rsp[0] != L'\0') {
        if (!DeleteFileW(copy_rsp)) {
            std::printf("Warning: can't delete temp response file\n");
        }
    }

    return static_cast<int>(exit_code);
}
