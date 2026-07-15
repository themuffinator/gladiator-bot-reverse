#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_last_botlib_message_type = 0;
static char g_last_botlib_message[1024];
static float g_aas_time = 0.0f;
typedef struct botlib_test_libvar_s {
        char name[64];
        char string[128];
        float value;
} botlib_test_libvar_t;

static botlib_test_libvar_t g_botlib_test_libvars[16];
static size_t g_botlib_test_libvar_count = 0;
static char g_bridge_maxclients_name[32] = "maxclients";
static char g_bridge_maxclients_string[32] = "4";
static libvar_t g_bridge_maxclients;

/*
=============
BotLib_TestFindLibVar

Finds or creates a mocked libvar entry for chat tests.
=============
*/
static botlib_test_libvar_t *BotLib_TestFindLibVar(const char *var_name,
	bool create)
{
	if (var_name == NULL)
	{
		return NULL;
	}

	for (size_t index = 0; index < g_botlib_test_libvar_count; ++index)
	{
		if (strcmp(g_botlib_test_libvars[index].name, var_name) == 0)
		{
			return &g_botlib_test_libvars[index];
		}
	}

	if (!create
		|| g_botlib_test_libvar_count >=
			(sizeof(g_botlib_test_libvars) /
				sizeof(g_botlib_test_libvars[0])))
	{
		return NULL;
	}

	botlib_test_libvar_t *var =
		&g_botlib_test_libvars[g_botlib_test_libvar_count++];
	memset(var, 0, sizeof(*var));
	snprintf(var->name, sizeof(var->name), "%s", var_name);
	return var;
}

/*
=============
BotLib_TestResetLibVars

Clears the cached libvar overrides for the test harness.
=============
*/
void BotLib_TestResetLibVars(void)
{
	g_botlib_test_libvar_count = 0;
	g_aas_time = 0.0f;

	g_bridge_maxclients.value = strtof(g_bridge_maxclients_string, NULL);
	g_bridge_maxclients.modified = 0;
	g_bridge_maxclients.name = g_bridge_maxclients_name;
	g_bridge_maxclients.string = g_bridge_maxclients_string;
	g_bridge_maxclients.next = NULL;
}

/*
=============
BotLib_TestSetMaxClients

Overrides the mocked maxclients libvar used by the bridge.
=============
*/
void BotLib_TestSetMaxClients(float value)
{
	snprintf(g_bridge_maxclients_string,
		sizeof(g_bridge_maxclients_string),
		"%g",
		value);
	g_bridge_maxclients.value = value;
}

/*
=============
Bridge_MaxClients

Provides the maxclients libvar for bridge validation hooks.
=============
*/
libvar_t *Bridge_MaxClients(void)
{
	return &g_bridge_maxclients;
}

/*
=============
BotLib_TestSetLibVar

Overrides the specified libvar for LibVarValue queries in tests.
=============
*/
void BotLib_TestSetLibVar(const char *var_name, float value) {
	botlib_test_libvar_t *var = BotLib_TestFindLibVar(var_name, true);
	if (var == NULL) {
		return;
	}

	var->value = value;
	snprintf(var->string, sizeof(var->string), "%g", value);
}

/*
=============
BotLib_TestSetLibVarString

Overrides a mocked libvar string and keeps its numeric value in sync.
=============
*/
void BotLib_TestSetLibVarString(const char *var_name, const char *value)
{
	botlib_test_libvar_t *var = BotLib_TestFindLibVar(var_name, true);
	if (var == NULL)
	{
		return;
	}

	snprintf(var->string, sizeof(var->string), "%s", value != NULL ? value : "");
	var->value = strtof(var->string, NULL);
}

/*
=============
BotLib_TestResetLastMessage

Clears the cached BotLib_Print message.
=============
*/
void BotLib_TestResetLastMessage(void) {
	g_last_botlib_message_type = 0;
	g_last_botlib_message[0] = '\0';
}

