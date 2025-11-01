# Headless Quake II parity check

The headless parity harness boots a Quake II dedicated server against the
rebuilt Gladiator bot library to verify that the bridge layer remains
compatible with the original mod.  The check is optional and tagged as a
long-running CTest so CI can run it on a separate cadence (nightly or
on-demand).

## Prerequisites

* **Quake II dedicated server build** – Provide a legal copy of the Quake II
  dedicated server binaries and `baseq2` assets.  The harness expects the
  directory layout produced by the official installers: a root folder containing
  `baseq2/` plus the dedicated server executable (`q2ded`, `quake2`, or
  `quake2.exe`).
* **Rebuilt Gladiator module** – Configure the project and build the shared
  library target:

  ```bash
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
  cmake --build build --target gladiator --parallel
  ```

  The build emits `gladiator.dll` on Windows and `libgladiator.so` on Unix-like
  platforms under `build/`.  The harness copies whichever file is provided into
  the Quake II install tree before launching the server.
* **Python 3.8+** – Required for the harness launcher script
  (`tests/headless/run_headless_parity.py`).

## Environment variables

The CTest entry and launcher rely on the following environment variables.  The
CI workflow wires them automatically, but local runs must export them before
invoking the harness.

| Variable | Required | Description |
| --- | --- | --- |
| `GLADIATOR_Q2_DEDICATED_SERVER` | ✅ | Absolute path to the Quake II dedicated server executable (`q2ded`, `quake2`, etc.). |
| `GLADIATOR_Q2_BASEDIR` | ✅ | Directory that contains the `baseq2/` game assets. |
| `GLADIATOR_Q2_MODULE_PATH` | ✅ | Path to the rebuilt Gladiator shared library.  CTest injects `$<TARGET_FILE:gladiator>` automatically after a successful build. |
| `GLADIATOR_Q2_CAPTURE_DIR` | ⛔ | Optional destination for harness logs and generated configs.  Defaults to `headless-parity-captures/` inside the build directory. |
| `GLADIATOR_Q2_PARITY_CFG` | ⛔ | Optional override for the config executed by the dedicated server.  When unset the harness writes a minimal config that starts `q2dm1`, enables bots, and quits after the map loads. |
| `GLADIATOR_Q2_MOD_DIR` | ⛔ | Optional directory inside the Quake II install tree where the Gladiator module should be staged.  Defaults to `<basedir>/gladiator/`. |
| `GLADIATOR_Q2_EXTRA_ARGS` | ⛔ | Additional command-line tokens appended to the dedicated server launch (useful for debug flags). |
| `GLADIATOR_Q2_TIMEOUT` | ⛔ | Timeout in seconds before the harness aborts the dedicated server (default `180`). |

The launcher exits with status code `125` when any required variable is missing
or when the assets cannot be found.  CTest treats that status as a skipped test
so developers can opt in once their environment is prepared.

## Running the harness locally

1. Stage the Quake II dedicated server and assets somewhere accessible, e.g.:

   ```bash
   export Q2_ROOT="$HOME/games/quake2"
   ```

2. Configure and build the project:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
   cmake --build build --target gladiator --parallel
   ```

3. Export the environment variables and run the tagged CTest:

   ```bash
   export GLADIATOR_Q2_DEDICATED_SERVER="$Q2_ROOT/q2ded"
   export GLADIATOR_Q2_BASEDIR="$Q2_ROOT"
   # CTest injects GLADIATOR_Q2_MODULE_PATH, but you can override it explicitly:
   export GLADIATOR_Q2_MODULE_PATH="$(realpath build/libgladiator.so)"
   export GLADIATOR_Q2_CAPTURE_DIR="$(realpath build/headless-parity-captures)"

   ctest --test-dir build -R headless_quake2_parity --output-on-failure
   ```

   The harness writes console output to
   `build/headless-parity-captures/dedicated.log` and prints the location on
   completion.  Update the environment variables if your binary is named
   differently (e.g., `quake2.exe` on Windows).

4. Inspect the generated log to compare behaviour against reference parity
   runs.  The default config loads `q2dm1`, enables bots, and issues a `quit`
   command after a short delay.  Replace `GLADIATOR_Q2_PARITY_CFG` with a custom
   script if your parity scenario requires more elaborate steps.

## CI integration

The GitHub Actions workflow defines a dedicated `headless-quake2-parity` job.
It runs only during scheduled or manually dispatched workflows to keep the
regular PR jobs lightweight.  Operators must supply a download URL for the
Quake II dedicated server archive via either:

* the `quake2_dedicated_url` input when starting a `workflow_dispatch` run, or
* the `QUAKE2_DEDICATED_URL` repository/organisation secret for nightly runs.

When the URL is provided, the job downloads the archive, extracts it, and runs
`ctest -R headless_quake2_parity`.  Failures capture the harness log and
standard CTest diagnostics as artifacts named `headless-quake2-artifacts`,
mirroring the layout described above.  If the URL (or assets) are missing, the
job reports a skipped status so the workflow still succeeds.

To reproduce the CI job locally, run the same CTest command after exporting the
required environment variables and pointing them at your Quake II install.
