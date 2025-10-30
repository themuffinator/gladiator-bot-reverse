//===========================================================================
//
// Name:				fw_items.c
// Function:
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-12-16
// Tab Size:		3 (real tabs)
//
//
// xatrix & rogue weights:
// W_? weights
// GWW_? weights
// ammo weights
//===========================================================================


#define MZ(value)				(value) < 0 ? 0 : (value)
#define HEALTH_SCALE(v)		balance($evalfloat(FS_HEALTH*v), $evalfloat(MZ(FS_HEALTH*v-BR_HEALTH)), $evalfloat(MZ(FS_HEALTH*v+BR_HEALTH)))
#define ARMOR_SCALE(v)		balance($evalfloat(FS_ARMOR*v), $evalfloat(MZ(FS_ARMOR*v-BR_ARMOR)), $evalfloat(MZ(FS_ARMOR*v+BR_ARMOR)))
#define WEAPON_SCALE(v)		balance($evalfloat(MZ(v)), $evalfloat(MZ(v-BR_WEAPON)), $evalfloat(MZ(v+BR_WEAPON)))
#define POWERUP_SCALE(v)	balance($evalfloat(MZ(v)), $evalfloat(MZ(v-BR_POWERUP)), $evalfloat(MZ(v+BR_POWERUP)))

//=============================================
// WEAPONS
//=============================================

