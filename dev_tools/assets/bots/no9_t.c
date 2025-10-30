//===========================================================================
//
// Name:				no9_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

	
chat "no9"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO
		HELLO3
		HELLO7
	} //end type
	type "exit_game"
	{
		GOODBYE
		GOODBYE2
		GOODBYE3
	} //end type
	type "start_level"
	{
		STARTLEVEL1
	} //end type
	type "end_level"
	{
		ENDLEVEL1
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
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT28
		KILL_INSULT29
		KILL_INSULT30
	} //end type
	type "kill_praise"
	{
		PRAISE
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED4
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT4
		TAUNT8
	} //end type
	type "random_misc"
	{
		MISC
		MISC1
		MISC3
		MISC5
	} //end type
} //end chat no9
