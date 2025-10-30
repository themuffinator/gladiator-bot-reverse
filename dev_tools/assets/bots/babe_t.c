//===========================================================================
//
// Name:				babe_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "babe"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO6
		HELLO7
		HELLO2
	} //end type
	type "exit_game"
	{
		GOODBYE4
		GOODBYE5
		GOODBYE2
	} //end type
	type "start_level"
	{
		STARTLEVEL
	} //end type
	type "end_level"
	{
		ENDLEVEL2
	} //end type
	type "death_bfg"
	{
		DEATH_BFG
		DEATH_BFG1
		DEATH_BFG2
	} //end type
	type "death_insult"
	{
		DEATH_INSULT3
		DEATH_FEM_INSULT
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT6
		KILL_INSULT7
		KILL_INSULT8
		"don't mess with the silicon babe";
		"the only good man is a dead man";
		"the babe did it again!";
		"hey, your blood matches my nailpolish";
	} //end type
	type "kill_praise"
	{
		"the only good man is a dead man";
		PRAISE3
	} //end type
	type "kill_telefrag"
	{
		"sorry ", 0, ", didn't see you there. ;)";
		TELEFRAGGED6
		TELEFRAGGED5
	} //end type
	type "random_insult"
	{
		TAUNT5
		TAUNT6
		TAUNT8
		TAUNT_FEM
		TAUNT_FEM1
		TAUNT_FEM2
		TAUNT_FEM3
		TAUNT_FEM4
		TAUNT_FEM5		
		TAUNT_FEM6
		TAUNT_FEM7
	} //end type
	type "random_misc"
	{
		"isn't this game just the coolest";
		"anybody seen my eyeliner ?";
		"damned! i dropped my purse somewhere!!!!";
		"lara croft, eat yer heart out!";
		MISC10
		MISC3
		MISC15
		"l'm the queen of the hill";
		"lara croft, meet your match";
		"Pamela is the silicone babe; me i'm just silicon";
	} //end type
} //end chat babe
