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
#define FS_HEALTH				2
#define FS_ARMOR				3

//initial weapon weights
#define W_SHOTGUN				750
#define W_MACHINEGUN			10
#define W_GRENADELAUNCHER		10
#define W_ROCKETLAUNCHER		10
#define W_RAILGUN				15
#define W_BFG10K				10
#define W_LIGHTNING				10
#define W_PLASMAGUN				10

//the bot has the weapons, so the weights change a little bit
#define GWW_SHOTGUN				35
#define GWW_MACHINEGUN			50
#define GWW_GRENADELAUNCHER		30
#define GWW_ROCKETLAUNCHER		10
#define GWW_RAILGUN				25
#define GWW_BFG10K				41
#define GWW_LIGHTNING			40
#define GWW_PLASMAGUN			40

//initial powerup weights
#define W_TELEPORTER			10
#define W_MEDKIT				10
#define W_QUAD					10
#define W_ENVIRO				400
#define W_HASTE					10
#define W_INVISIBILITY			200
#define W_REGEN					10
#define W_FLIGHT				10

//flag weight
#define FLAG_WEIGHT				50

//
#include "fw_items.c"

