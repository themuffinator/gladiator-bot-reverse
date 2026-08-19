# Q2 bridge

Translation between the Quake II game module and the botlib. Everything that
crosses the host boundary passes through here.

| File | Contents |
| --- | --- |
| `botlib.h` | The retail ABI: the 20-pointer export table, the ten-callback import table, and the shared bot structures. Layouts are fixed by the retail binary |
| `bridge.c` | The import-table wrappers the botlib calls back through |
| `bridge_config.c` | Bridge-side configuration and libvar plumbing |
| `aas_translation.c` | Entity and trace translation between Quake II and AAS |
| `update_translator.c` | Assembles `bot_input_t` and entity-update records |

Structure layouts and calling conventions in `botlib.h` are dictated by the
retail module; changing one changes the ABI the original Gladiator mod links
against. `tests/parity/test_bridge.c` and
`tests/parity/test_update_translator.c` cover this layer.
