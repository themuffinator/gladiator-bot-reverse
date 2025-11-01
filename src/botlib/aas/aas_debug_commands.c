#include "aas_debug.h"

#include "../common/l_log.h"
#include "../interface/botlib_interface.h"

#include <ctype.h>
#include <stdlib.h>

typedef void (*bot_command_callback_t)(void);

static const char *const g_aasDebugCommands[] = {
    "bot_test",
    "aas_showpath",
    "aas_showareas",
};

static const botlib_import_table_t *AAS_DebugImportTable(void)
{
    return BotInterface_GetImportTable();
}

static int AAS_DebugCmdArgc(void)
{
    const botlib_import_table_t *imports = AAS_DebugImportTable();
    if (imports == NULL || imports->CmdArgc == NULL)
    {
        return 0;
    }
    return imports->CmdArgc();
}

static const char *AAS_DebugCmdArgv(int index)
{
    const botlib_import_table_t *imports = AAS_DebugImportTable();
    if (imports == NULL || imports->CmdArgv == NULL)
    {
        return NULL;
    }
    return imports->CmdArgv(index);
}

static int AAS_DebugParseInt(const char *text, int fallback)
{
    if (text == NULL)
    {
        return fallback;
    }

    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (endptr == text)
    {
        return fallback;
    }
    return (int)value;
}

static float AAS_DebugParseFloat(const char *text, float fallback)
{
    if (text == NULL)
    {
        return fallback;
    }

    char *endptr = NULL;
    float value = strtof(text, &endptr);
    if (endptr == text)
    {
        return fallback;
    }
    return value;
}

static void AAS_DebugCommand_BotTest(void)
{
    vec3_t origin = {0.0f, 0.0f, 0.0f};
    vec3_t angles = {0.0f, 0.0f, 0.0f};

    int argc = AAS_DebugCmdArgc();
    int entity = 0;
    int area = 0;

    if (argc > 1)
    {
        area = AAS_DebugParseInt(AAS_DebugCmdArgv(1), 0);
    }
    if (argc > 4)
    {
        origin[0] = AAS_DebugParseFloat(AAS_DebugCmdArgv(2), origin[0]);
        origin[1] = AAS_DebugParseFloat(AAS_DebugCmdArgv(3), origin[1]);
        origin[2] = AAS_DebugParseFloat(AAS_DebugCmdArgv(4), origin[2]);
    }
    if (argc > 7)
    {
        angles[0] = AAS_DebugParseFloat(AAS_DebugCmdArgv(5), angles[0]);
        angles[1] = AAS_DebugParseFloat(AAS_DebugCmdArgv(6), angles[1]);
        angles[2] = AAS_DebugParseFloat(AAS_DebugCmdArgv(7), angles[2]);
    }

    AAS_DebugBotTest(entity, (area != 0) ? AAS_DebugCmdArgv(1) : NULL, origin, angles);
}

static void AAS_DebugCommand_ShowPath(void)
{
    vec3_t start = {0.0f, 0.0f, 0.0f};
    vec3_t goal = {0.0f, 0.0f, 0.0f};

    int startArea = 0;
    int goalArea = 0;

    int argc = AAS_DebugCmdArgc();
    if (argc > 1)
    {
        startArea = AAS_DebugParseInt(AAS_DebugCmdArgv(1), 0);
    }
    if (argc > 2)
    {
        goalArea = AAS_DebugParseInt(AAS_DebugCmdArgv(2), 0);
    }

    if (argc > 5)
    {
        start[0] = AAS_DebugParseFloat(AAS_DebugCmdArgv(3), start[0]);
        start[1] = AAS_DebugParseFloat(AAS_DebugCmdArgv(4), start[1]);
        start[2] = AAS_DebugParseFloat(AAS_DebugCmdArgv(5), start[2]);
    }

    if (argc > 8)
    {
        goal[0] = AAS_DebugParseFloat(AAS_DebugCmdArgv(6), goal[0]);
        goal[1] = AAS_DebugParseFloat(AAS_DebugCmdArgv(7), goal[1]);
        goal[2] = AAS_DebugParseFloat(AAS_DebugCmdArgv(8), goal[2]);
    }

    AAS_DebugShowPath(startArea, goalArea, start, goal);
}

static void AAS_DebugCommand_ShowAreas(void)
{
    int argc = AAS_DebugCmdArgc();
    if (argc <= 1)
    {
        AAS_DebugShowAreas(NULL, 0U);
        return;
    }

    int areas[64];
    size_t count = 0U;

    for (int index = 1; index < argc && count < (sizeof(areas) / sizeof(areas[0])); ++index)
    {
        areas[count++] = AAS_DebugParseInt(AAS_DebugCmdArgv(index), 0);
    }

    AAS_DebugShowAreas(areas, count);
}

static void AAS_DebugRegisterSingleCommand(const char *name, bot_command_callback_t callback)
{
    const botlib_import_table_t *imports = AAS_DebugImportTable();
    if (imports == NULL || imports->AddCommand == NULL)
    {
        return;
    }

    imports->AddCommand(name, callback);
}

static void AAS_DebugUnregisterSingleCommand(const char *name)
{
    const botlib_import_table_t *imports = AAS_DebugImportTable();
    if (imports == NULL || imports->RemoveCommand == NULL)
    {
        return;
    }

    imports->RemoveCommand(name);
}

void AAS_DebugRegisterConsoleCommands(void)
{
    AAS_DebugRegisterSingleCommand(g_aasDebugCommands[0], AAS_DebugCommand_BotTest);
    AAS_DebugRegisterSingleCommand(g_aasDebugCommands[1], AAS_DebugCommand_ShowPath);
    AAS_DebugRegisterSingleCommand(g_aasDebugCommands[2], AAS_DebugCommand_ShowAreas);
}

void AAS_DebugUnregisterConsoleCommands(void)
{
    AAS_DebugUnregisterSingleCommand(g_aasDebugCommands[0]);
    AAS_DebugUnregisterSingleCommand(g_aasDebugCommands[1]);
    AAS_DebugUnregisterSingleCommand(g_aasDebugCommands[2]);
}
