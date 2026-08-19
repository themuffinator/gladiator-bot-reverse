# Agent Instructions

This repository is the **Q2 Gladiator Bot Botlib Reconstruction**. It exists to faithfully reconstruct the Gladiator Bot's botlib (gladiator.dll) for Quake II using the Binary Ninja HLIL references as an accurate guide to the retail code base; every change should focus on accurately reconstructing the original Gladiator botlib codebase piece by piece using the HLIL references, and Gladiator Bot's successor - Quake III Arena bot.

The work is a tribute to Mr Elusive (Jan Paul van Waveren), who designed the original. Documentation should reflect that.

This repository currently has the following rules for agents:

- Obey system, developer, and user instructions, and ensure AGENTS.md scope rules are followed.
- Use the latest AI model available and announce the model at the start of the first response each session.
- In C/C++ code, indent with tabs, and include the required commented header format above every function definition.
- Avoid creating binary files (including standard images, models, and similar assets).
- Avoid creating new source files for trivially small code additions unless they are expected to grow.
- When tasks are large, break them into smaller tasks automatically.
- Do not make significant decisions based on assumptions; ask questions if needed.
- Prefer `rg` instead of `ls -R` or `grep -R` for repository searches.
- After committing changes, generate a pull request message using the `make_pr` tool.
- For each task completion, estimate before and after parity percentages versus the original Gladiator botlib source base outlined in the Binary Ninja HLIL references.
- Never change the legacy botlib version. `BotVersion()` returns `"BotLib v0.96"` and `BotSetupLibrary` prints it in the startup banner; both are pinned by address in `tests/reference/botlib_contract.json`. The reconstruction's own version lives in `cmake/ReconstructionVersion.cmake` — bump that instead. See `docs/reconstruction_versioning.md`.
- **Read-only access to the `dev_tools/` directory tree.**
