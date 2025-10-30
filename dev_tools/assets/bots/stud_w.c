//===========================================================================
//
// Name:				Stud_w.c
// Function:
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-11-09 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						30
#define sshotgun						60
#define machinegun					60
#define chaingun						35
#define grenadelauncher				25
#define rocketlauncher				90
#define hyperblaster					70
#define railgun						90
#define BFG10K							80
#define grenades						25

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			25 
#define self_dmg_grenadelauncher	25
#define self_dmg_rocketlauncher	60

//standard weight if the user had quad damage activated

#define quad_shotgun					50 
#define quad_sshotgun				70
#define quad_machinegun				80
#define quad_chaingun				70
#define quad_grenadelauncher		25
#define quad_rocketlauncher		80
#define quad_hyperblaster			65
#define quad_railgun					70
#define quad_BFG10K					50

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	70
#define pwrshield_BFG10K			80

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		90

// next three are BFG only

#define add_invuln					50 //how much to add if you have invulnerability
#define add_quad						25 
#define add_too_close				-40

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						40
#define chainfist						40
#define proxylauncher				40
#define plasmabeam					40
#define disruptor						40


#include "fw_weap.c"
