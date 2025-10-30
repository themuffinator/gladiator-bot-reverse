//===========================================================================
//
// Name:				omicro_w.c
// Function:		weapon weights for the Demigodess
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						30
#define shotgun						40
#define sshotgun						80
#define machinegun					90
#define chaingun						70
#define grenadelauncher				100
#define rocketlauncher				120
#define hyperblaster					60
#define railgun						80
#define BFG10K							90
#define grenades						95

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			55 
#define self_dmg_grenadelauncher	85
#define self_dmg_rocketlauncher	75

//standard weight if the user had quad damage activated

#define quad_shotgun					60 
#define quad_sshotgun				100
#define quad_machinegun				110
#define quad_chaingun				90
#define quad_grenadelauncher		50
#define quad_rocketlauncher		120
#define quad_hyperblaster			80
#define quad_railgun					85
#define quad_BFG10K					110

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	30
#define pwrshield_BFG10K			90

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		120

// next three are BFG only

#define add_invuln					30 //how much to add if you have invulnerability
#define add_quad						20 
#define add_too_close				-20

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						60
#define chainfist						60
#define proxylauncher				100
#define plasmabeam					60
#define disruptor						88


#include "fw_weap.c"
