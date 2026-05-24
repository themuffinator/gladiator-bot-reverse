//===========================================================================
//
// Name:				sounds.c
// Function:		sound configuration
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-02-03
// Tab Size:		3 (real tabs)
//
// Xatrix Weapon & Powerup sounds
// Rogue Weapon & Powerup sounds
//===========================================================================


#define SOUNDTYPE_IGNORE				0		//ignore this sound
#define SOUNDTYPE_PLAYER				1		//some sound caused by a player
#define SOUNDTYPE_PLAYERSTEPS			2		//walking
#define SOUNDTYPE_PLAYERJUMP			3		//jumping
#define SOUNDTYPE_PLAYERWATERIN		4		//entering water
#define SOUNDTYPE_PLAYERWATEROUT		5		//getting out of the water
#define SOUNDTYPE_PLAYERFALL			6		//falling
#define SOUNDTYPE_FIRINGWEAPON		7		//firing weapon
#define SOUNDTYPE_USINGPOWERUP		8		//using a powerup
#define SOUNDTYPE_PICKUPWEAPON		9		//picking up weapon
#define SOUNDTYPE_PICKUPAMMO			10		//picking up ammo
#define SOUNDTYPE_PICKUPARMOR			11		//pickup armor
#define SOUNDTYPE_PICKUPARMORSHARD	12		//pickup armor shard
#define SOUNDTYPE_PICKUPPOWERUP		13		//pickup powerup
#define SOUNDTYPE_PICKUPHEALTH_S 	14		//pickup small health
#define SOUNDTYPE_PICKUPHEALTH_N 	15		//pickup normal health
#define SOUNDTYPE_PICKUPHEALTH_L 	16		//pickup large health
#define SOUNDTYPE_PICKUPHEALTH_M 	17		//pickup mega health
#define SOUNDTYPE_DOOR					18		//door
#define SOUNDTYPE_ELEVATOR				19		//elevator
#define SOUNDTYPE_TELEPORT				20		//teleportation

/*
soundinfo
{
	name
	volume
	duration
	type
	recognition
	string
}
*/

//==========================
// Player sounds
//==========================

