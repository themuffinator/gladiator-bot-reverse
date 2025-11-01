# Engine Log Parity Catalogue

This directory stores the reference console output extracted from the
headless Quake II parity harness.  Each entry mirrors an HLIL trace captured
from the original Gladiator DLL so that automated runs can diff the rebuilt
botlib against the historical diagnostics.

* `catalog.json` &mdash; Structured expectations indexed by scenario name.
  Each scenario records the startup banner, chat markers, map loading
  confirmations, and diagnostic payloads observed in the HLIL.
* `samples/` &mdash; Example captures used by the harness unit tests or to
  bootstrap new recordings.  These files are optional but serve as a quick
  smoke-test reference when exercising the comparison logic locally.

When the upstream binary changes (for example, the reconstructed botlib emits
updated diagnostics), re-run the headless harness with `engine_log_harness.py
refresh ...` to regenerate the affected scenario block.  The helper converts
captured console output and demo metadata into the canonical JSON format used
by the regression suite.

Quick usage examples:

```sh
# Diff a new capture against the dm1_headless_smoke expectations
python tests/parity/engine_log_harness.py compare dm1_headless_smoke \
  --console-log /path/to/console.log \
  --metadata /path/to/demo.json

# Refresh the catalogue entry with the latest capture
python tests/parity/engine_log_harness.py refresh dm1_headless_smoke \
  --console-log /path/to/console.log \
  --metadata /path/to/demo.json
```
