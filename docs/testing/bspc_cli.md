# BSPC CLI regression checks

The reconstructed `bspc` binary ships with placeholder pipelines that mirror
the control-flow of the historical tool. The script at
`tools/bspc/run_cli_modes.py` exercises every CLI mode against lightweight
fixtures so that automated pipelines can detect argument parsing or filesystem
regressions quickly.

## Running the smoke test

1. Build the `bspc` executable (`cmake --build build --target bspc`).
2. Execute the harness:

   ```bash
   python tools/bspc/run_cli_modes.py --bspc /path/to/build/tools/bspc/bspc
   ```

   The script emits a JSON summary that records the command line for each mode,
the detected timing messages, and the artifacts that were validated. The
workspace defaults to `build/test-output/bspc_cli/` and is recreated for every
run.

### Expected outputs and timing summaries

Each mode that performs compilation asserts that stdout includes a message
matching the legacy `"%5.0f seconds elapsed"` format. The generated
artifacts are compared byte-for-byte against golden baselines stored under
`tests/support/assets/bspc/golden/`:

| Mode     | Inputs                                     | Required outputs |
|----------|--------------------------------------------|------------------|
| map2bsp  | `tests/support/assets/bspc/simple_room.map` | `.bsp`, `.prt`, `.lin` |
| map2aas  | `tests/support/assets/bspc/simple_room.map` | `.aas`, `.bsp`, `.prt`, `.lin` |
| bsp2map  | `dev_tools/assets/maps/2box4.bsp`           | No files (logging only) |
| bsp2bsp  | `dev_tools/assets/maps/2box4.bsp`           | `.bsp`, `.prt`, `.lin` |
| bsp2aas  | `dev_tools/assets/maps/2box4.bsp`           | `.aas` |

Text artifacts normalise newline and path separators to keep the diff friendly
across platforms. When a mismatch is detected, a unified diff is written next
to the generated artifact (for example,
`build/test-output/bspc_cli/map2bsp/simple_room.prt.diff`).

## Comparing outputs to golden baselines

The smoke test now performs all baseline comparisons automatically. Binary
artifacts are stored as Base64 payloads (for example,
`tests/support/assets/bspc/golden/map2bsp/simple_room.bsp.base64`) so that the
repository does not need to ship literal `.bsp`/`.aas` blobs; the harness
decodes these payloads before performing byte-for-byte comparisons. The JSON
report emitted via `--json` can still be consumed by downstream tooling to
summarise which artifacts were validated, but manual `diff`/`cmp` runs are no
longer necessary unless you need additional diagnostics.
