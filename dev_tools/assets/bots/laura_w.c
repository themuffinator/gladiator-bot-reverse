//===========================================================================
//
// Name:				laura_w.c
// Function:		Laura's weapon weights file
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

/*
	standard weapon weights...the higher the number, the more likely that the bot
	will want to aquire the weapon.
*/

#define blaster						20
#define shotgun						75
#define sshotgun						95
#define machinegun					120
#define chaingun						100
#define grenadelauncher				30
#define rocketlauncher				40
#define hyperblaster					50
#define railgun						65
#define BFG10K							80
#define grenades						25

/*
	the weights of certain weapons change if the weapon will cause damage to the user when used,
	i.e. like when you bump into a wall and fire say...the BFG10K...go ahead...try it!
*/

#define self_dmg_grenades			25 
#define self_dmg_grenadelauncher	25
#define self_dmg_rocketlauncher	21

/*
	standard weight if the user had quad damage activated. Some weapons actually
	significantly more powerful when the quad is active...like the shotgut
*/

#define quad_shotgun					75 
#define quad_sshotgun				95
#define quad_machinegun				120
#define quad_chaingun				100
#define quad_grenadelauncher		15
#define quad_rocketlauncher		40
#define quad_hyperblaster			55
#define quad_railgun					30
#define quad_BFG10K					85

/*
	standard weight if the user possesse a working powershield. Some people actually don't like
	to use the energy slurping BFG10K or hyperblaster when they have the powerarmor, since the
	likelyhood of survival is higher with powerarmor "on" and a shotgun then survival with depleted
	powerarmor after discharging a BFG10K.
*/

#define pwrshield_hyperblaster	35
#define pwrshield_BFG10K			25

/*
	weight if the user has invulnerability and a rocket launcher. Isn't it funny how suicidal one gets
	when in possession of invulnerability and a loaded rocket launcher....I know I do.
*/

#define invul_rocketlauncher		80

// next three are BFG only

#define add_invuln					50 //how much to add if you have invulnerability
#define add_quad						25 
#define add_too_close				-30

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						50
#define chainfist						35
#define proxylauncher				50
#define plasmabeam					40
#define disruptor						35

#include "fw_weap.c"
