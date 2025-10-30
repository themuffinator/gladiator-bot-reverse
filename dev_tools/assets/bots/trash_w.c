//===========================================================================
//
// Name:				trash_w.c
// Function:		Weapon weights for trash bot characters
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						15
#define shotgun						20
#define sshotgun						40
#define machinegun					50
#define chaingun						90
#define grenadelauncher				30
#define rocketlauncher				70
#define hyperblaster					80
#define railgun						60
#define BFG10K							100
#define grenades						15

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			10 
#define self_dmg_grenadelauncher	20
#define self_dmg_rocketlauncher	50

//standard weight if the user had quad damage activated

#define quad_shotgun					20 
#define quad_sshotgun				40
#define quad_machinegun				50
#define quad_chaingun				90
#define quad_grenadelauncher		30
#define quad_rocketlauncher		70
#define quad_hyperblaster			85
#define quad_railgun					60
#define quad_BFG10K					100

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	20
#define pwrshield_BFG10K			40

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		70

// next three are BFG only

#define add_invuln					0 //how much to add if you have invulnerability
#define add_quad						0 
#define add_too_close			 -10

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						40
#define chainfist						15
#define proxylauncher				30
#define plasmabeam					70
#define disruptor						40


#include "fw_weap.c"
