# Engine Parity Harness

This directory contains a lightweight harness for running a Quake II dedicated
server against the reconstructed Gladiator bot library. The `run_engine_parity.py`
script stages the built `gladiator.dll` into a throw-away game tree, boots a
server with `fs_game gladiator`, and executes a scripted startup sequence defined
in `startup.cfg.in`.

## Quick start

```
python3 tests/engine_parity/run_engine_parity.py \
    --server /path/to/yquake2-dedicated \
    --dll-path build/gladiator.dll \
    --demo-name engine_parity_capture
```

Artifacts (console log, qconsole log, and optional demos) are written to
`tests/engine_parity/artifacts/`. The harness exits with a non-zero status when
the server fails to load the bot library or terminates unexpectedly.
