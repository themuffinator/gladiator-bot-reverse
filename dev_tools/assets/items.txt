//===========================================================================
//
// Name:				items.c
// Function:		item configuration
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-12-16
// Tab Size:		3 (real tabs)
//===========================================================================

#include "inv.h"
#include "game.h"

#define ITEM_NONE						0
#define ITEM_AMMO						1
#define ITEM_WEAPON					2
#define ITEM_HEALTH					3
#define ITEM_ARMOR					4
#define ITEM_POWERUP					5
#define ITEM_KEY						6
#define ITEM_FLAG						7


//===================================
// ARMOR
//===================================

#if !(DMFLAGS & DF_NO_ARMOR)

iteminfo "item_armor_body"
{
	name					"Body Armor"
	model					"models/items/armor/body/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_ARMORBODY
	respawntime			20
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_armor_combat"
{
	name					"Combat Armor"
	model					"models/items/armor/combat/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_ARMORCOMBAT
	respawntime			20
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_armor_jacket"
{
	name					"Jacket Armor"
	model					"models/items/armor/jacket/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_ARMORJACKET
	respawntime			20
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_armor_shard"
{
	name					"Armor Shard"
	model					"models/items/armor/shard/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_ARMORSHARD
	respawntime			20
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_power_screen"
{
	name					"Power Screen"
	model					"models/items/armor/screen/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_POWERSCREEN
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_power_shield"
{
	name					"Power Shield"
	model					"models/items/armor/shield/tris.md2"
	type					ITEM_ARMOR
	index					INVENTORY_POWERSHIELD
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //!(DMFLAGS & DF_NO_ARMOR)

//===================================
// WEAPONS
//===================================
/*
iteminfo "weapon_blaster"
{
	name					"Blaster"
	model					"models/weapons/g_blast/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_BLASTER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo*/

iteminfo "weapon_shotgun"
{
	name					"Shotgun"
	model					"models/weapons/g_shotg/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_SHOTGUN
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_supershotgun"
{
	name					"Super Shotgun"
	model					"models/weapons/g_shotg2/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_SUPERSHOTGUN
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_machinegun"
{
	name					"Machinegun"
	model					"models/weapons/g_machn/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_MACHINEGUN
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_chaingun"
{
	name					"Chaingun"
	model					"models/weapons/g_chain/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_CHAINGUN
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_grenades"
{
	name					"Grenades"
	model					"models/items/ammo/grenades/medium/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_GRENADES
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_grenadelauncher"
{
	name					"Grenade Launcher"
	model					"models/weapons/g_launch/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_GRENADELAUNCHER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_rocketlauncher"
{
	name					"Rocket Launcher"
	model					"models/weapons/g_rocket/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_ROCKETLAUNCHER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_hyperblaster"
{
	name					"HyperBlaster"
	model					"models/weapons/g_hyperb/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_HYPERBLASTER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_railgun"
{
	name					"Railgun"
	model					"models/weapons/g_rail/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_RAILGUN
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#if !(DMFLAGS & DF_INFINITE_AMMO)

iteminfo "weapon_bfg"
{
	name					"BFG10K"
	model					"models/weapons/g_bfg/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_BFG10K
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //!(DMFLAGS & DF_INFINITE_AMMO)

#ifdef XATRIX

iteminfo "weapon_boomer"
{
	name					"Ionripper"
	model					"models/weapons/g_boom/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_IONRIPPER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_phalanx"
{
	name					"Phalanx"
	model					"models/weapons/g_shotx/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_PHALANX
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //XATRIX

#ifdef ROGUE

iteminfo "weapon_etf_rifle"
{
	name					"ETF Rifle"
	model					"models/weapons/g_etf_rifle/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_ETFRIFLE
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_proxlauncher"
{
	name					"Prox Launcher"
	model					"models/weapons/g_plaunch/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_PROXLAUNCHER
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_plasmabeam"
{
	name					"Plasma Beam"
	model					"models/weapons/g_beamer/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_PLASMABEAM
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_chainfist"
{
	name					"Chainfist"
	model					"models/weapons/g_chainf/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_CHAINFIST
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "weapon_disintegrator"
{
	name					"Disruptor"
	model					"models/weapons/g_dist/tris.md2"
	type					ITEM_WEAPON
	index					INVENTORY_DISRUPTOR
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //ROGUE

//===================================
// AMMO
//===================================

#if !(DMFLAGS & DF_INFINITE_AMMO)

iteminfo "ammo_shells"
{
	name					"Shells"
	model					"models/items/ammo/shells/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_SHELLS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_bullets"
{
	name					"Bullets"
	model					"models/items/ammo/bullets/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_BULLETS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_cells"
{
	name					"Cells"
	model					"models/items/ammo/cells/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_CELLS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_grenades"
{
	name					"Grenades"
	model					"models/items/ammo/grenades/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_GRENADES
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_rockets"
{
	name					"Rockets"
	model					"models/items/ammo/rockets/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_ROCKETS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_slugs"
{
	name					"Slugs"
	model					"models/items/ammo/slugs/medium/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_SLUGS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#ifdef XATRIX

iteminfo "ammo_magslug"
{
	name					"Mag Slug"
	model					"models/objects/ammo/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_MAGSLUGS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_trap"
{
	name					"Trap"
	model					"models/weapons/g_trap/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_TRAP
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //XATRIX

#ifdef ROGUE

iteminfo "ammo_flechettes"
{
	name					"Flechettes"
	model					"models/ammo/am_flechette/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_FLECHETTES
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_prox"
{
	name					"Prox"
	model					"models/ammo/am_prox/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_PROX
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_tesla"
{
	name					"Tesla"
	model					"models/ammo/am_tesl/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_TESLA
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_nuke"
{
	name					"A-M Bomb"
	model					"models/weapons/g_nuke/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_AMBOMB
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "ammo_disruptor"
{
	name					"Rounds"
	model					"models/ammo/am_disr/tris.md2"
	type					ITEM_AMMO
	index					INVENTORY_ROUNDS
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //ROGUE

#endif //!(DMFLAGS & DF_INFINITE_AMMO)

//===================================
// HEALTH
//===================================

#if !(DMFLAGS & DF_NO_HEALTH)

iteminfo "item_health"
{
	name					"Health"
	model					"models/items/healing/medium/tris.md2"
	type					ITEM_HEALTH
	index					INVENTORY_HEALTH
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_health_small"
{
	name					"Health"
	model					"models/items/healing/stimpack/tris.md2"
	type					ITEM_HEALTH
	index					INVENTORY_HEALTH
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_health_large"
{
	name					"Health"
	model					"models/items/healing/large/tris.md2"
	type					ITEM_HEALTH
	index					INVENTORY_HEALTH
	respawntime			30
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_health_mega"
{
	name					"Health"
	model					"models/items/mega_h/tris.md2"
	type					ITEM_HEALTH
	index					INVENTORY_HEALTH
	respawntime			20
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //!(DMFLAGS & DF_NO_HEALTH)

//===================================
// POWERUPS
//===================================

#if !(DMFLAGS & DF_NO_ITEMS)

iteminfo "item_quad"
{
	name					"Quad Damage"
	model					"models/items/quaddama/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_QUAD
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_invulnerability"
{
	name					"Invulnerability"
	model					"models/items/invulner/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_INVULNERABILITY
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_silencer"
{
	name					"Silencer"
	model					"models/items/silencer/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_SILENCER
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_breather"
{
	name					"Rebreather"
	model					"models/items/breather/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_REBREATHER
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_enviro"
{
	name					"Environment Suit"
	model					"models/items/enviro/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_ENVIRONMENTSUIT
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_ancient_head"
{
	name					"Ancient Head"
	model					"models/items/c_head/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_ANCIENTHEAD
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_adrenaline"
{
	name					"Adrenaline"
	model					"models/items/adrenal/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_ANCIENTHEAD
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_bandolier"
{
	name					"Bandolier"
	model					"models/items/band/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_BANDOLIER
	respawntime			60
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_pack"
{
	name					"Ammo Pack"
	model					"models/items/pack/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_AMMOPACK
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#ifdef XATRIX

iteminfo "item_quadfire"
{
	name					"DualFire Damage"
	model					"models/items/quadfire/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_DUALFIREDAMAGE
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //XATRIX

#ifdef ROGUE

iteminfo "item_ir_goggles"
{
	name					"IR Goggles"
	model					"models/items/goggles/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_IRGOGGLES
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_double"
{
	name					"Double Damage"
	model					"models/items/ddamage/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_DOUBLEDAMAGE
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_compass"
{
	name					"compass"
	model					"models/objects/fire/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_COMPASS
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_sphere_vengeance"
{
	name					"vengeance sphere"
	model					"models/items/vengnce/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_VENGEANCESPHERE
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_sphere_hunter"
{
	name					"hunter sphere"
	model					"models/items/hunter/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_HUNTERSPHERE
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_sphere_defender"
{
	name					"defender sphere"
	model					"models/items/defender/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_DEFENDERSPHERE
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_doppleganger"
{
	name					"Doppleganger"
	model					"models/items/dopple/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_DOPPLEGANGER
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "dm_tag_token"
{
	name					"Tag Token"
	model					"models/items/tagtoken/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TAGTOKEN
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "dm_tag_token"
{
	name					"Tag Token"
	model					"models/items/tagtoken/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TAGTOKEN
	respawntime			180
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

#endif //ROGUE

#endif //!(DMFLAGS & DF_NO_ITEMS)

//===================================
// KEYS
//===================================

/*
"Data CD",						"key_data_cd",					21
"Power Cube",					"key_power_cube",				22
"Pyramid Key",					"key_pyramid",					23
"Data Spinner",				"key_data_spinner",			24
"Security Pass",				"key_pass",						25
"Blue Key",						"key_blue_key",				26
"Red Key",						"key_red_key",					27
"Commander's Head",			"key_commander_head",		28
"Airstrike Marker",			"key_airstrike_target",		29
*/

//===================================
// CTF flags
//===================================

iteminfo "item_flag_team1"
{
	name					"Red Flag"
	model					"players/male/flag1.md2"
	type					ITEM_FLAG
	index					INVENTORY_FLAG1
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_flag_team2"
{
	name					"Blue Flag"
	model					"players/male/flag2.md2"
	type					ITEM_FLAG
	index					INVENTORY_FLAG2
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

//===================================
// CTF tech
//===================================

iteminfo "item_tech1"
{
	name					"Disruptor Shield"
	model					"models/ctf/resistance/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TECH1
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_tech2"
{
	name					"Power Amplifier"
	model					"models/ctf/strength/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TECH2
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_tech3"
{
	name					"Time Accel"
	model					"models/ctf/haste/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TECH3
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

iteminfo "item_tech4"
{
	name					"AutoDoc"
	model					"models/ctf/regeneration/tris.md2"
	type					ITEM_POWERUP
	index					INVENTORY_TECH4
	mins					{-15,-15,-15}
	maxs					{15,15,15}
} //end iteminfo

