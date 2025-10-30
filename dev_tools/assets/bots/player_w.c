//===========================================================================
//
// Name:				player_w.c
// Function:		weapon weights for the player bot character
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19980511a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						30
#define shotgun						50
#define sshotgun						75
#define machinegun					70
#define chaingun						80
#define grenadelauncher				50
#define rocketlauncher				90
#define hyperblaster					85
#define railgun						90
#define BFG10K							100
#define grenades						30

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			30 
#define self_dmg_grenadelauncher	50
#define self_dmg_rocketlauncher	90

//standard weight if the user had quad damage activated

#define quad_shotgun					50 
#define quad_sshotgun				75
#define quad_machinegun				70
#define quad_chaingun				80
#define quad_grenadelauncher		50
#define quad_rocketlauncher		90
#define quad_hyperblaster			85
#define quad_railgun					90
#define quad_BFG10K					100

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	85
#define pwrshield_BFG10K			100

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		90

// next three are BFG only

#define add_invuln					0 //how much to add if you have invulnerability
#define add_quad						0 
#define add_too_close				0

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						50
#define chainfist						50
#define proxylauncher				50
#define plasmabeam					50
#define disruptor						50


#include "fw_weap.c"
