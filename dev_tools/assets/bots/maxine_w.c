//===========================================================================
//
// Name:				maxine_w.c
// Function:
// Programmer:		BD Squatt with a little bit of help from Elusive  :)
// Last update:	1998-10-19 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						10
#define shotgun						20
#define sshotgun						30
#define machinegun					40
#define chaingun						50
#define grenadelauncher				60
#define rocketlauncher				70
#define hyperblaster					80
#define railgun						90
#define BFG10K							100
#define grenades						15

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			15
#define self_dmg_grenadelauncher	30
#define self_dmg_rocketlauncher	35

//standard weight if the user had quad damage activated

#define quad_shotgun					20
#define quad_sshotgun				30
#define quad_machinegun				80
#define quad_chaingun				70
#define quad_grenadelauncher		20
#define quad_rocketlauncher		60
#define quad_hyperblaster			70
#define quad_railgun					75
#define quad_BFG10K					60

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	35
#define pwrshield_BFG10K			40

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		90

// next three are BFG only

#define add_invuln					50//how much to add if you have invulnerability
#define add_quad						20 
#define add_too_close				-10

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						40
#define chainfist						60
#define proxylauncher				75
#define plasmabeam					85
#define disruptor						99


#include "fw_weap.c"
