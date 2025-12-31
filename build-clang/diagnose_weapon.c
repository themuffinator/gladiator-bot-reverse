#include <stdarg.h>
#include <stdio.h>

#include "botlib/interface/botlib_interface.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_struct.h"
#include "botlib/common/l_utils.h"
#include "botlib/ai_weapon/bot_weapon.h"

static void DebugPrint(int type, const char *fmt, ...)
{
	(void)type;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

int main(void)
{
	botlib_import_table_t imports = {0};
	imports.Print = DebugPrint;
	BotInterface_SetImportTable(&imports);

	LibVar_Init();
	L_Utils_Init();
	L_Struct_Init();

	LibVarSet("basedir", ".");
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", "");
	LibVarSet("weaponconfig", "weapons.c");
	LibVarSet("max_weaponinfo", "64");
	LibVarSet("max_projectileinfo", "64");

	ai_weapon_library_t *library = AI_LoadWeaponLibrary("weapons.c");
	fprintf(stderr, "AI_LoadWeaponLibrary returned %p\n", (void *)library);
	return (library != NULL) ? 0 : 1;
}