soundinfo	//player foot steps
{
	name				"player/step1.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player foot steps
{
	name				"player/step2.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player foot steps
{
	name				"player/step3.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player foot steps
{
	name				"player/step4.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player fall
{
	name				"player/fall1.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player fall
{
	name				"player/fall2.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player water in
{
	name				"player/watr_in.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player water out
{
	name				"player/watr_out.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player under water
{
	name				"player/watr_un.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}

soundinfo	//player fry in lava or slime
{
	name				"player/fry.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_PLAYER
	recognition		0
	string			""
}


//==========================
// Weapon sounds
//==========================

soundinfo	//BLASTer Fire, muzzle flash sound
{
	name				"weapons/blastf1a.wav"
	volume			80.0
	duration			0.47		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Blaster"
}

soundinfo	//SHOTGun Fire 1b, muzzle flash sound
{
	name				"weapons/shotgf1b.wav"
	volume			80.0
	duration			0.58		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Shotgun"
}

soundinfo	//SHOTGun Reload 1b, muzzle flash sound
{
	name				"weapons/shotgr1b.wav"
	volume			80.0
	duration			0.66		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Shotgun"
}

soundinfo	//Super SHOTgun Fire 1b, muzzle flash sound (contains reload sound)
{
	name				"weapons/sshotf1b.wav"
	volume			80.0
	duration			0.96		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Super Shotgun"
}

soundinfo	//Super SHOTgun Reload 1b, (not used by Quake2)
{
	name				"weapons/sshotr1b.wav"
	volume			80.0
	duration			0.39		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Super Shotgun"
}

soundinfo	//MACHineGun Fire 1b, muzzle flash sound
{
	name				"weapons/machgf1b.wav"
	volume			80.0
	duration			0.31		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Machinegun"
}

soundinfo	//MACHineGun Fire 2b, muzzle flash sound
{
	name				"weapons/machgf2b.wav"
	volume			80.0
	duration			0.31		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Machinegun"
}

soundinfo	//MACHineGun Fire 3b, muzzle flash sound
{
	name				"weapons/machgf3b.wav"
	volume			80.0
	duration			0.31		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Machinegun"
}

soundinfo	//MACHineGun Fire 4b, muzzle flash sound
{
	name				"weapons/machgf4b.wav"
	volume			80.0
	duration			0.31		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Machinegun"
}

soundinfo	//MACHineGun Fire 5b, muzzle flash sound
{
	name				"weapons/machgf5b.wav"
	volume			80.0
	duration			0.31		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Machinegun"
}

soundinfo	//GRENade Launcher Fire 1a, muzzle flash sound
{
	name				"weapons/grenlf1a.wav"
	volume			80.0
	duration			1.09		//correct, but no sound for last .5 seconds
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Grenade Launcher"
}

soundinfo	//GRENade Launcher Reload 1a, muzzle flash sound
{
	name				"weapons/grenlr1a.wav"
	volume			80.0
	duration			0.74		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Grenade Launcher"
}

soundinfo	//ROCKet Launcher Fire 1a, muzzle flash sound
{
	name				"weapons/rocklf1a.wav"
	volume			80.0
	duration			1.2		//correct, but no sound for last .5 seconds
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Rocket Launcher"
}

soundinfo	//ROCKet Launcher Reload 1a, muzzle flash sound
{
	name				"weapons/rocklr1a.wav"
	volume			80.0
	duration			0.73		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Rocket Launcher"
}

soundinfo	//HYPeRBlaster Fire 1a, muzzle flash sound
{
	name				"weapons/hyprbf1a.wav"
	volume			80.0
	duration			0.09		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"HyperBlaster"
}

soundinfo	//RAILGun Fire 1a, muzzle flash sound
{
	name				"weapons/railgf1a.wav"
	volume			80.0
	duration			1.5		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Railgun"
}

soundinfo	//RAILGun Reload 1a (this sound is not used by Quake2)
{
	name				"weapons/railgr1a.wav"
	volume			80.0
	duration			1.16		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"Railgun"
}

soundinfo	//BFG Fire 1y, muzzle flash sound
{
	name				"weapons/bfg__f1y.wav"
	volume			80.0
	duration			2.39		//correct
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			"BFG10K"
}

soundinfo	//Respawn, muzzle flash sound
{
	name				"misc/tele1.wav"
	volume			50.0
	duration			0.59
	type				SOUNDTYPE_FIRINGWEAPON
	recognition		0
	string			""
}

//==========================
// Using item sounds
//==========================

soundinfo	//Quad Damage activate
{
	name				"items/damage.wav"
	volume			80.0
	duration			2.11
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Quad Damage wearing off
{
	name				"items/damage2.wav"
	volume			80.0
	duration			2.98
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Quad Damage attack
{
	name				"items/damage3.wav"
	volume			80.0
	duration			1.14
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Invulnerability activate
{
	name				"items/protect.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Invulnerability wearing off
{
	name				"items/protect2.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Invulnerability protecting
{
	name				"items/protect3.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Invulnerability protecting?
{
	name				"items/protect4.wav"
	volume			80.0
	duration			0.3
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Power Armor activate
{
	name				"misc/power1.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

soundinfo	//Power Armor wearing off
{
	name				"misc/power2.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_USINGPOWERUP
	recognition		0
	string			""
}

//==========================
// Pickup sounds
//==========================

soundinfo	//pickup Armor
{
	name				"misc/ar1_pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPARMOR
	recognition		0
	string			"Body Armor"
}

soundinfo	//pickup Armor Shard
{
	name				"misc/ar2_pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPARMORSHARD
	recognition		0
	string			"Armor Shard"
}

soundinfo	//pickup Power Shield
{
	name				"misc/ar3_pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPPOWERUP
	recognition		0
	string			"Power Shield"
}

soundinfo	//pickup Weapon
{
	name				"misc/w_pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPWEAPON
	recognition		0
	string			"Weapon"
}

soundinfo	//pickup Ammo
{
	name				"misc/am_pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPAMMO
	recognition		0
	string			"Ammo"
}

soundinfo	//pickup Powerup
{
	name				"items/pkup.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPPOWERUP
	recognition		0
	string			"Powerup"
}

soundinfo	//pickup Small Health
{
	name				"items/s_health.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPHEALTH_S
	recognition		0
	string			"Health"
}

soundinfo	//pickup Normal Health
{
	name				"items/n_health.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPHEALTH_N
	recognition		0
	string			"Health"
}

soundinfo	//pickup Large Health
{
	name				"items/l_health.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPHEALTH_L
	recognition		0
	string			"Health"
}

soundinfo	//pickup Mega Health
{
	name				"items/m_health.wav"
	volume			80.0
	duration			0.2
	type				SOUNDTYPE_PICKUPHEALTH_M
	recognition		0
	string			"Health"
}

