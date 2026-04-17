# Modernization plan

**Status:** The **CMake-based split** (`defgen` library + `link-export-all` proxy) is implemented at the repo root; legacy `AT_Linker/` / `CoffParser/` MSBuild projects are retained for reference only.

---

This document captures the agreed direction for refactoring the repository: a **cross-platform library** that turns object files into module-definition (`.def`) content, plus a **Windows-only proxy** that wraps `link.exe` and implements an **export-all-style workflow** for MSVC—conceptually similar to what GCC/Clang users achieve with linker flags around exporting symbols from shared objects, which MSVC does not offer as a single `link.exe` switch.

---

## Problem statement

**MSVC’s `link.exe` has no equivalent to a single “export every public symbol from these objects into the DLL” option.** Projects that need a DLL exposing a large, evolving surface historically either maintain a `.def` by hand, or scatter `__declspec(dllexport)` (or macros) across the codebase.

**Annotating exports by hand does not scale.** For a greenfield DLL you might place export attributes on a bounded API. For an **existing** codebase—especially one with **millions of lines** and years of history—**manually exporting everything the linker could expose is not realistic**: the symbol set is huge, spans countless translation units, and changes with every build. Automation (a generated `.def` or similar) is the practical approach.

On **ELF** toolchains, people often discuss this in terms of **default visibility**, **version scripts**, and flags such as **`-Wl,--export-dynamic`** (for executables) or building shared objects with broad visibility—the details differ by platform, but the **intent** is familiar: avoid naming every export by hand. This project targets the **MSVC** side by **generating a `.def`** and invoking the real linker.

---

## Goals

1. **Split the codebase** into two clear deliverables (see below).
2. **Build with CMake** on Windows (MSVC), Debug/Release, x86 and x64; library target should also build on other platforms for tests and reuse.
3. **Rename and rebrand** for generic use (no legacy internal codenames in public-facing names); documentation should explain purpose using **GCC/Clang / “export all”** terminology where it helps readers.
4. **Clean up code**: **C++20**, modular boundaries, fix known bugs (quoted paths, `-o` iterator, narrow paths, `DefIsChanged` loop).
5. **Document** build steps, CLI flags, architecture, limitations, and the MSVC vs GCC/Clang comparison honestly.

---

## Architecture: two projects

### 1. Cross-platform library (objects → `.def` content)

**Responsibility**

- Accept an ordered list of paths to **COFF** (MSVC `.obj`) and/or **ELF** (e.g. `.o`) object files.
- Parse each file, extract exportable symbols (same semantics as today, documented).
- Merge, deduplicate, apply ignore rules (passed in by the caller; avoid hidden CWD-relative config inside the core if possible).
- Emit **`.def` text** (lines or string), suitable for passing to MSVC `link.exe` `/DEF:`.

**Out of scope for this library**

- Windows process APIs, `link.exe`, response files, temp files, `CreateProcess`.
- Incremental “skip regen” using `FILETIME` (optional: portable `std::filesystem::last_write_time` helper in the library *or* keep incremental logic entirely in the proxy).

**Implementation notes**

- **C++20** for both the library and the proxy (MSVC and other toolchains used in CI). Use modern features where they **improve clarity and maintainability**, not for novelty: e.g. `std::span` for buffer views, **concepts** to document parser/stream constraints, **`std::optional` / `std::variant`** where they model real absence or alternatives, **ranges** (`std::ranges::`, views) for filtering/sorting symbol lists, **`std::filesystem`** throughout, and **`std::expected`** (C++23 on some compilers—or a small polyfill / `Result`-style type) for parse results if the chosen toolchain supports it; otherwise a clear `enum class` error + `std::string` message is fine.
- Prefer standard library over ad-hoc helpers; no `hash_map` / `for each` / mandatory PCH.
- Paths: `std::filesystem::path`; on Windows, do not assume narrow paths are valid—use proper wide/UTF-8 handling for file open.
- Public API shape (illustrative): options struct, `ObjectFormat::Auto|Coff|Elf`, function returning lines or a typed result type.

### 2. Windows-only proxy linker

**Responsibility**

