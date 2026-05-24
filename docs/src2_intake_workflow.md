# `src2/` Ghidra Intake Workflow

`src2/` is the staging area for a fresh Ghidra C export of `gladiator.dll`.
Keep it outside production CMake targets until the raw export has been named,
sanity-scanned, and split along the existing botlib subsystem boundaries.

## Current Baseline

As of the initial intake round:

- The raw Ghidra export has not been imported yet.
- `src2/maps/contract_from_hlil.json` can be regenerated from the existing HLIL
  extractor without modifying `dev_tools/`.
- `src2/maps/address_to_name.csv` seeds direct `FUN_` and selected `DAT_`
  replacements from the HLIL contract.
- `src2/maps/external_symbols.csv` stores reviewed symbols imported from
  external linker/debug artifacts such as MSVC `.map` files or `llvm-nm` text.
- `src2/maps/symbol_overrides.csv` stores reviewed address-backed additions or
  field overrides for symbols that should not be edited into generated maps.
- `src2/maps/abi_inventory.json` records header-derived ABI drift between the
  original 0.96 header, current bridge header, and internal shim header.
- `src2/maps/export_aliases.csv` records the public export-table names that
  correspond to address-backed implementation names.
- `src2/maps/module_owners.json` records the reviewed production owner,
  existing CMake target, and CMake file for each staged module shard.
- `src2/maps/symbol_catalog.json` merges addresses, replacement tokens, public
  aliases, promotion status, and first-pass module ownership.
- `src2/staging/include/ghidra_type_seed_manifest.json` lists separate Ghidra
  parse groups for original public ABI, current bridge ABI, and current
  internal module types.
- `src2/staging/include/ghidra_compat.h` provides temporary placeholder types
  for raw Ghidra output.

`src2/maps/contract_from_hlil.json` is extractor-only. The checked-in
`tests/reference/botlib_contract.json` can also contain test-harness bridge
diagnostics that are not produced from HLIL, so do not overwrite it with the
staged copy.

`src2/maps/abi_inventory.json` is generated from headers only. It identifies
drift and ordering risks, but it is not proof of binary layout.

## Intake Commands

Run the standard staged pipeline:

```powershell
python src2/scripts/run_intake_pipeline.py
```

This regenerates the contract copy, address map, ABI inventory, Ghidra type
seed bundle, export aliases, symbol catalog, module manifest, and map validation
outputs in dependency order. The symbol catalog also reads
`external_symbols.csv` when it contains reviewed linker/debug evidence. With no
raw export present, the pipeline intentionally stops before symbol replacement
and reports raw-export-pending warnings.

When a fresh Ghidra export is available, register it and run strict intake:

```powershell
python src2/scripts/run_intake_pipeline.py `
  --raw-c C:\path\gladiator_raw.c `
  --raw-h C:\path\gladiator_raw.h `
  --require-raw-export
```

That copies the raw text exports into
`src2/ghidra_raw/2026-05-24/`, writes `export_manifest.json`, applies symbols,
generates `src2/staging/raw_export_inventory.json`, writes
`src2/staging/include/ghidra_inventory.h` plus
`src2/staging/ghidra_header_inventory.json`, scans the sanitized monolith, runs
a staged syntax check, writes staged module shards under `src2/staging/modules/`,
writes `src2/staging/promotion_readiness_report.json`, and then runs strict
validation.

## Ghidra Pre-Export Type Seeds

Before exporting C from Ghidra, generate the seed bundle and parse the wrappers
listed in `src2/staging/include/ghidra_type_seed_manifest.json`. Parse each
group separately:

- `original_public_abi`: original Quake II shared types plus bundled Gladiator
  0.96 `botlib.h`; use this when adjudicating binary-facing layouts.
- `current_bridge_abi`: current `src/q2bridge/botlib.h` and export wrapper
  declarations; use this for repo-facing bridge names.
- `current_internal_modules`: reconstructed module-local headers; use this as
  secondary type/naming context after public ABI types are settled.

Do not combine seed groups blindly. Several botlib typedef names intentionally
overlap while the ABI is still being reconciled.

The individual commands are still useful for debugging. Regenerate the contract
copy from the read-only HLIL source:

```powershell
python dev_tools/extract_botlib_contract.py --output src2/maps/contract_from_hlil.json
```

Regenerate the deterministic seed map:

```powershell
python src2/scripts/generate_address_map.py --output src2/maps/address_to_name.csv
```

Import optional external symbols:

```powershell
python src2/scripts/import_external_symbols.py C:\path\gladiator.map `
  --format msvc-map `
  --output src2/maps/external_symbols.csv `
  --report src2/maps/external_symbols_report.json
```

The importer also accepts normalized CSV input and `llvm-nm`-style text. Leave
`external_symbols.csv` as header-only when no external symbol artifact is
available. External rows must agree with the HLIL/contract address map for any
shared address; use `symbol_overrides.csv` only after a mismatch is reviewed.

Regenerate the header-derived ABI inventory:

```powershell
python src2/scripts/generate_abi_inventory.py --output src2/maps/abi_inventory.json
```

Regenerate the Ghidra type seed wrappers:

```powershell
python src2/scripts/generate_type_seed_bundle.py --output-dir src2/staging/include
```

Regenerate the public export alias map:

```powershell
python src2/scripts/generate_export_aliases.py --output src2/maps/export_aliases.csv
```

Regenerate the combined symbol catalog:

