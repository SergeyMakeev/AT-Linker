# link-export-all

**Bring an "export many symbols from object files into a DLL" workflow to the Microsoft linker.** On GCC/Clang/ELF, people often talk about broad symbol export using visibility and linker flags (for example `-Wl,--export-dynamic` for executables, or default-visibility shared objects). **MSVC `link.exe` has no single switch** that means "take these `.obj` files and export essentially everything suitable as a DLL surface." This project fills that gap by **scanning COFF (`.obj`) or ELF (`.o`) objects**, generating a **module-definition (`.def`)** file, and then invoking the **real** `link.exe`.

Annotating **`__declspec(dllexport)`** on every relevant symbol is fine for a small, controlled API. It is **impractical for large, existing codebases** (millions of lines, symbols spread across translation units). A generated `.def` derived from object files scales where manual annotation does not.

### Typical use case

**Problem:** Linking a **single monolithic** executable (or module) from a huge codebase often means **very long link times** on every iteration. That hurts day-to-day development.

**Approach:** Split the game into **separate DLLs** (on PC) or **PRX** modules (on **PS4**), each with an **import library** (`.lib` / stub libs) for consumers. Individual links stay **much smaller and faster**. At **runtime**, load modules and resolve imports so behavior still matches a **monolithic** layout from the player's perspective.

**Where this tool fits:** You need **broad exports** from each library without hand-maintaining **`__declspec(dllexport)`** or giant export lists. The proxy + generated **`.def`** (or **`.emd`** for **SN Linker**) automates that export surface.

**Release builds:** When you can tolerate a **long link** and want a **classic** pipeline (no proxy), point the build at the **real linker only** and drop **`link-export-all`** from the command line. Same sources; you are switching **link strategy**, not rewriting the project.

## Components

| Piece | Role |
|--------|------|
| **`defgen` (static library)** | Cross-platform C++20 library: turns object file lists into export text: either a MSVC `.def` (`EXPORTS`) or a **`.emd`** file in the **SN Linker `Library:` / `export:`** form (see [EMD files (PS4 PRX)](#emd-files-ps4-prx)). |
| **`link-export-all` (Windows executable)** | Drop-in **proxy** around the **real linker** (`link.exe` on PC, **SN Linker** on PS4, etc.): parses MSVC-style arguments, generates/updates **`.def`** or **`.emd`**, then `CreateProcess` the real executable from **`/lorig:`** or **`LINK_EXPORT_ALL_LINKER`**. |

See **`plan.md`** for design and migration notes. Legacy Visual Studio projects under `AT_Linker/` and `CoffParser/` are **not** the canonical build (CMake is).

### EMD files (PS4 PRX)

Sony's **SN Linker** for **PS4** combines object files into **ELF** and **PRX** images; it can create **bulk symbol exports for PRX** using **EMD** files. An **EMD** file is a **text** list of function and variable symbols to export when building a **PRX** (the same idea as a `.def` for a Windows DLL). The workflow is meant to feel familiar if you have used DLLs on Windows: exports and imports, `__declspec(dllexport)` / `__declspec(dllimport)`, and link-time export lists. Exports can come from source annotations **or** from directives at link time; the official **Linker User's Guide** covers EMD syntax and how EMD interacts with `__declspec(dllexport)`.

**`defgen`** can **generate** EMD text from **ELF** `.o` inputs: `Library:` / `export:` blocks, `//` comments, one symbol per line (use mangled names for C++ where required). When a PRX exports symbols, the linker also emits **stub** artifacts (`*_stub.a`, `*_stub_weak.a`) for importers; treat those and case rules per the SDK docs.

Example shape (details and constraints are in the SDK):

```text
// comment to end of line
Library: libplayer {
    export: {
       play
       pause
    }
}
```

**Host environment:** PS4 development on **Windows** uses **full MSVC integration** in Visual Studio; **SN Linker** is wired in so the IDE still looks like a **native MSVC-style** toolchain. The **`.emd`** path is part of that **same Windows** workflow. What differs from a straight PC build is the **artifact shape**: **ELF** `.o` files and an **`.emd`** consumed by **SN Linker** for PRX, not **COFF** `.obj` plus a **`.def`** for **`link.exe`**. **`link-export-all`** still acts as a proxy: **`/lorig:`** or **`LINK_EXPORT_ALL_LINKER`** must point at the **actual** linker executable your integration invokes (the SN Linker binary from your SDK layout), even when the outer command line looks MSVC-like.

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

**CI:** GitHub Actions builds Release/x64 on every push and pull request (see `.github/workflows/windows.yml`); artifacts include `link-export-all.exe` and `defgen.lib`.

The proxy and `defgen` are built with the **static MSVC runtime** (`/MT` / `/MTd`) so the executable does not depend on the VC++ redistributable DLLs (`vcruntime*.dll`, `msvcp*.dll`). You can confirm with `dumpbin /dependents link-export-all.exe` (only Windows system DLLs such as `KERNEL32.dll` should appear).

## Using the proxy

The proxy must know where the **real linker executable** is (PC **`link.exe`**, or **SN Linker** from your PS4 SDK / whatever your MSVC-integrated console project invokes). Resolution order:

1. **`/lorig:<path>`** on the command line (optional; stripped and **not** passed to the real linker).
2. Otherwise the environment variable **`LINK_EXPORT_ALL_LINKER`**, set to that executable's full path (optional quotes; trimmed).

If you set **`LINK_EXPORT_ALL_LINKER`** once in your environment or build machine, you do not need **`/lorig:`** at all.

Workflow (simplified):

1. Pass your usual linker arguments, plus **`/DEFGEN`**, **`/DEF:<path\to\exports.def>`** (typical PC **COFF** objects), **or** an **`.emd`** path plus **`/DEFGEN`** when the PS4 toolchain supplies **ELF** `.o` files and an EMD export list.
2. The tool collects object paths (and optional **`.olst`** response lists), regenerates the export file when inputs are newer, then runs the resolved **real linker** with the remaining arguments.

Optional **`DefBuildIgnores.txt`** in the **current working directory**: one substring per line; export names containing that substring are skipped (same behavior as the legacy tool).

Example (environment variable set to `link.exe`; no `/lorig:`):

```bat
set LINK_EXPORT_ALL_LINKER=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\bin\Hostx64\x64\link.exe
link-export-all.exe /DEFGEN /DEF:myexports.def /DLL ... *.obj
```

Example with explicit override:

```bat
link-export-all.exe /lorig:"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\bin\Hostx64\x64\link.exe" ^
  /DEFGEN /DEF:myexports.def /DLL ... *.obj
```

PS4 (same proxy on Windows; set the path to **SN Linker** from your SDK / Visual Studio integration; the executable name and folder vary by SDK):

```bat
set LINK_EXPORT_ALL_LINKER=C:\Path\From\Your\SCE\PS4\Toolchain\bin\sn.exe
link-export-all.exe /DEFGEN ... player.emd ... *.o
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
// r.out.lines - write to a .def file
```

## Limitations

- **Heuristics**, not a formal "every symbol in the universe" guarantee: COMDAT handling, name filtering (`??`, `__real`, etc.), and **functions vs. data** mirror the legacy implementation (data exports are still not emitted in the COFF `.def` path).
- **Proxy is Windows-only**; the **`defgen`** library is intended to stay **portable** for parsing and testing.

## License

See `LICENSE`.
