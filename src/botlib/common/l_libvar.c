#include "l_libvar.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/interface/botlib_interface.h"

#define BOTLIB_MAX_LIBVAR_STRING 1024

static libvar_t *g_libvar_list = NULL;

static char *LibVar_CopyString(const char *string)
{
    size_t length = (string != NULL) ? strlen(string) : 0;
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    if (length > 0 && string != NULL) {
        memcpy(copy, string, length);
    }
    copy[length] = '\0';
    return copy;
}

static float LibVar_ParseValue(const char *string)
{
    if (string == NULL) {
        return 0.0f;
    }

    char *end = NULL;
    float value = (float)strtod(string, &end);
    if (end == string) {
        return 0.0f;
    }
    return value;
}

static bool LibVar_NameEquals(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }

    while (*lhs && *rhs) {
        unsigned char a = (unsigned char)*lhs++;
        unsigned char b = (unsigned char)*rhs++;
        if (tolower(a) != tolower(b)) {
            return false;
        }
    }

    return (*lhs == '\0' && *rhs == '\0');
}

static libvar_t *LibVar_Find(const char *name)
{
    for (libvar_t *var = g_libvar_list; var != NULL; var = var->next) {
        if (LibVar_NameEquals(var->name, name)) {
            return var;
        }
    }
    return NULL;
}

static void LibVar_Link(libvar_t *var)
{
    if (var == NULL) {
        return;
    }

    var->next = g_libvar_list;
    g_libvar_list = var;
}

static libvar_t *LibVar_Create(const char *name, const char *string, bool modified)
{
    if (name == NULL) {
        return NULL;
    }

    libvar_t *var = (libvar_t *)calloc(1, sizeof(libvar_t));
    if (var == NULL) {
        return NULL;
    }

    var->name = LibVar_CopyString(name);
    if (var->name == NULL) {
        free(var);
        return NULL;
    }

    var->string = LibVar_CopyString(string != NULL ? string : "");
    if (var->string == NULL) {
        free(var->name);
        free(var);
        return NULL;
    }

    var->value = LibVar_ParseValue(var->string);
    var->modified = modified;

    LibVar_Link(var);
    return var;
}

static void LibVar_Destroy(libvar_t *var)
{
    if (var == NULL) {
        return;
    }

    free(var->name);
    free(var->string);
    free(var);
}

static void LibVar_UnlinkAll(void)
{
    libvar_t *var = g_libvar_list;
    while (var != NULL) {
        libvar_t *next = var->next;
        LibVar_Destroy(var);
        var = next;
    }
    g_libvar_list = NULL;
}

static bool LibVar_FetchFromImport(const char *name, char *buffer, size_t buffer_size)
{
    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    if (imports == NULL || imports->BotLibVarGet == NULL) {
        return false;
    }

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';
    if (imports->BotLibVarGet(name, buffer, buffer_size) != 0) {
        buffer[0] = '\0';
        return false;
    }

    buffer[buffer_size - 1] = '\0';
    return true;
}

static void LibVar_UpdateCachedString(libvar_t *var, const char *string)
{
    if (var == NULL || string == NULL) {
        return;
    }

    if (var->string != NULL && strcmp(var->string, string) == 0) {
        return;
    }

    char *copy = LibVar_CopyString(string);
    if (copy == NULL) {
        return;
    }

    free(var->string);
    var->string = copy;
    var->value = LibVar_ParseValue(copy);
    var->modified = true;
}

static libvar_t *LibVar_CreateFromImport(const char *name)
{
    char buffer[BOTLIB_MAX_LIBVAR_STRING];
    if (!LibVar_FetchFromImport(name, buffer, sizeof(buffer))) {
        return NULL;
    }

    return LibVar_Create(name, buffer, false);
}

void LibVar_Init(void)
{
    LibVar_UnlinkAll();
}

void LibVar_Shutdown(void)
{
    LibVar_UnlinkAll();
}

void LibVar_ResetCache(void)
{
    LibVar_UnlinkAll();
}

libvar_t *LibVarGet(const char *var_name)
{
    if (var_name == NULL) {
        return NULL;
    }

    libvar_t *var = LibVar_Find(var_name);
    if (var != NULL) {
        char buffer[BOTLIB_MAX_LIBVAR_STRING];
        if (LibVar_FetchFromImport(var->name, buffer, sizeof(buffer))) {
            LibVar_UpdateCachedString(var, buffer);
        }
        return var;
    }

    var = LibVar_CreateFromImport(var_name);
    if (var != NULL) {
        return var;
    }

    return NULL;
}

const char *LibVarGetString(const char *var_name)
{
    libvar_t *var = LibVarGet(var_name);
    return (var != NULL && var->string != NULL) ? var->string : "";
}

float LibVarGetValue(const char *var_name)
{
    libvar_t *var = LibVarGet(var_name);
    return (var != NULL) ? var->value : 0.0f;
}

libvar_t *LibVar(const char *var_name, const char *value)
{
    libvar_t *var = LibVarGet(var_name);
    if (var != NULL) {
        return var;
    }

    const char *initial = (value != NULL) ? value : "";
    var = LibVar_Create(var_name, initial, true);
    return var;
}

const char *LibVarString(const char *var_name, const char *value)
{
    libvar_t *var = LibVar(var_name, value);
    return (var != NULL && var->string != NULL) ? var->string : "";
}

float LibVarValue(const char *var_name, const char *value)
{
    libvar_t *var = LibVar(var_name, value);
    return (var != NULL) ? var->value : 0.0f;
}

int LibVarSetStatus(const char *var_name, const char *value)
{
    if (var_name == NULL || value == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    if (imports != NULL && imports->BotLibVarSet != NULL) {
        int status = imports->BotLibVarSet(var_name, value);
        if (status != BLERR_NOERROR) {
            return status;
        }
    }

    libvar_t *var = LibVar_Find(var_name);
    if (var == NULL) {
        var = LibVar_Create(var_name, value, true);
        if (var == NULL) {
            return BLERR_INVALIDIMPORT;
        }
        return BLERR_NOERROR;
    }

    LibVar_UpdateCachedString(var, value);
    return BLERR_NOERROR;
}

void LibVarSet(const char *var_name, const char *value)
{
    (void)LibVarSetStatus(var_name, value);
}

bool LibVarChanged(const char *var_name)
{
    libvar_t *var = LibVar_Find(var_name);
    if (var == NULL) {
        return false;
    }
    return var->modified;
}

void LibVarSetNotModified(const char *var_name)
{
    libvar_t *var = LibVar_Find(var_name);
    if (var == NULL) {
        return;
    }
    var->modified = false;
}
