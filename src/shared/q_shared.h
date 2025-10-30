#ifndef SHARED_Q_SHARED_H
#define SHARED_Q_SHARED_H

#include <stdint.h>

// Basic Quake II shared types reproduced for the bridge layer. The intent is
// to stay layout-compatible with the original headers while providing a
// trimmed set of declarations required by the modern codebase.

typedef unsigned char byte;

typedef enum qboolean_e {
    qfalse = 0,
    qtrue  = 1
} qboolean;

typedef float vec_t;
typedef vec_t vec3_t[3];

// Plane definition used by BSP collision traces.
typedef struct cplane_s {
    vec3_t normal;
    float  dist;
    byte   type;
    byte   signbits;
    byte   pad[2];
} cplane_t;

// Movement types recognised by the Quake II prediction code.
typedef enum pmtype_e {
    PM_NORMAL,
    PM_SPECTATOR,
    PM_DEAD,
    PM_GIB,
    PM_FREEZE
} pmtype_t;

// Constants mirrored from the original q_shared header.
#define MAX_STATS 32
#define MAX_ITEMS 256

#endif // SHARED_Q_SHARED_H
