# Gladiator Bot Debug Console Commands

This catalogue lists the diagnostic commands surfaced by the reversed
Gladiator botlib.  Each entry summarises the behaviour observed in the
HLIL export of `gladiator.dll` and cross-references the equivalent
implementation from the open-sourced Quake III botlib for additional
context.

## `bot_test`

* **Purpose** – Dumps information about the AAS area that contains the
  player issuing the command.  The HLIL shows the handler requesting the
  client console to print the tongue-in-cheek banner
  `"I never hacked your brain..."` before queueing the `removebot`
  command, matching the Quake III tooling that exposes the same
  behaviour through `BotExportTest`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L25580-L25622】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L377-L424】
* **Expected Output** –
  1. Report the bot origin when the command fired.
  2. Identify the active area, its cluster number and presence type.
  3. Emit a human readable list of `AREACONTENTS_*` flags (water, lava,
     slime, jump pads, portals, do-not-enter, movers).  Quake III prints
     each flag separated by `&` and terminates the block with an empty
     line when no flags are set.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L384-L423】
  4. Follow with any reachabilities that depart from the area so map
     authors can reason about traversal.

## `aas_showpath`

* **Purpose** – Walks the current routing graph from a start to a goal
  area and prints a per-step breakdown including travel type, travel
  time and reachability endpoints.  The HLIL exposes the same iteration
  as Quake III’s `BotExportTest`: once a path is found the handler
  accumulates the travel time and logs each hop via the engine
  print façade.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L398-L432】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L473-L513】
* **Expected Output** –
  1. A banner indicating the resolved start and goal areas.
  2. One line per reachability showing the source/target areas, travel
     type, travel time and the `start`/`end` vectors taken from the
     reachability record.
  3. A footer summarising total steps and accumulated travel time.
  4. A warning if no route exists between the supplied areas.

## `aas_showareas`

* **Purpose** – Prints a structured dump for either the full set of AAS
  areas or a caller-provided subset.  Both HLIL and the Quake III botlib
  perform the same iteration: for each selected area the handler logs
  the area metadata then enumerates outgoing reachabilities so level
  designers can validate connectivity.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L470-L520】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L424-L466】
* **Expected Output** –
  1. Either `listing N areas` when the caller specifies explicit area
     numbers, or `dumping all <count> areas` when no arguments are
     supplied.
  2. For every area: the number of faces, the first-face index, the
     cached centre, bounding box extents and the same reachability list
     used by `aas_showpath`.
  3. Any invalid area identifiers generate a warning noting the value is
     outside the loaded set.

The behaviours above are now wired into the rebuilt botlib through a
dedicated command registration layer so parity tests can exercise the
same debug output captured in the historical Gladiator traces.

