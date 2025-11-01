#include "botlib/ai/ea/ea_main.h"

#include <stddef.h>
#include <stdlib.h>

#include "botlib/common/l_log.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/botlib.h"

static bot_input_t *g_ea_inputs = NULL;
static int g_ea_max_clients = 0;
static bool g_ea_initialised = false;

int EA_Init(int max_clients)
{
    if (g_ea_initialised)
    {
        return BLERR_NOERROR;
    }

    if (max_clients <= 0)
    {
        BotLib_Print(PRT_ERROR, "EA_Init: invalid max_clients %d\n", max_clients);
        return BLERR_INVALIDIMPORT;
    }

    size_t allocation = (size_t)max_clients * sizeof(bot_input_t);
    g_ea_inputs = (bot_input_t *)calloc((size_t)max_clients, sizeof(bot_input_t));
    if (g_ea_inputs == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "EA_Init: failed to allocate %zu bytes for client input buffers\n",
                     allocation);
        return BLERR_INVALIDIMPORT;
    }

    g_ea_max_clients = max_clients;
    g_ea_initialised = true;
    return BLERR_NOERROR;
}

void EA_Shutdown(void)
{
    if (!g_ea_initialised)
    {
        return;
    }

    free(g_ea_inputs);
    g_ea_inputs = NULL;
    g_ea_max_clients = 0;
    g_ea_initialised = false;
}

bool EA_IsInitialised(void)
{
    return g_ea_initialised;
}
