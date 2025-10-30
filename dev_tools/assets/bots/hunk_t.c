//===========================================================================
//
// Name:				hunk_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "hunk"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO4
		HELLO5
		HELLO
	} //end type
	type "exit_game"
	{
		GOODBYE3
		GOODBYE4
		GOODBYE2
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
		"Is that as good as you can do, ",0;
		DEATH_BFG
		DEATH_BFG1
		DEATH_BFG2
	} //end type
	type "death_insult"
	{
		DEATH_INSULT4
		"'tis but a flesh wound";
		"i've had worse cuts shaving";
		"my ulcer hurts more";
	} //end type
	type "death_praise"
	{
		D_PRAISE
		"i should have remained a lumberjack";
	} //end type
	type "kill_insult"
	{
		KILL_INSULT9
		KILL_INSULT10
		KILL_INSULT11
		"is that as good as you can do, ",0,"?";
	} //end type
	type "kill_praise"
	{
		PRAISE
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		TELEFRAGGED6
	} //end type
	type "random_insult"
	{
		TAUNT4
		TAUNT6
		TAUNT8
		"is that as good as you can do, ",0;
	} //end type
	type "random_misc"
	{
		"DOOM! DOOM! DOOM!";
		MISC15
		MISC14
		MISC10
		MISC9
		MISC3
		MISC12
		MISC8
	} //end type
} //end chat hunk
