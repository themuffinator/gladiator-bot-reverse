# gladiator-bot-reverse

DLL reverse-engineering effort for Q2 Gladiator Bot.

## Project Purpose

This project aims to reconstruct the Gladiator bot library (botlib) so that it
remains compatible with the original Gladiator mod for Quake II. The goal is to
recreate the original functionality and behavior while preserving support for
the original Area Awareness System (AAS) file format used by the mod. By
maintaining format compatibility, the reconstructed botlib can be used as a
drop-in replacement in environments that expect the original Gladiator assets.

## Building

### Quick start

If you just want a working `gladiator` binary, run the bootstrap helper:

```bash
python dev_tools/bootstrap_cmake.py
```

The script creates the `build/` directory, configures CMake, and then invokes
`cmake --build`. Use it any time you encounter the `missing CMakeCache.txt`
error after cloning the repository on a new machine. 【F:dev_tools/bootstrap_cmake.py†L1-L189】

### Detailed steps

The reconstructed bot library is published as a shared module named
`gladiator`/`gladiator.dll`.  CMake drives the build across every supported
platform; the steps below mirror the CI configuration so the resulting binaries
match the artifacts we ship.

#### Prerequisites

| Platform | Toolchain | Notes |
| --- | --- | --- |
| Linux | GCC 11+ or Clang 14+, Ninja, `build-essential`/`libstdc++-dev` | Install with your package manager, e.g. `sudo apt-get install build-essential ninja-build`. |
| Windows | Visual Studio 2022 (Build Tools or full IDE) with the "Desktop development with C++" workload, Ninja | Run the "x64 Native Tools Command Prompt" (or call `vcvarsall.bat`) before configuring so MSVC is on `PATH`. |
| macOS | Xcode command-line tools, Ninja | Install with `xcode-select --install`. |

All platforms require CMake 3.16 or newer and Python 3.8+ (for tooling and
tests).  The build system always enables position-independent code so static
libraries can be linked into the shared `gladiator` module without relinking. 【F:CMakeLists.txt†L1-L28】

#### Configure and build the module

1. Create a build tree for the desired configuration.  Use Ninja to mirror the
   CI jobs and keep build scripts identical on every platform.

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   ```

   On Windows invoke the command from an MSVC developer prompt.  To generate
   Debug binaries swap `-DCMAKE_BUILD_TYPE=Release` for `Debug`.

   CMake writes the configuration artifacts, including
   `build/CMakeCache.txt`, into the `build/` directory during this step.  The
   cache file will not exist until you run the configure command at least once
   in a fresh clone.

2. Compile just the shared library (this avoids linking the optional test
   executables unless you request them explicitly):

   ```bash
   cmake --build build --target gladiator
   ```

   The target emits `libgladiator.so`/`gladiator.dll` under `build/` alongside
   static archives for each subsystem.

   If you accidentally skip the configuration step, CMake will emit
   `Error: not a CMake build directory (missing CMakeCache.txt)` when you try to
   build.  Re-run step 1 or use the automated bootstrap helper:

   ```bash
   python dev_tools/bootstrap_cmake.py
   ```

   The script configures the default build tree when required and then invokes
   the `gladiator` target for you. 【F:dev_tools/bootstrap_cmake.py†L1-L181】

3. (Optional) Install the runtime into a staging directory that mimics your
   game or mod layout:

   ```bash
   cmake --install build --prefix /path/to/gladiator/install
   ```

   The install step drops the shared library and the generated CMake export file
   into the prefix so downstream projects can consume the reconstructed botlib
   via `find_package(gladiator CONFIG)`. 【F:CMakeLists.txt†L18-L46】

## Testing

Optional parity validation can be driven through CTest.  See the
[Headless Quake II parity check](docs/testing/headless_quake2_parity_check.md)
guide for environment setup, CI integration notes, and instructions on running
the long-running harness locally.

To build the automated parity tests locally you must enable the cmocka-backed
fixtures when configuring the tree:

```bash
cmake -S . -B build -G Ninja \
      -DBUILD_TESTING=ON \
      -DBOTLIB_PARITY_ENABLE_SOURCES=ON \
      -DBOTLIB_PARITY_FRAMEWORK=cmocka
cmake --build build --target botlib_parity_tests
```

The FetchContent script retrieves cmocka from GitHub so ensure the host has
outbound HTTPS access.  If the dependency cannot be fetched, re-run CMake with
`-DBOTLIB_PARITY_FRAMEWORK=none` to skip the cmocka targets and still build the
core library. 【F:tests/CMakeLists.txt†L7-L33】

## Asset packaging workflow

The original Gladiator release shipped its data inside numbered `pak` archives.
To reproduce that layout, this repository provides a `dist/` tree that mirrors
the classic mod directory hierarchy (`bots/`, `maps/`, `sounds/`, etc.). Drop
updated assets into the matching folders under `dist/gladiator/` before
building an archive.

Generate the distributable archives with the packaging helper:

```bash
python dev_tools/package_assets.py            # uses dist/pak_manifest.json
```

The default manifest creates a legacy-compatible `pak7.pak` next to the
`dist/` tree. Additional packages can be declared in `dist/pak_manifest.json`
if you need to split assets across multiple archives. Copy the generated
`pak*.pak` files alongside the rebuilt `gladiator.dll` (or `gladiator.so`) when
deploying the mod.

During development you can continue to override packaged content by pointing
the `GLADIATOR_ASSET_DIR` environment variable (or the `gladiator_asset_dir`
libvar) at a directory containing loose files. The runtime automatically
extracts bundled assets into a local `.pak_cache/` directory when the archives
are used, while still preferring any explicit override paths.

### Exported symbols

Projects that embed or extend the reconstructed botlib should include
`src/shared/platform_export.h` to access the `GLADIATOR_API` annotation used by
`GetBotAPI`. The macro maps to `__declspec(dllexport)` on Windows and uses
default ELF visibility on other platforms so the entry point remains visible to
dynamic loaders.

The CMake target now explicitly disables `WINDOWS_EXPORT_ALL_SYMBOLS`, meaning
additional public functions must use `GLADIATOR_API` (or a downstream override)
to be exported from the final `gladiator` DLL/SO. Consumers that previously
relied on the automatic export behaviour should update their declarations to
use the macro before upgrading.

## Credits

- Mr Elusive for creating the [Gladiator Bot for Quake II](https://mrelusive.com/oldprojects/gladiator/gladiator.html) and the [Quake III Arena bot library](https://github.com/id-Software/Quake-III-Arena/tree/master/code/botlib).
- id Software for Quake II and Quake III Arena.
