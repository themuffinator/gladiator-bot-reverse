//===========================================================================
//
// Name:				bitch_w.c
// Function:		Quad Bitch' weapons of choice
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	19990510a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"

//standard weapon weights
#define blaster						20
#define shotgun						45
#define sshotgun						60
#define machinegun					100
#define chaingun						50
#define grenadelauncher				55
#define rocketlauncher				75
#define hyperblaster					100
#define railgun						95
#define BFG10K							55
#define grenades						40

//standard weight if the weapon will cause damage to self when used

#define self_dmg_grenades			23 
#define self_dmg_grenadelauncher	24
#define self_dmg_rocketlauncher	26

//standard weight if the user had quad damage activated

#define quad_shotgun					65 
#define quad_sshotgun				80
#define quad_machinegun				120
#define quad_chaingun				70
#define quad_grenadelauncher		25
#define quad_rocketlauncher		100
#define quad_hyperblaster			120
#define quad_railgun					75
#define quad_BFG10K					105

//standard weight if the user possesse a working powershield

#define pwrshield_hyperblaster	70
#define pwrshield_BFG10K			35

//weight if the user has invulnerability and a rocket launcher

#define invul_rocketlauncher		78

// next three are BFG only

#define add_invuln					50 //how much to add if you have invulnerability
#define add_quad						25 
#define add_too_close				-25

// Xatrix support

//not nessesary...xatrix weapons equal roughtly to Q2 weapons

//Rogue Support

#define etf_rifle						65
#define chainfist						70
#define proxylauncher				55
#define plasmabeam					75
#define disruptor						69


#include "fw_weap.c"
