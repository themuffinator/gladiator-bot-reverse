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

The reconstructed bot library is published as a shared module named
`gladiator`/`gladiator.dll`. The project uses CMake for configuration and build
tool generation.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Optional: deploy the module for a game engine or mod install tree
cmake --install build --prefix /path/to/gladiator/install
```

The install step places the module and an export file under the configured
prefix so downstream engines can link or load the library directly.

## Testing

Optional parity validation can be driven through CTest.  See the
[Headless Quake II parity check](docs/testing/headless_quake2_parity_check.md)
guide for environment setup, CI integration notes, and instructions on running
the long-running harness locally.

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

- Mr Elusive for creating the Gladiator Bot and the Quake III Botlib.
- id Software for Quake II and Quake III Arena.
