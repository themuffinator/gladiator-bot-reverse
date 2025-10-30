//===========================================================================
//
// Name:				fw_aggr.c
// Function:
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-10-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================


#define HAS_SUPERSHOTGUN	\
switch(INVENTORY_SUPERSHOTGUN)\
{\
	case 1:\ //has no super shotgun
	{\
		return balance(0, 0, 20);\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_SHELLS)\
		{\
			case 15:\ //too few shells
			{\
				return balance(0, 0, 20);\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_MACHINEGUN	\
switch(INVENTORY_MACHINEGUN)\
{\
	case 1:\ //has no machinegun
	{\
		HAS_SUPERSHOTGUN\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_BULLETS)\
		{\
			case 50:\ //too few bullets
			{\
				HAS_SUPERSHOTGUN\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_CHAINGUN	\
switch(INVENTORY_CHAINGUN)\
{\
	case 1:\ //has no chaingun
	{\
		HAS_MACHINEGUN\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_BULLETS)\
		{\
			case 100:\ //too few bullets
			{\
				HAS_MACHINEGUN\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_GRENADELAUNCHER	\
switch(INVENTORY_GRENADELAUNCHER)\
{\
	case 1:\ //has no grenade launcher
	{\
		HAS_CHAINGUN\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_GRENADES)\
		{\
			case 5:\ //too few grenades
			{\
				HAS_CHAINGUN\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_ROCKETLAUNCHER	\
switch(INVENTORY_ROCKETLAUNCHER)\
{\
	case 1:\ //has no rocket launcher
	{\
		HAS_GRENADELAUNCHER\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_ROCKETS)\
		{\
			case 5:\ //too few rockets
			{\
				HAS_GRENADELAUNCHER\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_HYPERBLASTER	\
switch(INVENTORY_HYPERBLASTER)\
{\
	case 1:\ //has no hyperblaster
	{\
		HAS_ROCKETLAUNCHER\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_CELLS)\
		{\
			case 50:\ //too few cells
			{\
				HAS_ROCKETLAUNCHER\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_RAILGUN	\
switch(INVENTORY_RAILGUN)\
{\
	case 1:\ //has no railgun
	{\
		HAS_HYPERBLASTER\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_SLUGS)\
		{\
			case 5:\ //too few slugs
			{\
				HAS_HYPERBLASTER\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
}\ //end switch

#define HAS_BFG10K	\
switch(INVENTORY_BFG10K)\
{\
	case 1:\ //has no bfg10k
	{\
		HAS_RAILGUN\
	}\ //end case
	default:\
	{\
		switch(INVENTORY_CELLS)\
		{\
			case 50:\ //too few to fire the bfg
			{\
				HAS_RAILGUN\
			}\ //end case
			default: return balance(100, 80, 100);\
		}\ //end switch
	}\ //end default
} //end switch

#define ENOUGH_HEALTH \
switch(INVENTORY_HEALTH)\
{\
	case 40: return balance(0, 0, 20);\
	case 90:\
	{\
		switch(INVENTORY_ARMORBODY)\
		{\
			case 40:\
			{\
				switch(INVENTORY_ARMORCOMBAT)\
				{\
					case 50:\
					{\
						switch(INVENTORY_ARMORJACKET)\
						{\
							case 60:\
							{\
								return balance(0, 0, 20);\
							}\ //end case
							default: HAS_BFG10K\
						}\ //end switch
					}\ //end case
					default: HAS_BFG10K\
				}\ //end switch
			}\ //end case
			default: HAS_BFG10K\
		}\ //end switch
	}\ //end case
	default:\
	{\
		HAS_BFG10K\
	}\ //end default
} //end switch

#define POWERSCREEN \
switch(ENEMY_POWERSCREEN)\
{
	case 1:\ //enemy has no powerscreen
	{\
		ENOUGH_HEALTH\
	}\ //end case
	default\
	{\
		switch(USING_POWERSCREEN)\
		{\
			case 1:\ //not using the powerscreen
			{\
				return balance(0, 0, 20);\
			}\ //end case
			default:\
			{\
				switch(INVENTORY_CELLS)\
				{\
					case 50: return balance(0, 0, 20);\
					default:\
					{\
						ENOUGH_HEALTH\
					}\ //end default
				}\ //end switch
			}\ //end default
		}\ //end switch
	}\ //end default
} //end switch

weight "aggression"
{
	switch(USING_INVULNERABILITY)
	{
		case 1: //not using the invulnerability
		{
			switch(ENEMY_INVULNERABILITY)
			{
				case 1: //enemy has no invulnerability
				{
					switch(ENEMY_QUAD)
					{
						case 1: //enemy is not using the quad
						{
							POWERSCREEN
						} //end case
						default: //enemy is using the quad
						{
							switch(USING_QUAD)
							{
								case 1: //not using the quad 
								{
									return balance(0, 0, 20);
								} //end case
								default:
								{
									POWERSCREEN
								} //end default
							} //end switch
						} //end default
					} //end switch
				} //end case
				default: return balance(0, 0, 20);
			} //end switch
		} //end if
		default: return balance(100, 80, 100);
	} //end switch
} //end weight


