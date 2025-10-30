//===========================================================================
//
// Name:				sparta_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"


chat "spartacus"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO4
		HELLO5
		HELLO6
	} //end type
	type "exit_game"
	{
		GOODBYE
		GOODBYE3
		GOODBYE4
	} //end type
	type "start_level"
	{
		STARTLEVEL1
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
	} //end type
	type "death_praise"
	{
		D_PRAISE
		"good one mate";
	} //end type
	type "kill_insult"
	{
		KILL_INSULT12
		KILL_INSULT22
		KILL_INSULT26
		KILL_INSULT30
		KILL_INSULT35
		KILL_INSULT36
	} //end type
	type "kill_praise"
	{
		PRAISE1
		"good one mate";
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED3
		TELEFRAGGED4
		TELEFRAGGED5
		TELEFRAGGED6
	} //end type
	type "random_insult"
	{
		TAUNT1
		TAUNT3
		TAUNT6
	} //end type
	type "random_misc"
	{
		MISC2
		MISC4
		MISC6
		MISC8
	} //end type
} //end chat spartacus
