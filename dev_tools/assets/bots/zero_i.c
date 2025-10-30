//===========================================================================
//
// Name:				zero_i.c
// Function:		contains the generic item weights for Zero
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.3	//real stuff here
#define FS_ARMOR					1		//real stuff here
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights

#define W_SHOTGUN					50
#define W_SUPERSHOTGUN			50
#define W_MACHINEGUN				60
#define W_CHAINGUN				35
#define W_GRENADELAUNCHER		25
#define W_ROCKETLAUNCHER		80
#define W_HYPERBLASTER			60
#define W_RAILGUN					80
#define W_BFG10K					100
#define W_GRENADES				25
#ifdef XATRIX
#define W_IONRIPPER				50
#define W_PHALANX					50
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				50
#define W_PROXLAUNCHER			50
#define W_PLASMABEAM				50
#define W_CHAINFIST				50
#define W_DISRUPTOR				50
#endif //ROGUE
//got weapon weights

#define GWW_SHOTGUN				50
#define GWW_SUPERSHOTGUN		50
#define GWW_MACHINEGUN			60
#define GWW_CHAINGUN				35
#define GWW_GRENADELAUNCHER	25
#define GWW_ROCKETLAUNCHER		80
#define GWW_HYPERBLASTER		60
#define GWW_RAILGUN				80
#define GWW_BFG10K				100
#define GWW_GRENADES				25
#ifdef XATRIX
#define GWW_IONRIPPER			50
#define GWW_PHALANX				50
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				50
#define GWW_PROXLAUNCHER		50
#define GWW_PLASMABEAM			50
#define GWW_CHAINFIST			50
#define GWW_DISRUPTOR			50
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
#define W_POWERSHIELD			50
#define W_POWERSCREEN			50
//powerups
#define W_MEGAHEALTH				80
#define W_AMMOPACK				50
#define W_BANDOLIER				50
#define W_QUAD						80
#define W_INVULNERABILITY		80
#define W_SILENCER				50
#define W_REBREATHER				50
#define W_ENVIRO					50
#ifdef XATRIX
#define W_QUADFIRE				50
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				50
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

