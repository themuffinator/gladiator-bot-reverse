//===========================================================================
//
// Name:				defaul_m.c
// Function:		bot memory
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-05-12
// Tab Size:		3 (real tabs)
//===========================================================================


#include "inv.h"
#include "game.h"


checkpoints
{
	mapname "q2dm1"
	{
		cp(x, y, z, "name");
		cp(x, y, z, "name");
	}
	mapname "q2dm2"
	{
		cp(x, y, z, "name");
	}
}

playerstatistics
{
	"playername", kills, deaths;
}

mapstatistics
{
	mapname "q2dm1"
	{
		timesplayed;
	}
}

