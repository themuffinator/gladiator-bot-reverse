//===========================================================================
//
// Name:				byte_i.c
// Function:		contains the item weights for Byte
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.5
#define FS_ARMOR					0
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights

#define W_SHOTGUN					30
#define W_SUPERSHOTGUN			60
#define W_MACHINEGUN				70
#define W_CHAINGUN				35
#define W_GRENADELAUNCHER		25
#define W_ROCKETLAUNCHER		90
#define W_HYPERBLASTER			35
#define W_RAILGUN					80
#define W_BFG10K					80
#define W_GRENADES				25
#ifdef XATRIX
#define W_IONRIPPER				60
#define W_PHALANX					35
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				65
#define W_PROXLAUNCHER			35
#define W_PLASMABEAM				80
#define W_CHAINFIST				20
#define W_DISRUPTOR				35
#endif //ROGUE
//got weapon weights
#define GWW_SHOTGUN				30
#define GWW_SUPERSHOTGUN		30
#define GWW_MACHINEGUN			30
#define GWW_CHAINGUN				30
#define GWW_GRENADELAUNCHER	25
#define GWW_ROCKETLAUNCHER		50
#define GWW_HYPERBLASTER		30
#define GWW_RAILGUN				37
#define GWW_BFG10K				50
#define GWW_GRENADES				20
#ifdef XATRIX
#define GWW_IONRIPPER			30
#define GWW_PHALANX				30
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				30
#define GWW_PROXLAUNCHER		30
#define GWW_PLASMABEAM			30
#define GWW_CHAINFIST			30
#define GWW_DISRUPTOR			30
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					10
//armor
#define W_REDARMOR				32
#define W_YELLOWARMOR			30
#define W_GREENARMOR				28
#define W_ARMORSHARD				 9		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			30
#define W_POWERSCREEN			30
//powerups
#define W_MEGAHEALTH				80
#define W_AMMOPACK				40
#define W_BANDOLIER				25
#define W_QUAD						99
#define W_INVULNERABILITY		99
#define W_SILENCER				50
#define W_REBREATHER				50
#define W_ENVIRO					50
#ifdef XATRIX
#define W_QUADFIRE				50
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				40
#define W_DOUBLE					50
#define W_COMPASS					50
#define W_SPHERE_VENGEANCE		50
#define W_SPHERE_HUNTER			50
#define W_SPHERE_DEFENDER		50
#define W_DOPPLEGANGER			50
#define W_TAG_TOKEN				50
#endif //ROGUE

#define TECH1_WEIGHT				50	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				50	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				50	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				50	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

