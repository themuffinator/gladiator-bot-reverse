#include "ai_goal.h"

#include <string.h>

#include "../../q2bridge/botlib.h"

typedef struct ai_goal_module_state_s
{
    bool initialised;
} ai_goal_module_state_t;

static ai_goal_module_state_t g_ai_goal_module = {false};

bool AI_Goal_Init(void)
{
    if (g_ai_goal_module.initialised)
    {
        return true;
    }

    g_ai_goal_module.initialised = true;
    return true;
}

void AI_Goal_Shutdown(void)
{
    if (!g_ai_goal_module.initialised)
    {
        return;
    }

    BotGoal_Shutdown();
    memset(&g_ai_goal_module, 0, sizeof(g_ai_goal_module));
}

void AI_Goal_BeginFrame(float time)
{
    (void)time;

    if (!g_ai_goal_module.initialised)
    {
        return;
    }

    /*
     * Placeholder for future per-frame maintenance. When the goal module is
     * fully reconstructed this hook will synchronise avoid-goal timers with the
     * current AAS time and purge expired stack entries.
     */
}

int AI_Goal_AllocState(int client)
{
    if (!g_ai_goal_module.initialised && !AI_Goal_Init())
    {
        return 0;
    }

    return BotAllocGoalState(client);
}

void AI_Goal_FreeState(int handle)
{
    BotFreeGoalState(handle);
}

int AI_Goal_LoadItemWeights(int goalstate, const char *filename)
{
    if (!g_ai_goal_module.initialised && !AI_Goal_Init())
    {
        return BLERR_CANNOTLOADITEMWEIGHTS;
    }

    return BotLoadItemWeights(goalstate, filename);
}

void AI_Goal_SetClient(int goalstate, int client)
{
    BotGoal_SetClient(goalstate, client);
}

