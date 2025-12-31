//===========================================================================
//
// Name:				weapons.c
// Function:		weapon configuration
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-12-16
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define VEC_ORIGIN						{0, 0, 0}
//projectile flags
#define PFL_WINDOWDAMAGE			1		//projectile damages through window
#define PFL_RETURN					2		//set when projectile returns to owner
//weapon flags
#define WFL_FIRERELEASED			1		//set when projectile is fired with key-up event
//damage types
#define DAMAGETYPE_IMPACT			1		//damage on impact
#define DAMAGETYPE_RADIAL			2		//radial damage
#define DAMAGETYPE_VISIBLE			4		//damage to all entities visible to the projectile
#define DAMAGETYPE_IGNOREARMOR	8		//projectile goes right through armor

//===========================================================================
// Blaster
//===========================================================================

projectileinfo //for Blaster
{
	name					"blasterbolt"
	model					"models/objects/laser/tris.md2"
	flags					0
	gravity				0
#if DEATHMATCH
	damage				15						//p_weapon.c: deathmatch: 15 otherwise 10
#else
	damage				10
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Blaster
{
	name					"Blaster"
	model					"models/weapons/v_blast/tris.md2"
	level					0
	weaponindex			INVENTORY_BLASTER	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"blasterbolt"
	numprojectiles		1
	hspread				0
	vspread				0
	speed					1000					//p_weapon.c: Blaster_Fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{24, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			0
	ammoindex			INVENTORY_NONE
	activate				0.5					//FIXME: get correct value
	reload				0.5					//p_weapon.c: Weapon_Blaster fire_frames[]
	spinup				0.5					//p_weapon.c: Weapon_Blaster fire_frames[]
	spindown				0
} //end weaponinfo

//===========================================================================
// Shotgun
//===========================================================================

projectileinfo //for Shotgun
{
	name					"shotgunbullet"
	model					""
	flags					0
	gravity				0
	damage				4						//p_weapon.c: weapon_shotgun_fire
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Shotgun
{
	name					"Shotgun"
	model					"models/weapons/v_shotg/tris.md2"
	level					0
	weaponindex			INVENTORY_SHOTGUN	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"shotgunbullet"
	numprojectiles		12						//g_local.h: DEFAULT_DEATHMATCH_SHOTGUN_COUNT
	hspread				3.5					//p_weapon.c: hspread = 500, g_weapon.c: 8192
	vspread				3.5					//p_weapon.c: vspread = 500, g_weapon.c: 8192
	speed					0						//g_weapon.c: direct impact
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1
	ammoindex			INVENTORY_SHELLS
	activate				0.5					//FIXME: get correct value
	reload				1.1					//p_weapon.c: Weapon_Shotgun fire_frames[]
	spinup				0.1					//p_weapon.c: Weapon_Shotgun fire_frames[]
	spindown				0
} //end weaponinfo

//===========================================================================
// Super Shotgun
//===========================================================================

projectileinfo //for Super Shotgun
{
	name					"supershotgunbullet"
	model					""
	flags					0
	gravity				0
	damage				6						//p_weapon.c: weapon_supershotgun_fire
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Super Shotgun
{
	name					"Super Shotgun"
	model					"models/weapons/v_shotg2/tris.md2"
	level					0
	weaponindex			INVENTORY_SUPERSHOTGUN	//g_items.c: gitem_t	itemlist[] =
	projectile			"supershotgunbullet"
	flags					0
	numprojectiles		20						//g_local.h: DEFAULT_DEATHMATCH_SHOTGUN_COUNT
	hspread				7.0					//p_weapon.c: hspread = 1000, g_weapon.c: 8192
	vspread				3.5					//p_weapon.c: vspread = 500, g_weapon.c: 8192
	speed					0						//g_weapon.c: direct impact
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			2
	ammoindex			INVENTORY_SHELLS
	reload				1.1					//p_weapon.c: Weapon_Shotgun fire_frames[]
	activate				0.5					//FIXME: get correct value
	spinup				0.1					//p_weapon.c: Weapon_Shotgun fire_frames[]
	spindown				0
} //end weaponinfo

//===========================================================================
// Machinegun
//===========================================================================

projectileinfo //for Machinegun
{
	name					"machinegunbullet"
	model					""
	flags					0
	gravity				0
	damage				8						//p_weapon.c: Machinegun_Fire
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Machinegun
{
	name					"Machinegun"
	model					"models/weapons/v_machn/tris.md2"
	level					0
	weaponindex			INVENTORY_MACHINEGUN	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"machinegunbullet"
	numprojectiles		1						//g_local.h: DEFAULT_DEATHMATCH_SHOTGUN_COUNT
	hspread				2.1					//p_weapon.c: hspread = 300, g_weapon.c: 8192
	vspread				3.5					//p_weapon.c: vspread = 500, g_weapon.c: 8192
	speed					0						//g_weapon.c: direct impact
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c: Machinegun_Fire
	ammoindex			INVENTORY_BULLETS	//g_items.c: gitem_t	itemlist[] =
	activate				0.5					//FIXME: get correct value
	reload				0.1					//p_weapon.c: Weapon_Machinegun fire_frames[]
	spinup				0.1					//p_weapon.c: Weapon_Machinegun fire_frames[]
	spindown				0
} //end weaponinfo

//===========================================================================
// Chaingun
//===========================================================================

projectileinfo //for Chaingun
{
	name					"chaingunbullet"
	model					""
	flags					0
	gravity				0
	damage				6						//p_weapon.c: Machinegun_Fire (deatmatch)
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Chaingun
{
	name					"Chaingun"
	model					"models/weapons/v_chain/tris.md2"
	level					0
	weaponindex			INVENTORY_CHAINGUN	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"chaingunbullet"
	numprojectiles		3						//p_weapon.c: Chaingun_Fire
	hspread				2.1					//p_weapon.c: hspread = 300, g_weapon.c: 8192
	vspread				3.5					//p_weapon.c: vspread = 500, g_weapon.c: 8192
	speed					0						//g_weapon.c: direct impact
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 7, -8}			//actually 4 * random() is added to y and z
	angleoffset			VEC_ORIGIN
	ammoamount			3						//p_weapon.c: Chaingun_Fire
	ammoindex			INVENTORY_BULLETS	//g_items.c: gitem_t	itemlist[] =
	activate				0.5					//FIXME: get correct value
	reload				0.1					//p_weapon.c: Weapon_Chaingun fire_frames[]
	spinup				0.5					//p_weapon.c: Weapon_Chaingun fire_frames[]
	spindown				0.5					//p_weapon.c: Weapon_Chaingun fire_frames[]
} //end weaponinfo

//===========================================================================
// Grenade Launcher
//===========================================================================

projectileinfo //for Grenade Launcher
{
	name					"grenade"
	model					"models/objects/grenade/tris.md2"
	flags					0
	gravity				1
	damage				120					//p_weapon.c: weapon_grenadelauncher_fire
	radius				160					//p_weapon.c: weapon_grenadelauncher_fire
	visdamage			0
	damagetype			$evalint(DAMAGETYPE_IMPACT|DAMAGETYPE_RADIAL)	//g_weapon.c: impact and radial damage
	healthinc			0
	push					0
	detonation			2.5					//p_weapon.c: weapon_grenadelauncher_fire
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Grenade Launcher
{
	name					"Grenade Launcher"
	model					"models/weapons/v_launch/tris.md2"
	level					0
	weaponindex			INVENTORY_GRENADELAUNCHER	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"grenade"			//see above
	numprojectiles		1						//p_weapon.c: weapon_grenadelauncher_fire
	hspread				0
	vspread				0
	speed					600					//p_weapon.c: weapon_grenadelauncher_fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{8, 8, -8}
	angleoffset			VEC_ORIGIN
	extrazvelocity		200					//g_weapon.c: fire_grenade
	ammoamount			1						//p_weapon.c: weapon_grenadelauncher_fire
	ammoindex			INVENTORY_GRENADES//g_items.c: gitem_t	itemlist[] = ("grenades")
	reload				1.1					//p_weapon.c: Weapon_GrenadeLauncher fire_frames[]
	activate				0.5					//FIXME: get correct value
	spinup				0.1					//p_weapon.c: Weapon_GrenadeLauncher fire_frames[]
	spindown				0.0
} //end weaponinfo

//===========================================================================
// Rocket Launcher
//===========================================================================

projectileinfo //for Rocket Launcher
{
	name					"rocket"
	model					"models/objects/rocket/tris.md2"
	flags					0
	gravity				0
	damage				110					//p_weapon.c: Weapon_RocketLauncher_Fire
	radius				120					//p_weapon.c: Weapon_RocketLauncher_Fire
	visdamage			0
	damagetype			$evalint(DAMAGETYPE_IMPACT|DAMAGETYPE_RADIAL)	//g_weapon.c: impact and radial damage
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Rocket Launcher
{
	name					"Rocket Launcher"
	model					"models/weapons/v_rocket/tris.md2"
	level					0
	weaponindex			INVENTORY_ROCKETLAUNCHER	//g_items.c: gitem_t	itemlist[] =
	projectile			"rocket"				//see above
	flags					0
	numprojectiles		1						//p_weapon.c: Weapon_RocketLauncher_Fire
	hspread				0
	vspread				0
	speed					650					//p_weapon.c: Weapon_RocketLauncher_Fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{8, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c: Weapon_RocketLauncher_Fire
	ammoindex			INVENTORY_ROCKETS	//g_items.c: gitem_t	itemlist[] = ("Rockets")
	reload				0.8					//p_weapon.c: Weapon_RocketLauncher fire_frames[]
	activate				0.5					//FIXME: get correct value
	spinup				0.1					//p_weapon.c: Weapon_RocketLauncher fire_frames[]
	spindown				0.0
} //end weaponinfo

//===========================================================================
// Hyper Blaster
//===========================================================================

projectileinfo //for Hyper Blaster
{
	name					"hyperblasterbolt"
	model					"models/objects/laser/tris.md2"
	flags					0
	gravity				0
#if DEATHMATCH
	damage				15						//deathmatch: 15 otherwise 20
#else
	damage				20
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Hyper Blaster
{
	name					"HyperBlaster"
	model					"models/weapons/v_hyperb/tris.md2"
	level					0
	weaponindex			INVENTORY_HYPERBLASTER	//see g_items.c: gitem_t	itemlist[] = 
	flags					0
	projectile			"hyperblasterbolt"
	numprojectiles		1
	hspread				0
	vspread				0
	speed					1000					//see p_weapon.c: Blaster_Fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 0, 0}	//actually it circles with radius 4 around this offset
	angleoffset			VEC_ORIGIN
	ammoamount			1						//one cell
	ammoindex			INVENTORY_CELLS	//cells, see g_items.c
	activate				0.5					//FIXME: get correct value
	reload				0.1					//see p_weapon.c: Weapon_HyperBlaster fire_frames[]
	spinup				0.1					//see p_weapon.c: Weapon_HyperBlaster fire_frames[]
	spindown				0.0
} //end weaponinfo

//===========================================================================
// Railgun
//===========================================================================

projectileinfo //for Railgun
{
	name					"rail"
	model					""
	flags					0
	gravity				0
	damage				100					//p_weapon.c: weapon_railgun_fire (deathmatch)
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: damage on impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Railgun
{
	name					"Railgun"
	model					"models/weapons/v_rail/tris.md2"
	level					0
	weaponindex			INVENTORY_RAILGUN	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"rail"				//see above
	numprojectiles		1						//p_weapon.c: weapon_railgun_fire
	hspread				0
	vspread				0
	speed					0						//instant hit
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 7, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c: weapon_railgun_fire
	ammoindex			INVENTORY_SLUGS	//g_items.c: gitem_t	itemlist[] = ("slugs")
	activate				0.5					//FIXME: get correct value
	reload				1.5					//p_weapon.c: Weapon_Railgun
	spinup				0.1					//p_weapon.c: Weapon_Railgun
	spindown				0.0
} //end weaponinfo

//===========================================================================
// BFG10K
//===========================================================================

projectileinfo //for BFG10K
{
	name					"flash"
	model					"sprites/s_bfg1.sp2"
	flags					0
	gravity				0
	damage				200					//p_weapon.c: weapon_bfg_fire (deathmatch)
	radius				1000					//p_weapon.c: weapon_bfg_fire
	visdamage			5						//g_weapon.c: bfg_think
	damagetype			$evalint(DAMAGETYPE_IMPACT|DAMAGETYPE_RADIAL|DAMAGETYPE_VISIBLE) //g_weapon.c: impact, radial and visible
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //BFG10K
{
	name					"BFG10K"
	model					"models/weapons/v_bfg/tris.md2"
	level					0
	weaponindex			INVENTORY_BFG10K	//g_items.c: gitem_t	itemlist[] =
	projectile			"flash"				//see above
	flags					0
	numprojectiles		1						//p_weapon.c: weapon_bfg_fire
	hspread				0
	vspread				0
	speed					400					//p_weapon.c: weapon_bfg_fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{8, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			50						//p_weapon.c: weapon_bfg_fire
	ammoindex			INVENTORY_CELLS	//g_items.c: gitem_t	itemlist[] = ("Cells")
	activate				0.5					//FIXME: get correct value
	reload				2.4					//p_weapon.c: Weapon_BFG
	spinup				0.9					//p_weapon.c: Weapon_BFG
	spindown				0.0
} //end weaponinfo

//===========================================================================
// Hand Grenades
//===========================================================================

projectileinfo //for Hand Grenades
{
	name					"handgrenade"
	model					"models/objects/grenade2/tris.md2"
	flags					0
	gravity				1
	damage				125					//p_weapon.c: weapon_grenadelauncher_fire
	radius				165					//p_weapon.c: weapon_grenadelauncher_fire
	visdamage			0
	damagetype			$evalint(DAMAGETYPE_IMPACT|DAMAGETYPE_RADIAL)	//g_weapon.c: impact and radial damage
	healthinc			0
	push					0
	detonation			3.0					//p_weapon.c: weapon_grenadelauncher_fire
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Hand Grenades
{
	name					"grenades"
	model					"models/weapons/v_handgr/tris.md2"
	level					0
	weaponindex			INVENTORY_GRENADES//g_items.c: gitem_t	itemlist[] =
	flags					WFL_FIRERELEASED	//fired when released
	projectile			"handgrenade"		//see above
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					400					//p_weapon.c: weapon_grenade_fire
	acceleration		133					//p_weapon.c: weapon_grenade_fire
	recoil				VEC_ORIGIN
	offset				{8, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c: weapon_grenade_fire
	ammoindex			INVENTORY_GRENADES//g_items.c: gitem_t	itemlist[] = ("grenades")
	activate				0.5					//FIXME: get correct value
	reload				1.1					//p_weapon.c: Weapon_Grenade
	spinup				1.1					//p_weapon.c: Weapon_Grenade
	spindown				0.0
} //end weaponinfo

#ifdef XATRIX

//===========================================================================
// Ionripper (shoots bouncing red cells)
//===========================================================================

projectileinfo //ions
{
	name					"ion"
	model					"models/objects/boomrang/tris.md2"
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				30
#else
	damage				50
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Ionripper
{
	name					"Ionripper"
	model					"models/weapons/v_boomer/tris.md2"
	level					0
	weaponindex			INVENTORY_IONRIPPER	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"ion"					//see above
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					500					//p_weapon.c: weapon_grenade_fire
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{16, 7, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			2						//g_items.c: itemlist ->quantity
	ammoindex			INVENTORY_CELLS	//g_items.c: gitem_t	itemlist[]
	activate				0.5					//FIXME: get correct value
	reload				0.2					//p_weapon.c: Weapon_Grenade
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Phalanx (sort of double rocket launcher)
//===========================================================================

projectileinfo //plasma
{
	name					"plasma"
	model					"models/objects/boomrang/tris.md2"
	flags					0
	gravity				1
	damage				70
	radius				120
	visdamage			0
	damagetype			$evalint(DAMAGETYPE_IMPACT|DAMAGETYPE_RADIAL)	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
}

weaponinfo //Phalanx
{
	name					"Phalanx"
	model					"models/weapons/v_shotx/tris.md2"
	level					0
	weaponindex			INVENTORY_PHALANX	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"plasma"				//see above
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					725					//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			INVENTORY_MAGSLUGS//g_items.c: gitem_t	itemlist[]
	activate				0.8					//p_weapon.c:
	reload				0.8					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Trap (opzuig apparaat)
//===========================================================================

projectileinfo //trap
{
	name					"trap"
} //end projectileinfo

weaponinfo //Weapon trap
{
	name					"Trap"
	level					0

	projectile			"trap"
} //end weaponinfo

#endif //XATRIX

#ifdef ROGUE

//===========================================================================
// ETF Rifle
//===========================================================================

projectileinfo //etf
{
	name					"etf"
	model					"models/proj/flechette/tris.md2"
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				10
#else
	damage				10
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
} //end projectileinfo

weaponinfo //ETF Rifle
{
	name					"ETF Rifle"
	model					"models/weapons/v_etf_rifle/tris.md2"
	level					0
	weaponindex			INVENTORY_ETFRIFLE	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"etf"
	numprojectiles		2						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					750					//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{15, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			INVENTORY_FLECHETTES//g_items.c: gitem_t	itemlist[]
	activate				0.8					//p_weapon.c:
	reload				0.6					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Prox Launcher
//===========================================================================

projectileinfo //proximity
{
	name					"proximity"
	model					"models/weapons/g_prox/tris.md2"
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				90
#else
	damage				90
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
} //end projectileinfo

weaponinfo //Prox Launcher
{
	name					"Prox Launcher"
	model					"models/weapons/v_plaunch/tris.md2"
	level					0
	weaponindex			INVENTORY_PROXLAUNCHER	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"proximity"
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					750					//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{2, 6, -14}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			INVENTORY_PROX		//g_items.c: gitem_t	itemlist[]
	activate				0.8					//p_weapon.c:
	reload				0.6					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Tesla (sorta hand grenade)
//===========================================================================

projectileinfo //tesla
{
	name					"tesla"
} //end projectileinfo

weaponinfo //Weapon tesla
{
	name					"Tesla"
	level					0

	projectile			"tesla"
} //end weaponinfo

//===========================================================================
// Plasma Beam
//===========================================================================

projectileinfo //plasmabeam
{
	name					"plasmabeam"
	model					""		//TE_HEATBEAM_STEAM
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				15
#else
	damage				15
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
} //end projectileinfo

weaponinfo //Plasma Beam
{
	name					"Plasma Beam"
	model					"models/weapons/v_beamer/tris.md2"
	level					0
	weaponindex			INVENTORY_PLASMABEAM	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"plasmabeam"
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					0						//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{7, 2, -3}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			INVENTORY_CELLS	//g_items.c: gitem_t	itemlist[]
	activate				0.8					//p_weapon.c:
	reload				0.6					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Chainfist
//===========================================================================

projectileinfo //Chainfist
{
	name					"chainfistdamage"
	model					""
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				30
#else
	damage				15
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
} //end projectileinfo

weaponinfo //Chainfist
{
	name					"Chainfist"
	model					"models/weapons/v_chainf/tris.md2"
	level					0
	weaponindex			INVENTORY_CHAINFIST	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"chainfistdamage"
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					0						//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{0, 8, -4}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			0						//no ammo
	activate				0.8					//p_weapon.c:
	reload				0.6					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

//===========================================================================
// Disruptor
//===========================================================================

projectileinfo //tracker
{
	name					"tracker"
	model					"models/proj/disintegrator/tris.md2"
	flags					0
	gravity				1
#ifdef DEATHMATCH
	damage				30
#else
	damage				45
#endif //DEATHMATCH
	radius				0
	visdamage			0
	damagetype			DAMAGETYPE_IMPACT	//g_weapon.c: impact
	healthinc			0
	push					0
	detonation			0
	bounce				0
	bouncefric			0
	bouncestop			0
} //end projectileinfo

weaponinfo //Disruptor
{
	name					"Disruptor"
	model					"models/weapons/v_dist/tris.md2"
	level					0
	weaponindex			INVENTORY_DISRUPTOR	//g_items.c: gitem_t	itemlist[] =
	flags					0
	projectile			"tracker"
	numprojectiles		1						//p_weapon.c: weapon_grenade_fire
	hspread				0
	vspread				0
	speed					0						//p_weapon.c:
	acceleration		0
	recoil				VEC_ORIGIN
	offset				{24, 8, -8}
	angleoffset			VEC_ORIGIN
	ammoamount			1						//p_weapon.c:
	ammoindex			INVENTORY_ROUNDS
	activate				0.8					//p_weapon.c:
	reload				0.6					//p_weapon.c:
	spinup				0
	spindown				0
} //end weaponinfo

#endif //ROGUE

