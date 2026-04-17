# link-export-all

**Bring an “export many symbols from object files into a DLL” workflow to the Microsoft linker.** On GCC/Clang/ELF, people often talk about broad symbol export using visibility and linker flags (for example `-Wl,--export-dynamic` for executables, or default-visibility shared objects). **MSVC `link.exe` has no single switch** that means “take these `.obj` files and export essentially everything suitable as a DLL surface.” This project fills that gap by **scanning COFF (`.obj`) or ELF (`.o`) objects**, generating a **module-definition (`.def`)** file, and then invoking the **real** `link.exe`.

Annotating **`__declspec(dllexport)`** on every relevant symbol is fine for a small, controlled API. It is **impractical for large, existing codebases** (millions of lines, symbols spread across translation units). A generated `.def` derived from object files scales where manual annotation does not.

## Components

| Piece | Role |
|--------|------|
| **`defgen` (static library)** | Cross-platform C++20 library: given paths to object files, produces `.def` text (MSVC `EXPORTS` form, or an ELF-style `export { }` block used by the legacy `.emd` path). |
| **`link-export-all` (Windows executable)** | Drop-in style **proxy** around `link.exe`: parses the same style of arguments the old tool used, generates/updates the `.def`, then runs the real linker via `CreateProcess`. |

See **`plan.md`** for design and migration notes. Legacy Visual Studio projects under `AT_Linker/` and `CoffParser/` are **not** the canonical build (CMake is).

## Build (CMake)

Requirements: **Windows**, **Visual Studio 2019+** (or Build Tools) with **CMake 3.20+**.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs (typical):

- `build/Release/defgen.lib`
- `build/Release/link-export-all.exe`

To build only the library (e.g. on CI without the proxy), configure with `-DLINK_EXPORT_ALL_BUILD_PROXY=OFF`.

## Using the proxy

The proxy must know where the **real** `link.exe` is, via **`/lorig:<path>`** (this argument is **consumed** and not passed downstream).

Workflow (simplified):

1. Pass your usual linker arguments, plus **`/DEFGEN`**, **`/DEF:<path\to\exports.def>`** (COFF/MSVC objects), **or** an **`.emd`** path plus **`/DEFGEN`** for the ELF-style export block.
2. The tool collects **`*.obj`** (and optional **`.olst`** response lists), regenerates the `.def` when inputs are newer than the `.def` or the object count line changed, then runs **`/lorig:`** `link.exe` with the remaining arguments.

Optional **`DefBuildIgnores.txt`** in the **current working directory**: one substring per line; export names containing that substring are skipped (same behavior as the legacy tool).

Example fragments:

```bat
link-export-all.exe /lorig:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\bin\Hostx64\x64\link.exe" ^
  /DEFGEN /DEF:myexports.def /DLL ... *.obj
```

## Using the `defgen` library

```cpp
#include <defgen/defgen.hpp>

defgen::GenerateOptions opt;
opt.ignore_substrings = { "??" }; // optional
opt.object_count_line = ";ObjectCount=1";

const defgen::GenerateResult r =
    defgen::generate_def({ std::filesystem::path("a.obj") }, defgen::ObjectFormat::Coff, opt);
if (r.ec != defgen::Errc::Ok) { /* r.message */ }
// r.out.lines — write to a .def file
```

## Limitations

- **Heuristics**, not a formal “every symbol in the universe” guarantee: COMDAT handling, name filtering (`??`, `__real`, etc.), and **functions vs. data** mirror the legacy implementation (data exports are still not emitted in the COFF `.def` path).
- **Proxy is Windows-only**; the **`defgen`** library is intended to stay **portable** for parsing and testing.

## License

See `LICENSE`.
