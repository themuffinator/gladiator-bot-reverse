//===========================================================================
//
// Name:				messia_w.c
// Function:		weapon weights for the Shotgun Messiah
// Programmer:		BD Squatt with a little bit of help from Elusive  :)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						70
#define sshotgun						100
#define machinegun					95
#define chaingun						80
#define grenadelauncher				30
#define rocketlauncher				50
#define hyperblaster					60
#define railgun						65
#define BFG10K							40
#define grenades						0

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			30
#define self_dmg_grenadelauncher	30
#define self_dmg_rocketlauncher	40

//standard weight if the user had quad damage activated

#define quad_shotgun					80
#define quad_sshotgun				100
#define quad_machinegun				90
#define quad_chaingun				70
#define quad_grenadelauncher		30
#define quad_rocketlauncher		60
#define quad_hyperblaster			50
#define quad_railgun					75
#define quad_BFG10K					40

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	50
#define pwrshield_BFG10K			40

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		60

// next three are BFG only

#define add_invuln					10//how much to add if you have invulnerability
#define add_quad						20 
#define add_too_close				-20

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						99
#define chainfist						70
#define proxylauncher				35
#define plasmabeam					65
#define disruptor						40


#include "fw_weap.c"
