//===========================================================================
//
// Name:				zero_w.c
// Function:		weapon weights fpr Zero character
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999050a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						50
#define sshotgun						50
#define machinegun					60
#define chaingun						35
#define grenadelauncher				25
#define rocketlauncher				80
#define hyperblaster					60
#define railgun						80
#define BFG10K							100
#define grenades						25

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			25 
#define self_dmg_grenadelauncher	25
#define self_dmg_rocketlauncher	80

//standard weight if the user had quad damage activated

#define quad_shotgun					50 
#define quad_sshotgun				50
#define quad_machinegun				50
#define quad_chaingun				60
#define quad_grenadelauncher		25
#define quad_rocketlauncher		80
#define quad_hyperblaster			60
#define quad_railgun					80
#define quad_BFG10K					100

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	50
#define pwrshield_BFG10K			50

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		80

// next three are BFG only

#define add_invuln					0 //how much to add if you have invulnerability
#define add_quad						0 
#define add_too_close				-0

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						50
#define chainfist						50
#define proxylauncher				50
#define plasmabeam					50
#define disruptor						50


#include "fw_weap.c"
