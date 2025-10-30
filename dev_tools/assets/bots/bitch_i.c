//===========================================================================
//
// Name:				bitch_i.c
// Function:		contains the generic item weights (how intresting items are a the bot char.)
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.5
#define FS_ARMOR					0.6
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapon weights

#define W_SHOTGUN					45
#define W_SUPERSHOTGUN			60
#define W_MACHINEGUN				100
#define W_CHAINGUN				50
#define W_GRENADELAUNCHER		55
#define W_ROCKETLAUNCHER		75
#define W_HYPERBLASTER			100
#define W_RAILGUN					95
#define W_BFG10K					55
#define W_GRENADES				40
#ifdef XATRIX
#define W_IONRIPPER				45
#define W_PHALANX					60
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				65
#define W_PROXLAUNCHER			70
#define W_PLASMABEAM				55
#define W_CHAINFIST				75
#define W_DISRUPTOR				69
#endif //ROGUE
//if the character has the weapon already 
#define GWW_SHOTGUN				20
#define GWW_SUPERSHOTGUN		21
#define GWW_MACHINEGUN			22
#define GWW_CHAINGUN				23
#define GWW_GRENADELAUNCHER	30
#define GWW_ROCKETLAUNCHER		45
#define GWW_HYPERBLASTER		50
#define GWW_RAILGUN				30
#define GWW_BFG10K				55
#define GWW_GRENADES				40
#ifdef XATRIX
#define GWW_IONRIPPER			32
#define GWW_PHALANX				31
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				31
#define GWW_PROXLAUNCHER		32
#define GWW_PLASMABEAM			33
#define GWW_CHAINFIST			20
#define GWW_DISRUPTOR			45
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					11
//armor
#define W_REDARMOR				35
#define W_YELLOWARMOR			20
#define W_GREENARMOR				10
#define W_ARMORSHARD				5		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			45
#define W_POWERSCREEN			40
//powerups
#define W_MEGAHEALTH				70
#define W_AMMOPACK				56
#define W_BANDOLIER				65
#define W_QUAD						120
#define W_INVULNERABILITY		99
#define W_SILENCER				25
#define W_REBREATHER				25
#define W_ENVIRO					20
#ifdef XATRIX
#define W_QUADFIRE				120
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				20
#define W_DOUBLE					80
#define W_COMPASS					20
#define W_SPHERE_VENGEANCE		25
#define W_SPHERE_HUNTER			60
#define W_SPHERE_DEFENDER		50
#define W_DOPPLEGANGER			20
#define W_TAG_TOKEN				20
#endif //ROGUE

#define TECH1_WEIGHT				20	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				70	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				50	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				31	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

