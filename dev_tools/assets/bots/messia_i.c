//===========================================================================
//
// Name:				messia_i.c
// Function:		item weights for the Shotgun Messiah
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.6
#define FS_ARMOR					0.25
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapon weights
#define W_SHOTGUN					70
#define W_SUPERSHOTGUN			100
#define W_MACHINEGUN				95
#define W_CHAINGUN				80
#define W_GRENADELAUNCHER		30
#define W_ROCKETLAUNCHER		50
#define W_HYPERBLASTER			60
#define W_RAILGUN					65
#define W_BFG10K					40
#define W_GRENADES				 0
#ifdef XATRIX
#define W_IONRIPPER				54
#define W_PHALANX					53
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				65
#define W_PROXLAUNCHER			20
#define W_PLASMABEAM				20
#define W_CHAINFIST				20
#define W_DISRUPTOR				25
#endif //ROGUE
//got weapon weights
#define GWW_SHOTGUN				15
#define GWW_SUPERSHOTGUN		100
#define GWW_MACHINEGUN			10
#define GWW_CHAINGUN				12
#define GWW_GRENADELAUNCHER	10
#define GWW_ROCKETLAUNCHER		15
#define GWW_HYPERBLASTER		15
#define GWW_RAILGUN				17
#define GWW_BFG10K				11
#define GWW_GRENADES				10
#ifdef XATRIX
#define GWW_IONRIPPER			10
#define GWW_PHALANX				10
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				10
#define GWW_PROXLAUNCHER		10
#define GWW_PLASMABEAM			10
#define GWW_CHAINFIST			10
#define GWW_DISRUPTOR			10
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					1
//armor
#define W_REDARMOR				14
#define W_YELLOWARMOR			12
#define W_GREENARMOR				55
#define W_ARMORSHARD				0		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			50
#define W_POWERSCREEN			45
//powerups
#define W_MEGAHEALTH				99
#define W_AMMOPACK				99
#define W_BANDOLIER				80
#define W_QUAD						95
#define W_INVULNERABILITY		65
#define W_SILENCER				60
#define W_REBREATHER				20
#define W_ENVIRO					20
#ifdef XATRIX
#define W_QUADFIRE				40
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				20
#define W_DOUBLE					44
#define W_COMPASS					20
#define W_SPHERE_VENGEANCE		32
#define W_SPHERE_HUNTER			55
#define W_SPHERE_DEFENDER		62
#define W_DOPPLEGANGER			72
#define W_TAG_TOKEN				20
#endif //ROGUE

#define TECH1_WEIGHT				65	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				74	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				93	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				67	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

