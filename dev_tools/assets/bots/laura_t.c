//===========================================================================
//
// Name:				laura_t.c
// Function:		chats for laura craft character
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990511a
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"


chat "laura"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO2
		HELLO3
		HELLO5
		HELLO6
	} //end type
	type "exit_game"
	{
		GOODBYE
		GOODBYE1
		GOODBYE2
	} //end type
	type "start_level"
	{
		STARTLEVEL
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
		DEATH_INSULT3
		DEATH_FEM_INSULT
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT25
		KILL_INSULT26
		KILL_INSULT27
	} //end type
	type "kill_praise"
	{
		PRAISE2
		PRAISE3
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		TELEFRAGGED3
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT5
		TAUNT6
		TAUNT_FEM
		TAUNT_FEM1
		TAUNT_FEM3
		TAUNT_FEM5
		TAUNT_FEM7
	} //end type
	type "random_misc"
	{
		MISC12
		MISC13
		MISC14
		MISC15
		"no i'm not lara's sister";
		"any similarities are purely coincidence";
		"yes, these are my own tits";
	} //end type
} //end chat laura
