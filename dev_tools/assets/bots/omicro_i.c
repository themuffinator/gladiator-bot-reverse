//===========================================================================
//
// Name:				omicro_i.c
// Function:		contains the item weights for the demigoddess
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990510b (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define FS_HEALTH					0.5
#define FS_ARMOR					0.951
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4
//weapong weights
#define W_SHOTGUN					40
#define W_SUPERSHOTGUN			80
#define W_MACHINEGUN				90
#define W_CHAINGUN				70
#define W_GRENADELAUNCHER		100
#define W_ROCKETLAUNCHER		120
#define W_HYPERBLASTER			60
#define W_RAILGUN					80
#define W_BFG10K					90
#define W_GRENADES				95
#ifdef XATRIX
#define W_IONRIPPER				75
#define W_PHALANX					65
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				60
#define W_PROXLAUNCHER			60
#define W_PLASMABEAM				100
#define W_CHAINFIST				60
#define W_DISRUPTOR				88
#endif //ROGUE
//when she already has the weapon
#define GWW_SHOTGUN				35
#define GWW_SUPERSHOTGUN		35
#define GWW_MACHINEGUN			35
#define GWW_CHAINGUN				35
#define GWW_GRENADELAUNCHER	55
#define GWW_ROCKETLAUNCHER		55
#define GWW_HYPERBLASTER		35
#define GWW_RAILGUN				40
#define GWW_BFG10K				55
#define GWW_GRENADES				55
#ifdef XATRIX
#define GWW_IONRIPPER			35
#define GWW_PHALANX				35
#endif //XATRIX
#ifdef ROGUE
#define GWW_ETFRIFLE				35
#define GWW_PROXLAUNCHER		35
#define GWW_PLASMABEAM			35
#define GWW_CHAINFIST			35
#define GWW_DISRUPTOR			35
#endif //ROGUE
/*
	ammo: I had MrE. put cells in because they are not only ammo, they power the powershield and
	powerscreen as well, giving them (the cells) extra value...to some people...and bots characters. 
*/

#define W_CELLS					41
//armor
#define W_REDARMOR				95
#define W_YELLOWARMOR			30
#define W_GREENARMOR				25
#define W_ARMORSHARD				11		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			99
#define W_POWERSCREEN			99
//powerups
#define W_MEGAHEALTH				110
#define W_AMMOPACK				60
#define W_BANDOLIER				55
#define W_QUAD						95
#define W_INVULNERABILITY		95
#define W_SILENCER				25
#define W_REBREATHER				45
#define W_ENVIRO					44
#ifdef XATRIX
#define W_QUADFIRE				88
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				45
#define W_DOUBLE					77
#define W_COMPASS					40
#define W_SPHERE_VENGEANCE		80
#define W_SPHERE_HUNTER			80
#define W_SPHERE_DEFENDER		80
#define W_DOPPLEGANGER			80
#define W_TAG_TOKEN				50
#endif //ROGUE

#define TECH1_WEIGHT				20	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				99	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				70	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				20	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50

//include the fuzzy relations
#include "fw_items.c"

