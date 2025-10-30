//===========================================================================
//
// Name:				babe_i.c
// Function:		item weights for Silicon Babe character
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.4
#define FS_ARMOR					0.6
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights
#define W_SHOTGUN					50
#define W_SUPERSHOTGUN			65
#define W_MACHINEGUN				70
#define W_CHAINGUN				60
#define W_GRENADELAUNCHER		40
#define W_ROCKETLAUNCHER		120
#define W_HYPERBLASTER			75
#define W_RAILGUN					85
#define W_BFG10K					30
#define W_GRENADES				30
#ifdef XATRIX
#define W_IONRIPPER				40
#define W_PHALANX					41
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				42
#define W_PROXLAUNCHER			45
#define W_PLASMABEAM				50
#define W_CHAINFIST				32
#define W_DISRUPTOR				60
#endif //ROGUE
//the bot has the weapons, so the weights change a little bit
#define GWW_SHOTGUN				35
#define GWW_SUPERSHOTGUN		60
#define GWW_MACHINEGUN			50
#define GWW_CHAINGUN				40
#define GWW_GRENADELAUNCHER	30
#define GWW_ROCKETLAUNCHER		90
#define GWW_HYPERBLASTER		30
#define GWW_RAILGUN				25
#define GWW_BFG10K				41
#define GWW_GRENADES				30
#ifdef XATRIX
#define GWW_IONRIPPER			20
#define GWW_PHALANX				21
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				55
#define GWW_PROXLAUNCHER		51
#define GWW_PLASMABEAM			52
#define GWW_CHAINFIST			53
#define GWW_DISRUPTOR			54
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					61
//armor
#define W_REDARMOR				60
#define W_YELLOWARMOR			80
#define W_GREENARMOR				70
#define W_ARMORSHARD				21		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			94
#define W_POWERSCREEN			75
//powerups
#define W_MEGAHEALTH				86
#define W_AMMOPACK				70
#define W_BANDOLIER				40
#define W_QUAD						87
#define W_INVULNERABILITY		99
#define W_SILENCER				20
#define W_REBREATHER				20
#define W_ENVIRO					50
#ifdef XATRIX
#define W_QUADFIRE				90
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				30
#define W_DOUBLE					60
#define W_COMPASS					20
#define W_SPHERE_VENGEANCE		80
#define W_SPHERE_HUNTER			50
#define W_SPHERE_DEFENDER		55
#define W_DOPPLEGANGER			76
#define W_TAG_TOKEN				40
#endif //ROGUE

#define TECH1_WEIGHT				50		// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				100	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				98		// Haste Tech \ Time Accel
#define TECH4_WEIGHT				65		// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

