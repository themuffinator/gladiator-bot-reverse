#include "l_utils.h"

static bool g_l_utils_initialised = false;

bool L_Utils_Init(void)
{
    if (g_l_utils_initialised)
    {
        return true;
    }

    g_l_utils_initialised = true;
    return true;
}

void L_Utils_Shutdown(void)
{
    g_l_utils_initialised = false;
}

bool L_Utils_IsInitialised(void)
{
    return g_l_utils_initialised;
}
