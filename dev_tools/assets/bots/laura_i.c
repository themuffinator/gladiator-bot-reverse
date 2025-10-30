//===========================================================================
//
// Name:				laura_i.c
// Function:		this file describes the likelyhood that a bot character will
//						pick up a certain item, with 0 being (almost) never and 100 being more
//						often (sky is the limit here folks, i limit myself to 120)
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


// COMMENTARY BY SQUATT NOT BY MR ELUSIVE



#include "inv.h" //trust me...this file needs this or it won't work
#include "game.h" //yep...just include this..trust me...I know what I'm doing

/*
	FS_HEALTH and FS_ARMOR <not finished yet...subject to change>
*/
	
#define FS_HEALTH					0.67
#define FS_ARMOR					0.92
//undecided balance range
#define BR_HEALTH					10		//NOTE: was 8 but changed it for Rhea
#define BR_ARMOR					10		//NOTE: was 8
#define BR_WEAPON					30		//NOTE: was 15
#define BR_POWERUP				15		//NOTE: was 4

/*
	weapon weights: these should be the highest if you want the character to arm
	ittself before doing anything else
*/

#define W_SHOTGUN					75
#define W_SUPERSHOTGUN			95		//120 points of instant damage...but only up close
#define W_MACHINEGUN				120	//this weapon does the most damage per time unit
#define W_CHAINGUN				100
#define W_GRENADELAUNCHER		30
#define W_ROCKETLAUNCHER		40		//good weapon damage...but slow damage per time unit
#define W_HYPERBLASTER			50
#define W_RAILGUN					65		//120 pts of damage, but with an extremely narrow but
											
#define W_BFG10K					80		//easy for wide open places, difficult for smaller
												//areas; and it is slow as well
#define W_GRENADES				25
#ifdef XATRIX
#define W_IONRIPPER				0
#define W_PHALANX					5
#endif //XATRIX
#ifdef ROGUE
#define W_ETFRIFLE				50
#define W_PROXLAUNCHER			35
#define W_PLASMABEAM				50
#define W_CHAINFIST				40
#define W_DISRUPTOR				35
#endif //ROGUE

/*
	these weapon weights come into play when the bot already possesses the weapon; i.e. the
	likelyhood that it will pick up a weapon if it already possesses it.
*/

#define GWW_SHOTGUN				10
#define GWW_SUPERSHOTGUN		10
#define GWW_MACHINEGUN			10
#define GWW_CHAINGUN				10
#define GWW_GRENADELAUNCHER	10
#define GWW_ROCKETLAUNCHER		10
#define GWW_HYPERBLASTER		10
#define GWW_RAILGUN				10
#define GWW_BFG10K				10
#define GWW_GRENADES				10	//grenades are special....being ammo as well as a weapon
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
#define W_REDARMOR				100
#define W_YELLOWARMOR			90
#define W_GREENARMOR				85
#define W_ARMORSHARD				0		//while the significance of the armor shard may be minor, there
												//are always people (including myself) who are compulsive armor
												//shard gatherers.....except when doing one on one...heh heh

/*
	if the following items receive higher values than the initial weapon values, then the bot
	will first seek these items out, then the weapons (if still alive)
*/

//powershields
#define W_POWERSHIELD			85
#define W_POWERSCREEN			80	//we don't know where you can find this, but it is included with quake2
//powerups								  code
#define W_MEGAHEALTH				70
#define W_AMMOPACK				40
#define W_BANDOLIER				35	//sorta like an ammo pack but with less ammo capacity
#define W_QUAD						100
#define W_INVULNERABILITY		100
#define W_SILENCER				25
#define W_REBREATHER				30
#define W_ENVIRO					40
#ifdef XATRIX
#define W_QUADFIRE				50
#endif //XATRIX
#ifdef ROGUE
#define W_IR_GOGGLES				40
#define W_DOUBLE					40
#define W_COMPASS					40
#define W_SPHERE_VENGEANCE		99
#define W_SPHERE_HUNTER			65
#define W_SPHERE_DEFENDER		99
#define W_DOPPLEGANGER			50
#define W_TAG_TOKEN				40
#endif //ROGUE

/*
	with the following 4 weights the bot determines which tech it likes best
	(=the one with the highest value.) This happens only when it encounters a new tech
*/

#define TECH1_WEIGHT				43	// Resistance Tech \ Disruptor Shield
#define TECH2_WEIGHT				44	// Strength Tech \ Power Amplifier
#define TECH3_WEIGHT				42	// Haste Tech \ Time Accel
#define TECH4_WEIGHT				41	// Regeneration Tech \ AutoDoc

#define FLAG_WEIGHT				50	// Mr Elusive told me to leave this 50

//include the fuzzy relations, or else the above is done for naught but the typing exercise itself
#include "fw_items.c"

