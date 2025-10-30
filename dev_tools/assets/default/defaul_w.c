//===========================================================================
//
// Name:				defaul_w.c
// Function:		default weapons file...if you dont like to define your own weights
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990512a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

/*
	standard weapon weights...the higher the number, the more likely that the bot
	will want to aquire that weapon.
*/

#define blaster						20
#define shotgun						30
#define sshotgun						60
#define machinegun					70
#define chaingun						35
#define grenadelauncher				30
#define rocketlauncher				90
#define hyperblaster					55
#define railgun						90
#define BFG10K							80
#define grenades						25

/*
	the weights of certain weapons change if the weapon will cause damage to the user when used,
	i.e. like when you bump into a wall and fire say...the BFG10K...go ahead...try it!
*/

#define self_dmg_grenades			25 
#define self_dmg_grenadelauncher	25
#define self_dmg_rocketlauncher	60

/*
	standard weight if the user had quad damage activated. Some weapons actually
	significantly more powerful when the quad is active...like the shotgut
*/

#define quad_shotgun					50 
#define quad_sshotgun				90
#define quad_machinegun				80
#define quad_chaingun				70
#define quad_grenadelauncher		25
#define quad_rocketlauncher		80
#define quad_hyperblaster			75
#define quad_railgun					70
#define quad_BFG10K					60

/*
	standard weight if the user possesse a working powershield. Some people actually don't like
	to use the energy slurping BFG10K or hyperblaster when they have the powerarmor, since the
	likelyhood of survival is higher with powerarmor "on" and a shotgun then survival with depleted
	powerarmor after discharging a BFG10K.
*/

#define pwrshield_hyperblaster	40
#define pwrshield_BFG10K			40

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
#define chainfist						50
#define proxylauncher				50
#define plasmabeam					50
#define disruptor						50


#include "fw_weap.c"
