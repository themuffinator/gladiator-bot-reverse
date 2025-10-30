//===========================================================================
//
// Name:				java_i.c
// Function:		contains the item weights for Java Man
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.41
#define FS_ARMOR					0.33
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights

#define W_SHOTGUN					30
#define W_SUPERSHOTGUN			65
#define W_MACHINEGUN				100
#define W_CHAINGUN				50
#define W_GRENADELAUNCHER		55
#define W_ROCKETLAUNCHER		90
#define W_HYPERBLASTER			80
#define W_RAILGUN					75
#define W_BFG10K					85
#define W_GRENADES				40
#ifdef XATRIX
#define W_IONRIPPER				66
#define W_PHALANX					80
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				65
#define W_PROXLAUNCHER			55
#define W_PLASMABEAM				80
#define W_CHAINFIST				50
#define W_DISRUPTOR				85
#endif //ROGUE
//got weapon weights
#define GWW_SHOTGUN				10
#define GWW_SUPERSHOTGUN		10
#define GWW_MACHINEGUN			10
#define GWW_CHAINGUN				10
#define GWW_GRENADELAUNCHER	10
#define GWW_ROCKETLAUNCHER		10
#define GWW_HYPERBLASTER		10
#define GWW_RAILGUN				10
#define GWW_BFG10K				10
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

#define W_CELLS					51
//armor
#define W_REDARMOR				38
#define W_YELLOWARMOR			27
#define W_GREENARMOR				25
#define W_ARMORSHARD				2		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			40
#define W_POWERSCREEN			10
//powerups
#define W_MEGAHEALTH				100
#define W_AMMOPACK				30
#define W_BANDOLIER				35
#define W_QUAD						99
#define W_INVULNERABILITY		120
#define W_SILENCER				50
#define W_REBREATHER				50
#define W_ENVIRO					50
#ifdef XATRIX
#define W_QUADFIRE				60
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				70
#define W_DOUBLE					70
#define W_COMPASS					35
#define W_SPHERE_VENGEANCE		70
#define W_SPHERE_HUNTER			70
#define W_SPHERE_DEFENDER		70
#define W_DOPPLEGANGER			70
#define W_TAG_TOKEN				70
#endif //ROGUE

#define TECH1_WEIGHT				40	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				70	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				30	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				50	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