/*
=============
BotLib_TestGetLastMessage

Returns the cached BotLib_Print message text.
=============
*/
const char *BotLib_TestGetLastMessage(void)
{
	return g_last_botlib_message;
}

/*
=============
BotLib_TestGetLastMessageType

Returns the cached BotLib_Print message type.
=============
*/
int BotLib_TestGetLastMessageType(void)
{
	return g_last_botlib_message_type;
}

/*
=============
BotLib_Print

Captures diagnostics emitted by the bot library.
=============
*/
void BotLib_Print(int type, const char *fmt, ...) {
	g_last_botlib_message_type = type;

	va_list args;
	va_start(args, fmt);

	va_list copy;
	va_copy(copy, args);
	vsnprintf(g_last_botlib_message, sizeof(g_last_botlib_message), fmt, copy);
	va_end(copy);

	vfprintf(stderr, fmt, args);
	va_end(args);
}

/*
=============
BotLib_LogWrite
=============
*/
void BotLib_LogWrite(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

/*
=============
BotLib_Error
=============
*/
void BotLib_Error(int level, const char *fmt, ...) {
	(void)level;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

/*
=============
BotInterface_GetImportTable
=============
*/
const botlib_import_table_t *BotInterface_GetImportTable(void)
{
	return NULL;
}

/*
=============
BotLibraryInitialized
=============
*/
bool BotLibraryInitialized(void)
{
	return true;
}

/*
=============
BotLib_TestSetAASTime

Overrides the frame timestamp stored in newly queued console-message nodes.
=============
*/
void BotLib_TestSetAASTime(float time)
{
	g_aas_time = time;
}

/*
=============
AAS_Time

Provides the frame timestamp stored with retail console-message nodes.
=============
*/
float AAS_Time(void)
{
	return g_aas_time;
}

/*
=============
LibVarValue

Returns the overridden libvar value for tests, or the provided default.
=============
*/
float LibVarValue(const char *var_name, const char *default_value) {
	if (var_name != NULL) {
		botlib_test_libvar_t *var = BotLib_TestFindLibVar(var_name, false);
		if (var != NULL) {
			return var->value;
		}
		if (strcmp(var_name, "maxclients") == 0) {
			return g_bridge_maxclients.value;
		}
	}

	if (default_value == NULL) {
		return 0.0f;
	}

	return strtof(default_value, NULL);
}

/*
=============
LibVarString

Returns the overridden libvar string for tests, or the provided default.
=============
*/
const char *LibVarString(const char *var_name, const char *default_value)
{
	if (var_name != NULL)
	{
		botlib_test_libvar_t *var = BotLib_TestFindLibVar(var_name, false);
		if (var != NULL)
		{
			return var->string;
		}
		if (strcmp(var_name, "maxclients") == 0)
		{
			return g_bridge_maxclients_string;
		}
	}

	return default_value != NULL ? default_value : "";
}

/*
=============
GetMemory
=============
*/
void *GetMemory(size_t size)
{
	return malloc(size);
}

/*
=============
GetClearedMemory
=============
*/
void *GetClearedMemory(size_t size)
{
return calloc(1, size);
}

/*
=============
FreeMemory
=============
*/
void FreeMemory(void *ptr)
{
	free(ptr);
}

/*
=============
BotLib_LocateAssetRoot
=============
*/
bool BotLib_LocateAssetRoot(char *buffer, size_t size) {
#ifdef BOT_ASSET_ROOT
	if (buffer == NULL || size == 0) {
		return false;
	}

	int written = snprintf(buffer, size, "%s", BOT_ASSET_ROOT);
	if (written < 0 || (size_t)written >= size) {
		buffer[0] = '\0';
		return false;
	}

	return true;
#else
	if (buffer != NULL && size > 0) {
		buffer[0] = '\0';
	}
	return false;
#endif
}
