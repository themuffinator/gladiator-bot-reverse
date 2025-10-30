//===========================================================================
//
// Name:				maxine_i.c
// Function:		contains the item weights for Maxine
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.5
#define FS_ARMOR					1
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights

#define W_SHOTGUN					20
#define W_SUPERSHOTGUN			35
#define W_MACHINEGUN				40
#define W_CHAINGUN				50
#define W_GRENADELAUNCHER		60
#define W_ROCKETLAUNCHER		70
#define W_HYPERBLASTER			80
#define W_RAILGUN					90
#define W_BFG10K					100
#define W_GRENADES				15
#ifdef XATRIX
#define W_IONRIPPER				30
#define W_PHALANX					56
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				40
#define W_PROXLAUNCHER			75
#define W_PLASMABEAM				85
#define W_CHAINFIST				60
#define W_DISRUPTOR				99
#endif //ROGUE
//got weapon weights
#define GWW_SHOTGUN				2
#define GWW_SUPERSHOTGUN		3
#define GWW_MACHINEGUN			4
#define GWW_CHAINGUN				5
#define GWW_GRENADELAUNCHER	6
#define GWW_ROCKETLAUNCHER		7
#define GWW_HYPERBLASTER		8
#define GWW_RAILGUN				90
#define GWW_BFG10K				100
#define GWW_GRENADES				10
#ifdef XATRIX
#define GWW_IONRIPPER			3
#define GWW_PHALANX				3
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				1
#define GWW_PROXLAUNCHER		2
#define GWW_PLASMABEAM			3
#define GWW_CHAINFIST			4
#define GWW_DISRUPTOR			99
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					61
//armor
#define W_REDARMOR				95
#define W_YELLOWARMOR			80
#define W_GREENARMOR				75
#define W_ARMORSHARD				1		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			120
#define W_POWERSCREEN			120
//powerups
#define W_MEGAHEALTH				100
#define W_AMMOPACK				77
#define W_BANDOLIER				25
#define W_QUAD						80
#define W_INVULNERABILITY		90
#define W_SILENCER				30
#define W_REBREATHER				50
#define W_ENVIRO					70
#ifdef XATRIX
#define W_QUADFIRE				80
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				30
#define W_DOUBLE					65
#define W_COMPASS					21
#define W_SPHERE_VENGEANCE		22
#define W_SPHERE_HUNTER			25
#define W_SPHERE_DEFENDER		55
#define W_DOPPLEGANGER			60
#define W_TAG_TOKEN				20
#endif //ROGUE

#define TECH1_WEIGHT				40	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				23	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				21	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				22	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

