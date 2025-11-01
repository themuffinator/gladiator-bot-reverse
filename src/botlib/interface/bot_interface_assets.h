#ifndef BOTLIB_INTERFACE_BOT_INTERFACE_ASSETS_H
#define BOTLIB_INTERFACE_BOT_INTERFACE_ASSETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct botinterface_asset_list_s
{
    char **entries;
    size_t count;
} botinterface_asset_list_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BOTLIB_INTERFACE_BOT_INTERFACE_ASSETS_H */
