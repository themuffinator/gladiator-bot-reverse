//===========================================================================
//
// Name:				trash_c.c
// Function:		chat lines for Trash
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "trash"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO3
		HELLO5
		HELLO7
	} //end type
	type "exit_game"
	{
		GOODBYE1
		GOODBYE3
		GOODBYE5
	} //end type
	type "start_level"
	{
		STARTLEVEL2
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
		KILL_INSULT22
		KILL_INSULT23
		KILL_INSULT24
	} //end type
	type "kill_praise"
	{
		PRAISE3
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		TELEFRAGGED3
	} //end type
	type "random_insult"
	{
		TAUNT2
		TAUNT6
		TAUNT7
		"did you forget to flush the toilet ", 1, "?";
	} //end type
	type "random_misc"
	{
		MISC8
		MISC9
		MISC10
		MISC11
		"how about warcraft 4 a change?";
		"this quakey stuff ain't half bad";
		"anyone like the teletubbies?";
		"anybody got tickets for the spice girls?";
		"i put people on ice";
		"my skin is really cool";
		"who wants to be put on Ice then?";
	} //end type
} //end chat trash
