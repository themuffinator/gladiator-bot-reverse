//===========================================================================
//
// Name:				fw_weap.c
// Function:
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-12-16 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


weight "Blaster"
{
	return blaster;
} //end weight

weight "Shotgun"
{
	switch(INVENTORY_SHOTGUN)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_SHELLS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1: return shotgun; //not using quad
						default:
						{
							return quad_shotgun; //using quad
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Super Shotgun"
{
	switch(INVENTORY_SUPERSHOTGUN)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_SHELLS)
			{
				case 2: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1: return sshotgun; //not using quad
						default:
						{
							return quad_sshotgun; //using quad
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Machinegun"
{
	switch(INVENTORY_MACHINEGUN)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_BULLETS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1: return machinegun;
						default:
						{
							return quad_machinegun;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Chaingun"
{
	switch(INVENTORY_CHAINGUN)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_BULLETS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1: return chaingun;
						default:
						{
							return quad_chaingun;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Grenades"
{
	switch(INVENTORY_GRENADES)
	{
		case 1: return 0;
		default:
		{
			switch(ENEMY_HORIZONTAL_DIST)
			{
				case 65: //enemy is self damage close
				{
					return self_dmg_grenades;
				} //end case
				case 120: //enemy is too far????
				{
					return 0;//enemy is too far away
				} //end case
				default:
				{
					return grenades;
				}
			}
		} //end default
	} //end switch
} //end weight

weight "Grenade Launcher"
{
	switch(INVENTORY_GRENADELAUNCHER)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_GRENADES)
			{
				case 1: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1:
						{
							switch(ENEMY_HORIZONTAL_DIST)
							{
								case 65: //enemy is self damage close
								{
									return self_dmg_grenadelauncher;
								} //end case
								case 300: //enemy is too far????
								{
									return 0;//enemy is too far away
								} //end case
								default:
								{
									return grenadelauncher;
								} //end default
							} //end switch
						} //end case
						default:
						{
							return quad_grenadelauncher;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Rocket Launcher"
{
	switch(INVENTORY_ROCKETLAUNCHER)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_ROCKETS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_INVULNERABILITY)
					{
						case 1: //not using invulnerability
						{
							switch(USING_QUAD)
							{
								case 1: //not using quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 65: //enemy is self damage close
										{
											return self_dmg_rocketlauncher;
										} //end case
										default: //enemy is far enough so bot won't hurt itself
										{
											return rocketlauncher;
										} //end default
									} //end switch
								} //end case
								default: //using the quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 120: //enemy is self damage close
										{
											return self_dmg_rocketlauncher;
										} //end case
										default:
										{
											return quad_rocketlauncher;
										} //end default
									} //end switch
								} //end default
							} //end switch
						} //end case
						default: //using the invulnerability
						{
							return invul_rocketlauncher;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "HyperBlaster"
{
	switch(INVENTORY_HYPERBLASTER)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_CELLS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_POWERSHIELD)
					{
						case 1: //not using powershield
						{
							switch(USING_QUAD)
							{
								case 1:return hyperblaster; //not using quad and no powershield
								default:
								{
									return quad_hyperblaster; //using quad and no powershield
								} //end default
							} //end switch
						} //end case
						default: //using the powershield
						{
							return pwrshield_hyperblaster;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Railgun"
{
	switch(INVENTORY_RAILGUN)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_SLUGS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_QUAD)
					{
						case 1: return railgun; //not using quad
						default:
						{
							return quad_railgun; //using quad
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

#define MZ(value) (value) < 0 ? 0 : (value)
//MZ is for make-zero; no negative values are allowed  :0

#define BFG10K_WEIGHT(scale, add) \
	switch(NUM_VISIBLE_TEAMMATES)\
	{\
		case 1:\
		{\
			switch(USING_POWERSHIELD)\
			{\
				case 1: return $evalfloat(MZ(BFG10K * scale + add));\
				default: return $evalfloat(MZ(pwrshield_BFG10K * scale + add));\ //has the powerscreen so do not use up the cells
			}\ //end switch
		}\ //end case
		case 2:\
		{\
			switch(NUM_VISIBLE_ENEMIES)\
			{\
				case 2:\
				{\
					switch(USING_POWERSHIELD)\
					{\
						case 1: return $evalfloat(MZ(BFG10K * scale + add));\
						default: return $evalfloat(MZ(pwrshield_BFG10K * scale + add));\ //has the powerscreen so do not use up the cells
					}\ //end switch
				}\ //end case
				default:\
				{\
					switch(USING_POWERSHIELD)\
					{\
						case 1: return $evalfloat(MZ(BFG10K * scale + add));\
						default: return $evalfloat(MZ(pwrshield_BFG10K * scale + add));\ //has the powerscreen so do not use up the cells
					}\ //end switch
				}\ //end case
			}\ //end switch
		}\ //end case
		default:\
		{\
			switch(USING_POWERSHIELD)\
			{\
				case 1: return $evalfloat(MZ(BFG10K * scale + add));\
				default: return $evalfloat(MZ(pwrshield_BFG10K * scale + add));\ //has the powerscreen so do not use up the cells
			}\ //end switch
		}\ //end default
	} //end switch

weight "BFG10K"
{
	switch(INVENTORY_BFG10K)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_CELLS)
			{
				case 50: return 0;
				default:
				{
					switch(USING_INVULNERABILITY)
					{
						case 1: //not using invulnerability
						{
							switch(USING_QUAD)
							{
								case 1: //not using quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 65: //enemy is so close the bot will hurt self: value will be
										{
											BFG10K_WEIGHT(1, add_too_close)
										} //end case
										default: //enemy is far enough so bot won't hurt itself, value will be
										{
											BFG10K_WEIGHT(1, 0)
										} //end default
									} //end switch
								} //end case
								default: //using the quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 120: //enemy is self damage close
										{
											BFG10K_WEIGHT(1, add_too_close)
										} //end case
										default:
										{
											BFG10K_WEIGHT(1, add_quad)
										} //end default
									} //end switch
								} //end default
							} //end switch
						} //end case
						default: //using the invulnerability
						{
							BFG10K_WEIGHT(1, add_invuln)
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight


#ifdef XATRIX

//shoots bouncing red cells
weight "Ionripper"
{
	switch(INVENTORY_IONRIPPER)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_CELLS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_POWERSHIELD)
					{
						case 1: //not using powershield
						{
							switch(USING_QUAD)
							{
								case 1:return hyperblaster; //not using quad and no powershield
								default:
								{
									return quad_hyperblaster; //using quad and no powershield
								} //end default
							} //end switch
						} //end case
						default: //using the powershield
						{
							return pwrshield_hyperblaster;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

//sort of double rocket launcher
weight "Phalanx"
{
	switch(INVENTORY_PHALANX)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_MAGSLUGS)
			{
				case 1: return 0;
				default:
				{
					switch(USING_INVULNERABILITY)
					{
						case 1: //not using invulnerability
						{
							switch(USING_QUAD)
							{
								case 1: //not using quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 65: //enemy is self damage close
										{
											return self_dmg_rocketlauncher;
										} //end case
										default: //enemy is far enough so bot won't hurt itself
										{
											return rocketlauncher;
										} //end default
									} //end switch
								} //end case
								default: //using the quad
								{
									switch(ENEMY_HORIZONTAL_DIST)
									{
										case 120: //enemy is self damage close
										{
											return self_dmg_rocketlauncher;
										} //end case
										default:
										{
											return quad_rocketlauncher;
										} //end default
									} //end switch
								} //end default
							} //end switch
						} //end case
						default: //using the invulnerability
						{
							return invul_rocketlauncher;
						} //end default
					} //end switch
				} //end default
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Trap"
{
	switch(INVENTORY_TRAP)
	{
		case 1: return 0;
		default: return 40;
	} //end switch
} //end weight

#endif //XATRIX

#ifdef ROGUE

weight "ETF Rifle"
{
	switch(INVENTORY_ETFRIFLE)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_FLECHETTES)
			{
				case 1: return 0;
				default: return 40;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Prox Launcher"
{
	switch(INVENTORY_PROXLAUNCHER)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_PROX)
			{
				case 1: return 0;
				default: return 40;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Tesla"
{
	switch(INVENTORY_TESLA)
	{
		case 1: return 0;
		default: return 40;
	} //end switch
} //end weight

weight "Plasma Beam"
{
	switch(INVENTORY_PLASMABEAM)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_CELLS)
			{
				case 1: return 0;
				default: return 40;
			} //end switch
		} //end default
	} //end switch
} //end weight

weight "Chainfist"
{
	switch(INVENTORY_CHAINFIST)
	{
		case 1: return 0;
		default: //does not use any ammo
		{
			return 40;
		} //end default
	} //end switch
} //end weight

weight "Disruptor"
{
	switch(INVENTORY_DISRUPTOR)
	{
		case 1: return 0;
		default:
		{
			switch(INVENTORY_ROUNDS)
			{
				case 1: return 0;
				default: return 40;
			} //end switch
		} //end default
	} //end switch
} //end weight

#endif //ROGUE
