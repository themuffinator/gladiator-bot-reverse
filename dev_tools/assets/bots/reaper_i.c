//===========================================================================
//
// Name:				reaper_i.c
// Function:		item weights for the Reaper
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Version:			19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					1
#define FS_ARMOR					1
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights
#define W_SHOTGUN					30
#define W_SUPERSHOTGUN			60
#define W_MACHINEGUN				70
#define W_CHAINGUN				30
#define W_GRENADELAUNCHER		25
#define W_ROCKETLAUNCHER		100
#define W_HYPERBLASTER			35
#define W_RAILGUN					100
#define W_BFG10K					80
#define W_GRENADES				25
#ifdef XATRIX
#define W_IONRIPPER				75
#define W_PHALANX					65
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				30
#define W_PROXLAUNCHER			35
#define W_PLASMABEAM				40
#define W_CHAINFIST				35
#define W_DISRUPTOR				85
#endif //ROGUE
//the bot has the weapons, so the weights change a little bit
#define GWW_SHOTGUN				20
#define GWW_SUPERSHOTGUN		40
#define GWW_MACHINEGUN			45
#define GWW_CHAINGUN				30
#define GWW_GRENADELAUNCHER	10
#define GWW_ROCKETLAUNCHER		66
#define GWW_HYPERBLASTER		30
#define GWW_RAILGUN				35
#define GWW_BFG10K				52
#define GWW_GRENADES				25
#ifdef XATRIX
#define GWW_IONRIPPER			33
#define GWW_PHALANX				42
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				45
#define GWW_PROXLAUNCHER		25
#define GWW_PLASMABEAM			34
#define GWW_CHAINFIST			11
#define GWW_DISRUPTOR			20
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					51
//armor
#define W_REDARMOR				85
#define W_YELLOWARMOR			70
#define W_GREENARMOR				55
#define W_ARMORSHARD				11		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			99
#define W_POWERSCREEN			60
//powerups
#define W_MEGAHEALTH				98
#define W_AMMOPACK				90
#define W_BANDOLIER				80
#define W_QUAD						97
#define W_INVULNERABILITY		95
#define W_SILENCER				46
#define W_REBREATHER				33
#define W_ENVIRO					36
#ifdef XATRIX
#define W_QUADFIRE				40
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				30
#define W_DOUBLE					66
#define W_COMPASS					30
#define W_SPHERE_VENGEANCE		74
#define W_SPHERE_HUNTER			76
#define W_SPHERE_DEFENDER		77
#define W_DOPPLEGANGER			54
#define W_TAG_TOKEN				63
#endif //ROGUE

#define TECH1_WEIGHT				69	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				100	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				89	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				72	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