- Pretend to be the toolchain linker: parse `argv`, `@response`, custom switches (`/DEF:`, `/DEFGEN`, `/lorig:`, `.olst`, `-o`, `.emd`, etc.).
- Build the object-file list as today.
- Call the **library** to obtain `.def` content; write/update the `.def` file (including optional `ObjectCount` / incremental behavior if retained).
- Merge arguments, write temp response file if needed, **`CreateProcess`** the real `link.exe`.
- Load optional ignore files (e.g. `DefBuildIgnores.txt`) **here** and pass patterns into the library.

**Dependencies**

- Links only the library for symbol→DEF generation; everything else is Win32 + existing routing logic.

---

## CMake layout

- Root `CMakeLists.txt`: project name (see naming), **`CXX_STANDARD 20`**, `CXX_STANDARD_REQUIRED ON`, subdirectories.
- **`exportall_def`** (name TBD): `STATIC` library, cross-platform sources only.
- **`exportall_link`** or **`msvc_link_proxy`** (name TBD): `WIN32` or console executable, Windows-only, links `Shlwapi` (or remove those APIs over time).
- Options: static CRT (`/MT` vs `/MD`) as a cache option to match production needs; optional `BUILD_PROXY_LINKER=OFF` on non-Windows CI to build/test library only.
- **Do not** replicate legacy vcxproj post-build copies to paths outside the repo unless behind an explicit `cmake -D...` hook.

---

## Naming (to be finalized)

Pick one consistent name for the repository, CMake targets, and the proxy binary. Examples:

| Role | Example names |
|------|----------------|
| Library | `exportall_def`, `obj2def`, `msvc_defgen` |
| Proxy exe | `export-all-link`, `link-export-all`, `msvc-link-proxy` |

Avoid legacy “AT_” / team-specific branding in **public** names unless kept as historical note in CHANGELOG.

---

## Migration steps (phased)

### Phase 0 — Blockers

- Restore or implement **`Streams`** (or equivalent) if missing from the tree; the current vcxproj references `Streams.cpp` / `Streams.h`.
- Inventory **`ReferenceGen`**: optional separate tool/target if still needed; not required for minimal DEF generation.

### Phase 1 — CMake + split

- Add CMake; make the **library** and **proxy** targets match the split above.
- Move parser/object code into the library; move `main` and Win32 glue into the proxy.

### Phase 2 — Code quality

- Remove `stdafx` precompiled headers or make optional.
- Replace MSVC-only extensions; namespaces (`atlinker::` / `exportall::` TBD); adopt **C++20** idioms where they simplify (see Implementation notes above).
- Fix known issues: quoted `.obj` path handling, `-o` iterator bounds, wide→narrow path handling in DEF generation, `DefIsChanged` EOF handling.

### Phase 3 — Documentation

- **`README.md`**: one-liner, problem, **GCC/Clang comparison subsection**, how it works, limitations, build instructions.
- Optional **`docs/USAGE.md`**: full switch table for the proxy.
- Optional **`docs/ARCHITECTURE.md`**: data flow diagram (argv → objects → DEF → `link.exe`).
- Sample ignore file: `DefBuildIgnores.example.txt`.

### Phase 4 — Optional

- CI (e.g. GitHub Actions): CMake + MSVC x64 Release.
- Centralize version string (CMake `project(VERSION)` + generated header or single source).

---

## Documentation content to include

- **Goal statement**: Add an **export-all-style** workflow for MSVC (inspired by ELF/GCC discussions of broad export without listing every symbol); **not** a patch to Microsoft’s `link.exe`.
- **Comparison**: Brief, accurate note on GCC/Clang/ELF (visibility, linker flags); clarify MSVC uses `.def` + `link.exe`.
- **Large codebases**: Explicit sentence that **manual `__declspec(dllexport)` across millions of lines is impractical**; generated `.def` from object files is the scalable approach.

---

## Out of scope (unless requested later)

- Non-Windows behavior for the **proxy** (by definition Windows-only).
- Replacing the COFF reader with **LLVM**—possible future work; larger dependency.
- Re-enabling **data** exports in COFF output (currently commented out)—behavior change; decide product-wise before flipping.

---

## Risks

- **Subtle linker/DEF behavior**—validate against real pipelines after refactors.
- **CRT static vs dynamic**—must match how the tool is deployed with other static libs.
- **Symbol heuristics** (COMDAT, mangling, filters) remain documented assumptions, not “every symbol in the mathematical sense.”

---

## Legacy IDE

- Keep Visual Studio solution/projects **optional** during transition, or remove once CMake is verified; **CMake is the canonical build** going forward.
