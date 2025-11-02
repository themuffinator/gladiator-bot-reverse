//===========================================================================
//
// Name:			visor_i.c
// Function:		
// Programmer:		Mr Elusive (MrElusive@idsoftware.com)
// Last update:		1999-09-08
// Tab Size:		4 (real tabs)
//===========================================================================

#include "inv.h"

//initial health/armor states
#define FS_HEALTH			3
#define FS_ARMOR			2

//initial weapon weights
#define W_SHOTGUN				250
#define W_MACHINEGUN			70
#define W_GRENADELAUNCHER		40
#define W_ROCKETLAUNCHER		320
#define W_RAILGUN				85
#define W_BFG10K				30
#define W_LIGHTNING				450
#define W_PLASMAGUN				50

//the bot has the weapons, so the weights change a little bit
#define GWW_SHOTGUN				235
#define GWW_MACHINEGUN			50
#define GWW_GRENADELAUNCHER		30
#define GWW_ROCKETLAUNCHER		290
#define GWW_RAILGUN				25
#define GWW_BFG10K				41
#define GWW_LIGHTNING			440
#define GWW_PLASMAGUN			40

//initial powerup weights
#define W_TELEPORTER			40
#define W_MEDKIT				40
#define W_QUAD					400
#define W_ENVIRO				40
#define W_HASTE					40
#define W_INVISIBILITY			40
#define W_REGEN					40
#define W_FLIGHT				40

//flag weight
#define FLAG_WEIGHT				50

//
#include "fw_items.c"

