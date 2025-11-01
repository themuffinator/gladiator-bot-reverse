#include "l_struct.h"

static bool g_l_struct_initialised = false;

bool L_Struct_Init(void)
{
    if (g_l_struct_initialised)
    {
        return true;
    }

    g_l_struct_initialised = true;
    return true;
}

void L_Struct_Shutdown(void)
{
    g_l_struct_initialised = false;
}

bool L_Struct_IsInitialised(void)
{
    return g_l_struct_initialised;
}