weight "weapon_shotgun"
{
	switch(INVENTORY_SHOTGUN)
	{
		case 1:
		{
			switch(INVENTORY_SHELLS)
			{
				case 10: return WEAPON_SCALE(W_SHOTGUN - 10);
				default: return WEAPON_SCALE(W_SHOTGUN);
			} //end switch
		} //end case
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_SHELLS)
			{
				case 20: return WEAPON_SCALE(GWW_SHOTGUN);
				case 100: return WEAPON_SCALE(GWW_SHOTGUN - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_supershotgun"
{
	switch(INVENTORY_SUPERSHOTGUN)
	{
		case 1:
		{
			switch(INVENTORY_SHELLS)
			{
				case 10: return WEAPON_SCALE(W_SUPERSHOTGUN - 10);
				default: return WEAPON_SCALE(W_SUPERSHOTGUN);
			} //end switch
		} //end case
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_SHELLS)
			{
				case 20: return WEAPON_SCALE(GWW_SUPERSHOTGUN);
				case 100: return WEAPON_SCALE(GWW_SUPERSHOTGUN - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //switch
} //end weight

weight "weapon_machinegun"
{
	switch(INVENTORY_MACHINEGUN)
	{
		case 1:
		{
			switch(INVENTORY_BULLETS)
			{
				case 40: return WEAPON_SCALE(W_MACHINEGUN - 10);
				default: return WEAPON_SCALE(W_MACHINEGUN);
			} //end switch
		} //end case
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_BULLETS)
			{
				case 50: return WEAPON_SCALE(GWW_MACHINEGUN);
				case 200: return WEAPON_SCALE(GWW_MACHINEGUN - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //switch
} //end weight

weight "weapon_chaingun"
{
	switch(INVENTORY_CHAINGUN)
	{
		case 1:
		{
			switch(INVENTORY_BULLETS)
			{
				case 40: return WEAPON_SCALE(W_CHAINGUN - 10);
				default: return WEAPON_SCALE(W_CHAINGUN);
			} //end switch
		} //end case
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_BULLETS)
			{
				case 50: return WEAPON_SCALE(GWW_CHAINGUN);
				case 200: return WEAPON_SCALE(GWW_CHAINGUN - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //switch
} //end weight

weight "weapon_grenadelauncher"
{
	switch(INVENTORY_GRENADELAUNCHER)
	{
		case 1: return WEAPON_SCALE(W_GRENADELAUNCHER);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_GRENADES)
			{
				case 16: return WEAPON_SCALE(GWW_GRENADELAUNCHER);
				case 50: return WEAPON_SCALE(GWW_GRENADELAUNCHER - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_rocketlauncher"
{
	switch(INVENTORY_ROCKETLAUNCHER)
	{
		case 1: return WEAPON_SCALE(W_ROCKETLAUNCHER);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_ROCKETS)
			{
				case 16: return WEAPON_SCALE(GWW_ROCKETLAUNCHER);
				case 50: return WEAPON_SCALE(GWW_ROCKETLAUNCHER - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_hyperblaster"
{
	switch(INVENTORY_HYPERBLASTER)
	{
		case 1: return WEAPON_SCALE(W_HYPERBLASTER);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_CELLS)
			{
				case 16: return WEAPON_SCALE(GWW_HYPERBLASTER);
				case 200: return WEAPON_SCALE(GWW_HYPERBLASTER - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_railgun"
{
	switch(INVENTORY_RAILGUN)
	{
		case 1: return WEAPON_SCALE(W_RAILGUN);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_SLUGS)
			{
				case 16: return WEAPON_SCALE(GWW_RAILGUN);
				case 50: return WEAPON_SCALE(GWW_RAILGUN - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_bfg"
{
	switch(INVENTORY_BFG10K)
	{
		case 1: return WEAPON_SCALE(W_BFG10K);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_CELLS)
			{
				case 16: return WEAPON_SCALE(GWW_BFG10K);
				case 200: return WEAPON_SCALE(GWW_BFG10K - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

#ifdef XATRIX

weight "weapon_boomer"
{
	switch(INVENTORY_IONRIPPER)
	{
		case 1: return WEAPON_SCALE(W_IONRIPPER);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_CELLS)
			{
				case 40: return WEAPON_SCALE(GWW_IONRIPPER);
				case 200: return WEAPON_SCALE(GWW_IONRIPPER - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_phalanx"
{
	switch(INVENTORY_PHALANX)
	{
		case 1: return WEAPON_SCALE(W_PHALANX);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_MAGSLUGS)
			{
				case 15: return WEAPON_SCALE(GWW_PHALANX);
				case 50: return WEAPON_SCALE(GWW_PHALANX - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

#endif //XATRIX

#ifdef ROGUE

weight "weapon_etf_rifle"
{
	switch(INVENTORY_ETFRIFLE)
	{
		case 1: return WEAPON_SCALE(W_ETFRIFLE);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_FLECHETTES)
			{
				case 40: return WEAPON_SCALE(GWW_ETFRIFLE);
				case 200: return WEAPON_SCALE(GWW_ETFRIFLE - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_proxlauncher"
{
	switch(INVENTORY_PROXLAUNCHER)
	{
		case 1: return WEAPON_SCALE(W_PROXLAUNCHER);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_PROX)
			{
				case 10: return WEAPON_SCALE(GWW_PROXLAUNCHER);
				case 50: return WEAPON_SCALE(GWW_PROXLAUNCHER - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_plasmabeam"
{
	switch(INVENTORY_PLASMABEAM)
	{
		case 1: return WEAPON_SCALE(W_PLASMABEAM);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_CELLS)
			{
				case 50: return WEAPON_SCALE(GWW_PLASMABEAM);
				case 200: return WEAPON_SCALE(GWW_PLASMABEAM - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight

weight "weapon_chainfist"
{
	switch(INVENTORY_CHAINFIST)
	{
		case 1: return WEAPON_SCALE(W_CHAINFIST);
		default: //does not use any ammo
		{
			return 0;
		} //end default
	} //end switch
} //end weight

weight "weapon_disintegrator"
{
	switch(INVENTORY_DISRUPTOR)
	{
		case 1: return WEAPON_SCALE(W_DISRUPTOR);
		default:
		{
#if !(DMFLAGS & DF_WEAPONS_STAY)
			switch(INVENTORY_ROUNDS)
			{
				case 25: return WEAPON_SCALE(GWW_DISRUPTOR);
				case 100: return WEAPON_SCALE(GWW_DISRUPTOR - 10);
				default: return balance(5, 3, 7);
			} //end switch
#else
			return 0;
#endif			
		} //end default
	} //end switch
} //end weight
#endif //ROGUE


//=============================================
// AMMO
//
//
// max ammo:	normal,	bandolier,	ammo pack
//
// shells		100		150			200
// bullets		200		250			300
// grenades		50			50				100
// rockets		50			50				100
// cells			200		250			300
// slugs			50			75				100
//
// traps			5			5				5
// mag slugs	50			75				100
//
// flechettes	200		250			200
// prox			50			50				50
// tesla			50			50				50
// rounds		100		150			200
//
//=============================================

weight "ammo_grenades"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_GRENADES)
			{
				case 15: return balance(30, 28, 32);
				case 30: return balance(20, 18, 22);
				case 50: return balance(10, 8, 12);
				default: return 0;
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_GRENADES)
			{
				case 15: return balance(30, 28, 32);
				case 30: return balance(20, 18, 22);
				case 100: return balance(10, 8, 12);
				default: return 0;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "ammo_shells"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_BANDOLIER)
			{
				case 1: //if no bondolier
				{
					switch(INVENTORY_SHELLS)
					{
						case 15: return balance(25, 23, 27);
						case 30: return balance(15, 13, 17);
						case 100: return balance(5, 3, 7);
						default: return 0;
					} //end switch
				} //end case
				default: //with bondolier
				{
					switch(INVENTORY_SHELLS)
					{
						case 15: return balance(25, 23, 27);
						case 30: return balance(15, 13, 17);
						case 150: return balance(5, 3, 7);
						default: return 0;
					} //end switch
				} //end case
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_SHELLS)
			{
				case 15: return balance(25, 23, 27);
				case 30: return balance(15, 13, 17);
				case 200: return balance(5, 3, 7);
				default: return 0;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "ammo_bullets"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_BANDOLIER)
			{
				case 1: //if no bondolier
				{
					switch(INVENTORY_BULLETS)
					{
						case 15: return balance(35, 33, 37);
						case 30: return balance(25, 23, 27);
						case 200: return balance(15, 13, 17);
						default: return 0;
					} //end switch
				} //end case
				default: //with bondolier
				{
					switch(INVENTORY_BULLETS)
					{
						case 15: return balance(35, 33, 37);
						case 30: return balance(25, 23, 27);
						case 250: return balance(15, 13, 17);
						default: return 0;
					} //end switch
				} //end case
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_BULLETS)
			{
				case 15: return balance(35, 33, 37);
				case 30: return balance(25, 23, 27);
				case 300: return balance(15, 13, 17);
				default: return 0;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "ammo_cells"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_BANDOLIER)
			{
				case 1: //if no bondolier
				{
					switch(INVENTORY_BFG10K)
					{
						case 1:
						{
							switch(INVENTORY_HYPERBLASTER)
							{
								case 1:
								{
									switch(INVENTORY_CELLS)
									{
										case 50: return balance(45, 43, 47);
										case 200: return balance(15, 13, 17);
										default: return 0;
									} //end switch
								} //end default
								default:
								{
									switch(INVENTORY_CELLS)
									{
										case 200: return balance(20, 18, 22);
										default: return 0;
									} //end switch
								} //end default
							} //end switch
						} //end default
						default:
						{
							switch(INVENTORY_CELLS)
							{
								case 51: return balance(50, 48, 52);
								case 100: return balance(45, 43, 47);
								case 200: return balance(35, 33, 37);
								default: return 0;
							} //end switch
						} //end default
					} //end switch
				} //end case
				default: //with bondolier
				{
					switch(INVENTORY_BFG10K)
					{
						case 1:
						{
							switch(INVENTORY_HYPERBLASTER)
							{
								case 1:
								{
									switch(INVENTORY_CELLS)
									{
										case 50: return balance(45, 43, 47);
										case 250: return balance(15, 13, 17);
										default: return 0;
									} //end switch
								} //end default
								default:
								{
									switch(INVENTORY_CELLS)
									{
										case 250: return balance(20, 18, 22);
										default: return 0;
									} //end switch
								} //end default
							} //end switch
						} //end default
						default:
						{
							switch(INVENTORY_CELLS)
							{
								case 51: return balance(50, 48, 52);
								case 100: return balance(45, 43, 47);
								case 250: return balance(35, 33, 37);
								default: return 0;
							} //end switch
						} //end default
					} //end switch
				} //end case
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_BFG10K)
			{
				case 1:
				{
					switch(INVENTORY_HYPERBLASTER)
					{
						case 1:
						{
							switch(INVENTORY_CELLS)
							{
								case 50: return balance(45, 43, 47);
								case 300: return balance(15, 13, 17);
								default: return 0;
							} //end switch
						} //end default
						default:
						{
							switch(INVENTORY_CELLS)
							{
								case 300: return balance(20, 18, 22);
								default: return 0;
							} //end switch
						} //end default
					} //end switch
				} //end default
				default:
				{
					switch(INVENTORY_CELLS)
					{
						case 51: return balance(50, 48, 52);
						case 100: return balance(45, 43, 47);
						case 300: return balance(35, 33, 37);
						default: return 0;
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "ammo_rockets"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_ROCKETLAUNCHER)
			{
				case 1: //if a rocket launcher
				{
					switch(INVENTORY_ROCKETS)
					{
						case 15: return balance(45, 43, 47);
						case 30: return balance(40, 38, 42);
						case 50: return balance(25, 23, 27);
						default: return 0;
					} //end switch
				} //end default
				default:
				{
					switch(INVENTORY_ROCKETS)
					{
						case 15: return balance(35, 33, 37);
						case 30: return balance(30, 28, 32);
						case 50: return balance(15, 13, 17);
						default: return 0;
					} //end switch
				} //end default
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_ROCKETLAUNCHER)
			{
				case 1: //if a rocket launcher
				{
					switch(INVENTORY_ROCKETS)
					{
						case 15: return balance(45, 43, 47);
						case 30: return balance(40, 38, 42);
						case 100: return balance(25, 23, 27);
						default: return 0;
					} //end switch
				} //end default
				default:
				{
					switch(INVENTORY_ROCKETS)
					{
						case 15: return balance(35, 33, 37);
						case 30: return balance(30, 28, 32);
						case 100: return balance(15, 13, 17);
						default: return 0;
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "ammo_slugs"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //if no ammo pack
		{
			switch(INVENTORY_BANDOLIER)
			{
				case 1: //if no bondolier
				{
					switch(INVENTORY_RAILGUN)
					{
						case 1:
						{
							switch(INVENTORY_SLUGS)
							{
								case 15: return balance(35, 33, 37);
								case 30: return balance(15, 13, 17);
								case 50: return balance(5, 3, 7);
								default: return 0;
							} //end switch
						} //end case
						default:
						{
							switch(INVENTORY_SLUGS)
							{
								case 15: return balance(25, 23, 27);
								case 30: return balance(10, 8, 12);
								case 50: return balance(5, 3, 7);
								default: return 0;
							} //end switch
						} //end case
					} //end switch
				} //end case
				default: //with bondolier
				{
					switch(INVENTORY_RAILGUN)
					{
						case 1:
						{
							switch(INVENTORY_SLUGS)
							{
								case 15: return balance(35, 33, 37);
								case 30: return balance(15, 13, 17);
								case 75: return balance(5, 3, 7);
								default: return 0;
							} //end switch
						} //end case
						default:
						{
							switch(INVENTORY_SLUGS)
							{
								case 15: return balance(25, 23, 27);
								case 30: return balance(10, 8, 12);
								case 75: return balance(5, 3, 7);
								default: return 0;
							} //end switch
						} //end case
					} //end switch
				} //end case
			} //end switch
		} //end case
		default: //with ammo pack
		{
			switch(INVENTORY_RAILGUN)
			{
				case 1:
				{
					switch(INVENTORY_SLUGS)
					{
						case 15: return balance(35, 33, 37);
						case 30: return balance(15, 13, 17);
						case 100: return balance(5, 3, 7);
						default: return 0;
					} //end switch
				} //end case
				default:
				{
					switch(INVENTORY_SLUGS)
					{
						case 15: return balance(25, 23, 27);
						case 30: return balance(10, 8, 12);
						case 100: return balance(5, 3, 7);
						default: return 0;
					} //end switch
				} //end case
			} //end switch
		} //end default
	} //end switch
} //end weight

#ifdef XATRIX

weight "ammo_magslug"
{
	return balance(20, 18, 22);
} //end weight

weight "ammo_trap"
{
	return balance(20, 18, 22);
} //end weight

#endif //XATRIX

#ifdef ROGUE

weight "ammo_flechettes"
{
	return balance(20, 18, 22);
} //end weight

weight "ammo_prox"
{
	return balance(20, 18, 22);
} //end weight

weight "ammo_tesla"
{
	return balance(20, 18, 22);
} //end weight

weight "ammo_nuke"
{
	return balance(20, 18, 22);
} //end weight

weight "ammo_disruptor"
{
	return balance(20, 18, 22);
} //end weight

#endif //ROGUE

//=============================================
// HEALTH
//=============================================

//increases health with 100 up to 100
weight "item_adrenaline"
{
	switch(INVENTORY_HEALTH)
	{
		case 15: return HEALTH_SCALE(100);
		case 30: return HEALTH_SCALE(95);
		case 50: return HEALTH_SCALE(85);
		case 70: return HEALTH_SCALE(55);
		default: return HEALTH_SCALE(35);
	} //end switch
} //end weight

//increases health with 10
weight "item_health"
{
	switch(INVENTORY_HEALTH)
	{
		case 15: return HEALTH_SCALE(95);
		case 30: return HEALTH_SCALE(85);
		case 50: return HEALTH_SCALE(75);
		case 70: return HEALTH_SCALE(45);
		case 100: return HEALTH_SCALE(25);
		default: return 0;
	} //end switch
} //end weight

//increases health with 2
weight "item_health_small"
{
	switch(INVENTORY_HEALTH)
	{
		case 15: return HEALTH_SCALE(92);
		case 30: return HEALTH_SCALE(82);
		case 50: return HEALTH_SCALE(72);
		case 70: return HEALTH_SCALE(42);
		case 100: return HEALTH_SCALE(22);
		default: return 0;
	} //end switch
} //end weight

//increases health with 25
weight "item_health_large"
{
	switch(INVENTORY_HEALTH)
	{
		case 15: return HEALTH_SCALE(100);
		case 30: return HEALTH_SCALE(90);
		case 50: return HEALTH_SCALE(80);
		case 70: return HEALTH_SCALE(50);
		case 100: return HEALTH_SCALE(30);
		default: return 0;
	} //end switch
} //end weight

//increases health with 100 up to 999
weight "item_health_mega"
{
	switch(INVENTORY_HEALTH)
	{
		case 15: return HEALTH_SCALE(100);
		case 30: return HEALTH_SCALE(90);
		case 50: return HEALTH_SCALE(80);
		case 70: return HEALTH_SCALE(50);
		case 100: return HEALTH_SCALE(40);
		default: return HEALTH_SCALE(W_MEGAHEALTH);
	} //end switch
} //end weight

//=============================================
// ARMOR
//=============================================

weight "item_armor_body"		//red
{
	switch(INVENTORY_ARMORBODY)	//red
	{
		case 1:	//no red armor
		{
			switch(INVENTORY_ARMORCOMBAT)	//yellow
			{
				case 1:	//no yellow armor
				{
					switch(INVENTORY_ARMORJACKET)	//green
					{
						case 1: return ARMOR_SCALE(70);		//no green armor
						case 50: return ARMOR_SCALE(65);
						default: return ARMOR_SCALE(60);
					} //end switch
				} //end case
				default: return ARMOR_SCALE(60);
			} //end switch
		} //end case
		case 100: return ARMOR_SCALE(60);
		case 200: return ARMOR_SCALE(50);
		default: return 0;
	} //end switch
} //end weight

weight "item_armor_combat"	//yellow
{
	switch(INVENTORY_ARMORBODY)	//red
	{
		case 1:	//no red armor
		{
			switch(INVENTORY_ARMORCOMBAT)	//yellow
			{
				case 1:	//no yellow armor
				{
					switch(INVENTORY_ARMORJACKET)	//green
					{
						case 1: return ARMOR_SCALE(65);		//no green armor
						case 50: return ARMOR_SCALE(60);
						default: return ARMOR_SCALE(55);
					} //end switch
				} //end case
				case 100: return ARMOR_SCALE(65);
				default: return 0;
			} //end switch
		} //end case
		case 100: return ARMOR_SCALE(50);
		case 200: return ARMOR_SCALE(40);
		default: return 0;
	} //end switch
} //end weight

weight "item_armor_jacket"	//green
{
	switch(INVENTORY_ARMORBODY)	//red
	{
		case 1:	//no red armor
		{
			switch(INVENTORY_ARMORCOMBAT)	//yellow
			{
				case 1:	//no yellow armor
				{
					switch(INVENTORY_ARMORJACKET)	//green
					{
						case 1: return ARMOR_SCALE(50);		//no green armor
						case 50: return ARMOR_SCALE(40);
						default: return 0;
					} //end switch
				} //end case
				case 100: return ARMOR_SCALE(40);
				default: return 0;
			} //end switch
		} //end case
		case 100: return ARMOR_SCALE(40);
		case 200: return ARMOR_SCALE(30);
		default: return 0;
	} //end switch
} //end weight

weight "item_armor_shard"
{
	switch(INVENTORY_ARMORBODY)	//red
	{
		case 1:	//no red armor
		{
			switch(INVENTORY_ARMORCOMBAT)	//yellow
			{
				case 1:	//no yellow armor
				{
					switch(INVENTORY_ARMORJACKET)	//green
					{
						case 1: return ARMOR_SCALE(5);		//no green armor
						case 50: return ARMOR_SCALE(4);
						default: return ARMOR_SCALE(2);
					} //end switch
				} //end case
				case 100: return ARMOR_SCALE(5);
				default: return ARMOR_SCALE(2);
			} //end switch
		} //end case
		case 100: return ARMOR_SCALE(4);
		case 200: return ARMOR_SCALE(3);
		default: return ARMOR_SCALE(2);
	} //end switch
} //end weight

weight "item_power_screen" //"Power Screen"
{
	switch(INVENTORY_POWERSCREEN)
	{
		case 1: //no powerscreen
		{
			switch(INVENTORY_CELLS)
			{
				case 1: //no cells
				{
					return ARMOR_SCALE(W_POWERSCREEN);
				} //end case
				default: return ARMOR_SCALE(W_POWERSCREEN+2);
			} //end switch
		} //end case
		default: return 1;
	} //end switch
} //end weight

weight "item_power_shield" //"Power Shield"
{
	switch(INVENTORY_POWERSHIELD)
	{
		case 1: //no powershield
		{
			switch(INVENTORY_CELLS)
			{
				case 1: //no cells
				{
					return ARMOR_SCALE(W_POWERSHIELD);
				} //end case
				default: return ARMOR_SCALE(W_POWERSHIELD+2);
			} //end switch
		} //end case
		default: return 1;
	} //end switch
} //end weight

//=============================================
// POWERUPS
//=============================================

weight "item_pack" //"Ammo Pack"
{
	switch(INVENTORY_AMMOPACK)
	{
		case 1: //no ammo pack
		{
			return POWERUP_SCALE(W_AMMOPACK);
		} //end case
		default: return balance(5, 3, 7);
	} //end switch
} //end weight

weight "item_bandolier" //"Bandolier"
{
	switch(INVENTORY_BANDOLIER)
	{
		case 1: //no bandolier
		{
			return POWERUP_SCALE(W_BANDOLIER);
		} //end case
		default: return balance(4, 2, 6);
	} //end switch
} //end weight

weight "item_quad" //"Quad Damage"
{
	return POWERUP_SCALE(W_QUAD);
} //end weight

weight "item_invulnerability" //"Invulnerability"
{
	return POWERUP_SCALE(W_INVULNERABILITY);
} //end weight

weight "item_silencer" //"Silencer"
{
	return POWERUP_SCALE(W_SILENCER);
} //end weight

weight "item_breather" //"Rebreather"
{
	return POWERUP_SCALE(W_REBREATHER);
} //end weight

weight "item_enviro" //"Environment Suit"
{
	return POWERUP_SCALE(W_ENVIRO);
} //end weight

#ifdef XATRIX

weight "item_quadfire"
{
	return POWERUP_SCALE(W_QUADFIRE);
} //end weight

#endif //XATRIX

#ifdef ROGUE

weight "item_ir_goggles"
{
	return POWERUP_SCALE(W_IR_GOGGLES);
} //end weight

weight "item_double"
{
	return POWERUP_SCALE(W_DOUBLE);
} //end weight

weight "item_compass"
{
	return POWERUP_SCALE(W_COMPASS);
} //end weight

weight "item_sphere_vengeance"
{
	return POWERUP_SCALE(W_SPHERE_VENGEANCE);
} //end weight

weight "item_sphere_hunter"
{
	return POWERUP_SCALE(W_SPHERE_HUNTER);
} //end weight

weight "item_sphere_defender"
{
	return POWERUP_SCALE(W_SPHERE_DEFENDER);
} //end weight

weight "item_doppleganger"
{
	return POWERUP_SCALE(W_DOPPLEGANGER);
} //end weight

weight "dm_tag_token"
{
	return POWERUP_SCALE(W_TAG_TOKEN);
} //end weight

#endif //ROGUE


//=============================================
// CTF techs
//=============================================

weight "item_tech1" // Resistance Tech \ Disruptor Shield
{
	switch(INVENTORY_TECH1)
	{
		case 1: //no tech1
		{
			switch(INVENTORY_TECH2)
			{
				case 1: //no tech2
				{
					switch(INVENTORY_TECH3)
					{
						case 1: //no tech3
						{
							switch(INVENTORY_TECH4)
							{
								case 1: //no tech4
								{
									return TECH1_WEIGHT;
								} //end case
								default: return $evalfloat(TECH1_WEIGHT > TECH4_WEIGHT ? TECH1_WEIGHT : 0);
							} //end switch
						} //end case
						default: return $evalfloat(TECH1_WEIGHT > TECH3_WEIGHT ? TECH1_WEIGHT : 0);
					} //end switch
				} //end case
				default: return $evalfloat(TECH1_WEIGHT > TECH2_WEIGHT ? TECH1_WEIGHT : 0);
			} //end switch
		} //end case
		default: return 0; //already has tech1
	} //end switch
} //end weight

weight "item_tech2" // Strength Tech \ Power Amplifier
{
	switch(INVENTORY_TECH1)
	{
		case 1: //no tech1
		{
			switch(INVENTORY_TECH2)
			{
				case 1: //no tech2
				{
					switch(INVENTORY_TECH3)
					{
						case 1: //no tech3
						{
							switch(INVENTORY_TECH4)
							{
								case 1: //no tech4
								{
									return TECH2_WEIGHT;
								} //end case
								default: return $evalfloat(TECH2_WEIGHT > TECH4_WEIGHT ? TECH2_WEIGHT : 0);
							} //end switch
						} //end case
						default: return $evalfloat(TECH2_WEIGHT > TECH3_WEIGHT ? TECH2_WEIGHT : 0);
					} //end switch
				} //end case
				default: return 0; //already has tech2
			} //end switch
		} //end case
		default: return $evalfloat(TECH2_WEIGHT > TECH1_WEIGHT ? TECH2_WEIGHT : 0);
	} //end switch
} //end weight

weight "item_tech3" // Haste Tech \ Time Accel
{
	switch(INVENTORY_TECH1)
	{
		case 1: //no tech1
		{
			switch(INVENTORY_TECH2)
			{
				case 1: //no tech2
				{
					switch(INVENTORY_TECH3)
					{
						case 1: //no tech3
						{
							switch(INVENTORY_TECH4)
							{
								case 1: //no tech4
								{
									return TECH3_WEIGHT;
								} //end case
								default: return $evalfloat(TECH3_WEIGHT > TECH4_WEIGHT ? TECH3_WEIGHT : 0);
							} //end switch
						} //end case
						default: return 0; //already has tech3
					} //end switch
				} //end case
				default: return $evalfloat(TECH3_WEIGHT > TECH2_WEIGHT ? TECH3_WEIGHT : 0);
			} //end switch
		} //end case
		default: return $evalfloat(TECH3_WEIGHT > TECH1_WEIGHT ? TECH3_WEIGHT : 0);
	} //end switch
} //end weight

weight "item_tech4" // Regeneration Tech \ AutoDoc
{
	switch(INVENTORY_TECH1)
	{
		case 1: //no tech1
		{
			switch(INVENTORY_TECH2)
			{
				case 1: //no tech2
				{
					switch(INVENTORY_TECH3)
					{
						case 1: //no tech3
						{
							switch(INVENTORY_TECH4)
							{
								case 1: //no tech4
								{
									return TECH4_WEIGHT;
								} //end case
								default: return 0; //already has tech4
							} //end switch
						} //end case
						default: return $evalfloat(TECH4_WEIGHT > TECH3_WEIGHT ? TECH4_WEIGHT : 0);
					} //end switch
				} //end case
				default: return $evalfloat(TECH4_WEIGHT > TECH2_WEIGHT ? TECH4_WEIGHT : 0);
			} //end switch
		} //end case
		default: return $evalfloat(TECH4_WEIGHT > TECH1_WEIGHT ? TECH4_WEIGHT : 0);
	} //end switch
} //end weight

//=============================================
// for dropped CTF flags
//=============================================

weight "item_flag_team1" //Red Flag
{
	switch(INVENTORY_FLAG1)
	{
		case 1: //not carrying the red flag
		{
			switch(INVENTORY_FLAG2)
			{
				case 1: //not carrying the blue flag
				{
					return FLAG_WEIGHT;
				} //end case
				default: //bot carrying the blue flag so go back to the base
				{
					return 200;
				} //end case
			} //end switch
		} //end case
		default: //bot carrying the red flag, so don't go back
		{
			return 0;
		} //end default
	} //end switch
} //end weight

weight "item_flag_team2" //Blue Flag
{
	switch(INVENTORY_FLAG2)
	{
		case 1: //not carrying the blue flag
		{
			switch(INVENTORY_FLAG1)
			{
				case 1: //not carrying  the red flag
				{
					return FLAG_WEIGHT;
				} //end case
				default: //bot is carrying the red flag so go back to the base
				{
					return 200;
				} //end case
			} //end switch
		} //end case
		default: //bot is carrying the blue flag, so don't go back
		{
			return 0;
		} //end default
	} //end switch
} //end weight

