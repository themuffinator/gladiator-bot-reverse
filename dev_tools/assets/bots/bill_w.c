//===========================================================================
//
// Name:				bill_w.c
// Function:		Weapon weights for Bill Gates
// Programmer:		BD Squatt with a little bit of help from Elusive  :)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						40
#define sshotgun						80
#define machinegun					60
#define chaingun						70
#define grenadelauncher				50
#define rocketlauncher				80
#define hyperblaster					70
#define railgun						60
#define BFG10K							100
#define grenades						20

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			20
#define self_dmg_grenadelauncher	30
#define self_dmg_rocketlauncher	40

//standard weight if the user had quad damage activated

#define quad_shotgun					60
#define quad_sshotgun				70
#define quad_machinegun				80
#define quad_chaingun				95
#define quad_grenadelauncher		20
#define quad_rocketlauncher		90
#define quad_hyperblaster			80
#define quad_railgun					60
#define quad_BFG10K					100

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	50
#define pwrshield_BFG10K			10

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		80

// next three are BFG only; like bill will change his mind....NOT

#define add_invuln					0//how much to add if you have invulnerability
#define add_quad						0 
#define add_too_close				0

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						40
#define chainfist						40
#define proxylauncher				40
#define plasmabeam					40
#define disruptor						40


#include "fw_weap.c"
