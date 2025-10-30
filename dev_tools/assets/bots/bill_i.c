//===========================================================================
//
// Name:				bill_i.c
// Function:		item weights for Bill Gates
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.2
#define FS_ARMOR					0.45
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights
#define W_SHOTGUN					40
#define W_SUPERSHOTGUN			99
#define W_MACHINEGUN				60
#define W_CHAINGUN				70
#define W_GRENADELAUNCHER		50
#define W_ROCKETLAUNCHER		99
#define W_HYPERBLASTER			70
#define W_RAILGUN					60
#define W_BFG10K					10
#define W_GRENADES				20
#ifdef XATRIX
#define W_IONRIPPER				45
#define W_PHALANX					46
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				30
#define W_PROXLAUNCHER			25
#define W_PLASMABEAM				20
#define W_CHAINFIST				35
#define W_DISRUPTOR				40
#endif //ROGUE
//the bot has the weapons, so the weights change a little bit
#define GWW_SHOTGUN				13
#define GWW_SUPERSHOTGUN		14
#define GWW_MACHINEGUN			15
#define GWW_CHAINGUN				16
#define GWW_GRENADELAUNCHER	12
#define GWW_ROCKETLAUNCHER		17
#define GWW_HYPERBLASTER		18
#define GWW_RAILGUN				19
#define GWW_BFG10K				 9
#define GWW_GRENADES				11
#ifdef XATRIX
#define GWW_IONRIPPER			13
#define GWW_PHALANX				14
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				14
#define GWW_PROXLAUNCHER		15
#define GWW_PLASMABEAM			14
#define GWW_CHAINFIST			13
#define GWW_DISRUPTOR			12
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					31
//armor
#define W_REDARMOR				40
#define W_YELLOWARMOR			35
#define W_GREENARMOR				20
#define W_ARMORSHARD				55		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			50
#define W_POWERSCREEN			46
//powerups
#define W_MEGAHEALTH				88
#define W_AMMOPACK				40
#define W_BANDOLIER				39
#define W_QUAD						100
#define W_INVULNERABILITY		96
#define W_SILENCER				36
#define W_REBREATHER				36
#define W_ENVIRO					36
#ifdef XATRIX
#define W_QUADFIRE				40
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				30
#define W_DOUBLE					97
#define W_COMPASS					50
#define W_SPHERE_VENGEANCE		94
#define W_SPHERE_HUNTER			86
#define W_SPHERE_DEFENDER		87
#define W_DOPPLEGANGER			95
#define W_TAG_TOKEN				63
#endif //ROGUE

#define TECH1_WEIGHT				68		// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				100	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				100	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				68		// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

