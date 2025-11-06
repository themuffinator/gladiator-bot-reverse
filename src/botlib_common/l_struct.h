#ifndef BOTLIB_COMMON_L_STRUCT_H
#define BOTLIB_COMMON_L_STRUCT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "botlib_precomp/l_precomp.h"
#include "botlib_precomp/l_script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STRINGFIELD 80

/* Field type encodings mirror the id Software headers to simplify reuse of
 * historical data tables extracted from Gladiator. */
#define FT_CHAR      1
#define FT_INT       2
#define FT_FLOAT     3
#define FT_STRING    4
#define FT_STRUCT    6
#define FT_TYPE      0x00FF
#define FT_ARRAY     0x0100
#define FT_BOUNDED   0x0200
#define FT_UNSIGNED  0x0400

struct structdef_s;

typedef struct fielddef_s {
    const char *name;
    int offset;
    int type;
    int maxarray;
    float floatmin;
    float floatmax;
    const struct structdef_s *substruct;
} fielddef_t;

typedef struct structdef_s {
    int size;
    const fielddef_t *fields;
} structdef_t;

bool L_Struct_Init(void);
void L_Struct_Shutdown(void);
bool L_Struct_IsInitialised(void);

const fielddef_t *FindField(const fielddef_t *defs, const char *name);
bool ReadNumber(pc_source_t *source, const fielddef_t *fd, void *p);
bool ReadChar(pc_source_t *source, const fielddef_t *fd, void *p);
bool ReadString(pc_source_t *source, const fielddef_t *fd, void *p);
bool ReadStructure(pc_source_t *source, const structdef_t *def, void *structure);
int WriteIndent(FILE *fp, int indent);
bool WriteFloat(FILE *fp, float value);
bool WriteStructure(FILE *fp, const structdef_t *def, const void *structure);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BOTLIB_COMMON_L_STRUCT_H
