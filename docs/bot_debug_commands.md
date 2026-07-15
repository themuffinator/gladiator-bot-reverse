# Gladiator Bot Debug Console Commands

This catalogue lists reconstructed developer diagnostics exposed by this
project. They are not retail Gladiator exports: the DLL's `Test` slot at
`sub_10038460` simply returns zero. The commands use the loaded Gladiator AAS
data and take their presentation and graph-inspection cues from the open-source
Quake III botlib.

## `bot_test`

* **Purpose** – Dumps information about the requested AAS area, or resolves
  the supplied point through the loaded AAS BSP tree when the requested area
  is invalid.
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

* **Purpose** – Performs a bounded breadth-first walk over the loaded area's
  outgoing reachability spans and prints a per-step breakdown including travel
  type, travel time, and endpoints. This is a project diagnostic; it is not the
  retail route-selection algorithm.
* **Expected Output** –
  1. A banner indicating the resolved start and goal areas.
  2. One line per reachability showing the source/target areas, travel
     type, travel time and the `start`/`end` vectors taken from the
     reachability record.
  3. A footer summarising total steps and accumulated travel time.
  4. A warning if no route exists between the supplied areas.

## `aas_showareas`

* **Purpose** – Prints a structured dump for either every real AAS area or a
  caller-provided subset. For each selected area it logs metadata and enumerates
  the authoritative `firstreachablearea`/`numreachableareas` span so level
  designers can validate connectivity.
* **Expected Output** –
  1. Either `listing N areas` when the caller specifies explicit area
     numbers, or `dumping all <count> areas` when no arguments are
     supplied.
  2. For every area: the number of faces, the first-face index, the
     cached centre, bounding box extents and the same reachability list
     used by `aas_showpath`.
  3. Any invalid area identifiers generate a warning noting the value is
     outside the loaded set.

The behaviours above are wired through the rebuilt botlib's command layer and
covered as reconstruction diagnostics. They must not be counted as recovered
retail `Test`-export behaviour.

