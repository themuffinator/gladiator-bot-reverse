# Shared

Headers used across every module in the reconstruction.

| File | Contents |
| --- | --- |
| `q_shared.h` | Quake-derived shared types and math |
| `q_platform.h` | Platform and compiler conventions (`id386`, `idaxp`, byte order) |
| `bot_types.h` | Bot-facing structures shared by the botlib and the Q2 bridge |
| `platform_export.h` | The `GLADIATOR_API` annotation used by `GetBotAPI` |
| `gladiator_version.h.in` | Template for the generated reconstruction version header |
| `gladiator_version.rc.in` | Template for the Windows `VERSIONINFO` resource |

The two `.in` files are configured into `<build>/generated/shared/` by
`cmake/ReconstructionVersion.cmake` and reached as
`#include "shared/gladiator_version.h"`. See
[docs/reconstruction_versioning.md](../../docs/reconstruction_versioning.md).

`gladiator_shared_headers` is an INTERFACE target: it carries the include paths
and the `id386`/`idaxp` definitions, and every other module links it.