```powershell
python src2/scripts/generate_symbol_catalog.py `
  --external-symbols src2/maps/external_symbols.csv `
  --symbol-overrides src2/maps/symbol_overrides.csv `
  --output src2/maps/symbol_catalog.json
```

`symbol_catalog.json` is the effective map used by the raw symbol-application
step. `address_to_name.csv` remains the generated HLIL/contract seed map, while
`external_symbols.csv` adds reviewed linker/debug evidence and
`symbol_overrides.csv` is the reviewed place for additions, module corrections,
or promotion-status corrections.

Review staged module owners:

```powershell
python src2/scripts/validate_intake.py
```

`src2/maps/module_owners.json` is intentionally hand-reviewed data. Update it
when a new staged module owner is accepted, not as a generated side effect.

Regenerate the split-planning manifest:

```powershell
python src2/scripts/split_export.py `
  --catalog src2/maps/symbol_catalog.json `
  --output src2/staging/module_manifest.json
```

Validate staged intake maps and guardrails:

```powershell
python src2/scripts/validate_intake.py
```

The validator also enforces the quarantine boundary: production CMake files must
not reference `src2/`, raw/sanitized Ghidra artifacts must not appear under
`src/`, and Python bytecode/cache files must not be produced under `src2/` or
`dev_tools/`.

After placing a fresh export at
`src2/ghidra_raw/2026-05-24/gladiator_raw.c`, inventory the raw tokens:

```powershell
python src2/scripts/analyze_raw_export.py `
  --raw-c src2/ghidra_raw/2026-05-24/gladiator_raw.c `
  --raw-h src2/ghidra_raw/2026-05-24/gladiator_raw.h `
  --catalog src2/maps/symbol_catalog.json `
  --output src2/staging/raw_export_inventory.json
```

Then apply known names:

```powershell
python src2/scripts/apply_symbols.py `
  src2/ghidra_raw/2026-05-24/gladiator_raw.c `
  src2/maps/symbol_catalog.json `
  -o src2/staging/gladiator_sanitized.c `
  --report src2/staging/apply_symbols_report.json
```

Scan the sanitized monolith:

```powershell
python src2/scripts/sanity_scan.py --fail-on-fun-dat src2/staging/gladiator_sanitized.c
```

Syntax-check the sanitized monolith as one quarantined translation unit:

```powershell
python src2/scripts/check_monolith_compile.py `
  src2/staging/gladiator_sanitized.c `
  --output src2/staging/monolith_compile_report.json
```

The compile check force-includes `src2/staging/include/ghidra_compat.h` and
auto-discovers `CC`, `clang`, `clang-cl`, `gcc`, or `cl`. If no compiler is
available, it writes a `skipped_no_compiler` report so the missing environment
dependency is visible without wiring `src2/` into production CMake.

Generate first-pass staging artifacts:

```powershell
python src2/scripts/generate_headers.py `
  src2/staging/gladiator_sanitized.c `
  --output src2/staging/include/ghidra_inventory.h `
  --inventory-output src2/staging/ghidra_header_inventory.json
python src2/scripts/split_export.py `
  --catalog src2/maps/symbol_catalog.json `
  --output src2/staging/module_manifest.json `
  --input-c src2/staging/gladiator_sanitized.c `
  --module-output-dir src2/staging/modules `
  --shard-report src2/staging/module_split_report.json
python src2/scripts/generate_promotion_readiness.py `
  --split-report src2/staging/module_split_report.json `
  --module-manifest src2/staging/module_manifest.json `
  --abi-inventory src2/maps/abi_inventory.json `
  --module-owners src2/maps/module_owners.json `
  --output src2/staging/promotion_readiness_report.json
```

`ghidra_inventory.h` is a staging-only private declaration header. The JSON
inventory is the review ledger: it records `#define` macros, top-level type
declarations, externs, globals, function prototypes, and function definitions
observed in the sanitized export.

`src2/staging/modules/*.c` are generated review shards only. They are not
production files and must not be added to CMake until each shard is manually
reviewed, reconciled against the ABI inventory, and moved through the normal
source-audit flow.

`promotion_readiness_report.json` applies the promotion criteria below to the
generated shards. A `blocked` report is expected for early raw exports; it means
the staged shards are not promotable yet, not that raw intake itself failed.

## Guardrails

- Do not write into `dev_tools/`; treat it as read-only source evidence.
- Do not place raw export files directly under `src/`; the source audit is
  intentionally strict and should remain strict.
- Do not add `src2/` to production CMake targets.
- Leave `LAB_` labels unmapped unless a label-specific control-flow decision is
  reviewed. Function and data symbols are safer first-pass replacements.
- Keep raw export files unedited. Put every derived artifact under
  `src2/staging/`.
- Keep raw/sanitized Ghidra C outputs out of production `src/` until each shard
  is reviewed, split, and registered through the normal source-audit flow.

## Promotion Criteria

Before any staged code moves into `src/`, it should have:

1. A reviewed ABI classification in `docs/abi_reconciliation.md`.
2. Address-backed names in `src2/maps/address_to_name.csv`,
   `src2/maps/external_symbols.csv`, or a reviewed entry in
   `src2/maps/symbol_overrides.csv`.
3. No unresolved merge-conflict markers.
4. No unexpected `FUN_`, `DAT_`, or `SUB_` tokens in promoted code.
5. A subsystem owner matching `src2/maps/module_owners.json`.
6. CMake registration through the existing source-audit flow.
