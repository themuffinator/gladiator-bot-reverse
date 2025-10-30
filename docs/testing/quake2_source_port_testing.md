# Quake II Source Port + Rebuilt Botlib Side-by-Side Testing

This guide describes how to configure a Quake II source port to run against the rebuilt Gladiator botlib for functional parity comparisons. It also outlines the core gameplay scenarios to exercise and the tooling needed to collect evidence for regression tracking.

## 1. Test Environment Setup

### 1.1 Prerequisites
- **Operating system:** Windows 10+, macOS 12+, or a modern Linux distribution with SDL2 support.
- **Build toolchain:**
  - CMake 3.20+ and a C/C++ compiler supported by the chosen source port (MSVC, clang, or GCC).
  - Python 3.10+ for helper scripts and log processing.
- **Assets:** A licensed copy of *Quake II* (Steam or GOG) to provide the original `baseq2` data files.
- **Source code:**
  - Quake II source port (e.g., [Yamagi Quake II](https://www.yamagi.org/quake2/) or [KMQuake II](https://github.com/kmq2/kmquake2)).
  - Gladiator botlib rebuild artifacts from this repository (`build/botlib/libbotlib.a` or platform-equivalent shared library).

### 1.2 Directory Layout
```
~/quake2-testing/
├── source-port/          # Cloned Quake II source port
├── botlib/               # Rebuilt Gladiator botlib artifacts
├── baseq2/               # Retail game assets (pak0.pak, etc.)
└── demos/                # Automated demo recordings and logs
```

### 1.3 Building the Source Port
1. Clone and configure the chosen Quake II source port into `~/quake2-testing/source-port`.
2. Follow the port's README to build normally, verifying the binary launches with stock botlib disabled.
3. Copy the rebuilt Gladiator botlib into the port's expected location, replacing or overriding the bundled bot library:
   - **Static link:** Update the port's CMakeLists to point to `~/quake2-testing/botlib/libbotlib.a` and rebuild.
   - **Dynamic load:** Place the shared library (`botlib.dll`/`libbotlib.so`) in the same directory as the port executable or wherever the port looks for runtime bot modules.
4. Launch the port to ensure it successfully loads maps and the botlib without fatal errors.

### 1.4 Baseline Configuration
- Copy `baseq2` assets into the source port's game directory.
- Create a dedicated config file (`botlib_parity.cfg`) that standardizes:
  - `set deathmatch 1`
  - `set skill 2`
  - `set maxclients 16`
  - `set bot_enable 1`
- Place the config in the port's config path and auto-execute it via command line: `quake2 +set game baseq2 +exec botlib_parity.cfg`.

## 2. Key Test Scenarios

### 2.1 Map Loading
- Load canonical multiplayer arenas (`q2dm1`, `q2dm8`, and one custom map if supported).
- Record console output for map precache warnings, entity spawns, and botlib initialization lines.
- Capture load times via timestamped log entries to compare with reference builds.

### 2.2 Bot Spawning and Navigation
- Spawn a fixed roster of bots using `addbot` commands or scripted console aliases.
- Observe navigation mesh usage: confirm bots reach all major map zones, teleporters, and jump pads.
- Log waypoint acquisition messages and any navigation error printouts.

### 2.3 Combat Behaviors
- Start a timed deathmatch (10 minutes) with at least six bots.
- Monitor combat metrics: frag counts, weapon selection variety, and engagement frequency.
- Note anomalies such as bots failing to acquire weapons, stuck behavior, or aim instability.

## 3. Instrumentation and Logging Requirements

### 3.1 Console Logging Scripts
- Enable developer logging (`set developer 1`) and pipe console output to a file (`+set logfile 2`).
- Provide a wrapper script (`scripts/run_port.sh`) that launches the port with consistent arguments, logging to `logs/<timestamp>-console.log`.

### 3.2 Demo Recordings
- Use the port's `record <name>` command to capture first-person demos for each scenario:
  - `record map_load_q2dm1`
  - `record bot_spawn_suite`
  - `record combat_parity`
- Store demos under `demos/` with metadata JSON summarizing build hash, map, and scenario.

### 3.3 Automated Metrics Collection
- Develop a Python analysis script (`tools/analyze_logs.py`) that parses console logs to extract:
  - Map load durations.
  - Bot spawn success rate (count of `added bot` messages vs errors).
  - Combat statistics (frags per bot, suicides, disconnects).
- Generate CSV and Markdown summaries for regression dashboards.

### 3.4 Version Control and Traceability
- Tag each test run with:
  - Source port commit hash.
  - Botlib commit hash.
  - Configuration checksum (hash of `botlib_parity.cfg`).
- Archive console logs, demos, and analysis outputs under `artifacts/<YYYYMMDD-HHMM>` for repeatable comparisons.

## 4. Regression Parity Checklist
- ✅ Source port builds and links against the rebuilt botlib without runtime errors.
- ✅ All target maps load with identical entity counts and no missing resources.
- ✅ Bots spawn successfully and navigate key map landmarks.
- ✅ Combat demos show expected frag distribution and weapon usage.
- ✅ Console logs and demos are archived with associated metadata for future reference.

Following this process ensures consistent, repeatable validation that the rebuilt Gladiator botlib maintains parity with established Quake II source ports.
