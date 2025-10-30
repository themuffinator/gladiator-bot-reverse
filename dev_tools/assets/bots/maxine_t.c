//===========================================================================
//
// Name:				maxine_c.c
// Function:		chat file for Maxine Character
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "maxine"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO7
		HELLO3
		HELLO2
	} //end type
	type "exit_game"
	{
		GOODBYE
		GOODBYE1
		GOODBYE5
	} //end type
	type "start_level"
	{
		STARTLEVEL2
	} //end type
	type "end_level"
	{
		ENDLEVEL
	} //end type
	type "death_bfg"
	{
		DEATH_BFG
		DEATH_BFG1
		DEATH_BFG2
	} //end type
	type "death_insult"
	{
		DEATH_INSULT5
		DEATH_FEM_INSULT1
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT12
		KILL_INSULT13
		KILL_INSULT14
		"jeeez! ever heard about strafing, ", 0, "?";
		0, ", you should move faster";
	} //end type
	type "kill_praise"
	{
		PRAISE1
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED6
	} //end type
	type "random_insult"
	{
		TAUNT1
		TAUNT3
		TAUNT5
		TAUNT7
		TAUNT_FEM4
		TAUNT_FEM5
		TAUNT_FEM6
		TAUNT_FEM7
		TAUNT_FEM
	} //end type
	type "random_misc"
	{
		"I was born ready";
	} //end type
} //end chat maxine
