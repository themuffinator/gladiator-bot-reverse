# Agent Instructions

This repository exists to faithfully reconstruct the Gladiator Bot's botlib (gladiator.dll) for Quake II using the Binary Ninja HLIL references as an accurate guide to the retail code base; every change should focus on accurately reconstructing the original Gladiator botlib codebase piece by piece using the HLIL references, and Gladiator Bot's successor - Quake III Arena bot.

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
- **Read-only access to the `dev_tools/` directory tree.**
