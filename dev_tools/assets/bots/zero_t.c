//===========================================================================
//
// Name:				zero_c.c
// Function:		Chat lines for Zero
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"


chat "zero"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO
		HELLO3
		HELLO5
	} //end type
	type "exit_game"
	{
		GOODBYE
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
		DEATH_INSULT2
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT9
		KILL_INSULT18
		KILL_INSULT27
		KILL_INSULT36
		KILL_INSULT
	} //end type
	type "kill_praise"
	{
		PRAISE
		PRAISE3
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		TELEFRAGGED2
		TELEFRAGGED3
	} //end type
	type "random_insult"
	{
		TAUNT2
		TAUNT4
		TAUNT6
	} //end type
	type "random_misc"
	{
		MISC2
		MISC5
		MISC8
		MISC14
	} //end type
} //end chat zero
