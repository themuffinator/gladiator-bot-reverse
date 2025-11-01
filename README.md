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

- Mr Elusive for creating the Gladiator Bot and the Quake III Botlib.
- id Software for Quake II and Quake III Arena.
