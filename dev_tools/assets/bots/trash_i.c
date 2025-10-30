//===========================================================================
//
// Name:				trash_i.c
// Function:		item weights for thrash
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.35
#define FS_ARMOR					0.55
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights
#define W_SHOTGUN					20
#define W_SUPERSHOTGUN			45
#define W_MACHINEGUN				50
#define W_CHAINGUN				90
#define W_GRENADELAUNCHER		30
#define W_ROCKETLAUNCHER		70
#define W_HYPERBLASTER			80
#define W_RAILGUN					60
#define W_BFG10K					100
#define W_GRENADES				15
#ifdef XATRIX
#define W_IONRIPPER				95
#define W_PHALANX					65
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				55
#define W_PROXLAUNCHER			35
#define W_PLASMABEAM				85
#define W_CHAINFIST				25
#define W_DISRUPTOR				99
#endif //ROGUE
//bot has the weapon; so the weights are as follows
#define GWW_SHOTGUN				10
#define GWW_SUPERSHOTGUN		11
#define GWW_MACHINEGUN			13
#define GWW_CHAINGUN				14
#define GWW_GRENADELAUNCHER	12
#define GWW_ROCKETLAUNCHER		21
#define GWW_HYPERBLASTER		30
#define GWW_RAILGUN				24
#define GWW_BFG10K				100
#define GWW_GRENADES				 9
#ifdef XATRIX
#define GWW_IONRIPPER			15
#define GWW_PHALANX				16
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				18
#define GWW_PROXLAUNCHER		19
#define GWW_PLASMABEAM			21
#define GWW_CHAINFIST			22
#define GWW_DISRUPTOR			34
#endif //ROGUE

/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					21
//armor
#define W_REDARMOR				40
#define W_YELLOWARMOR			30
#define W_GREENARMOR				25
#define W_ARMORSHARD				6		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			37
#define W_POWERSCREEN			36
//powerups
#define W_MEGAHEALTH				80
#define W_AMMOPACK				40
#define W_BANDOLIER				35
#define W_QUAD						88
#define W_INVULNERABILITY		99
#define W_SILENCER				21
#define W_REBREATHER				31
#define W_ENVIRO					15
#ifdef XATRIX
#define W_QUADFIRE				97
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				42
#define W_DOUBLE					44
#define W_COMPASS					12
#define W_SPHERE_VENGEANCE		55
#define W_SPHERE_HUNTER			55
#define W_SPHERE_DEFENDER		55
#define W_DOPPLEGANGER			66
#define W_TAG_TOKEN				50
#endif //ROGUE

#define TECH1_WEIGHT				40	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				55	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				60	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				75	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

