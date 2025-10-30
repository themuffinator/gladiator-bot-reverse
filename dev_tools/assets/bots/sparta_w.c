//===========================================================================
//
// Name:				sparta_w.c
// Function:
// Programmer:		B.D.Squatt with a little help from Mr. Elusive   ;)
// Last update:	1998-10-19 (by Mr Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						50
#define sshotgun						65
#define machinegun					70
#define chaingun						60
#define grenadelauncher				40
#define rocketlauncher				95
#define hyperblaster					75
#define railgun						85
#define BFG10K							30
#define grenades						25

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			20 
#define self_dmg_grenadelauncher	20
#define self_dmg_rocketlauncher	55

//standard weight if the user had quad damage activated

#define quad_shotgun					50 
#define quad_sshotgun				80
#define quad_machinegun				80
#define quad_chaingun				60
#define quad_grenadelauncher		20
#define quad_rocketlauncher		80
#define quad_hyperblaster			60
#define quad_railgun					60
#define quad_BFG10K					50

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	0
#define pwrshield_BFG10K			0

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		95

// next three are BFG only

#define add_invuln					0 //how much to add if you have invulnerability
#define add_quad						10 
#define add_too_close				-30

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						40
#define chainfist						40
#define proxylauncher				40
#define plasmabeam					40
#define disruptor						40


#include "fw_weap.c"
