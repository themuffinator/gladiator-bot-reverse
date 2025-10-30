//===========================================================================
//
// Name:				luuzr_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "luuzr"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO2
		HELLO3
		HELLO4
	} //end type
	type "exit_game"
	{
		GOODBYE2
		GOODBYE4
		GOODBYE5
	} //end type
	type "start_level"
	{
		STARTLEVEL
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
		DEATH_INSULT
		"exit light, enter night ", 0;
		"where's your crown, king nothing?";
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT16
		KILL_INSULT17
		KILL_INSULT18
	} //end type
	type "kill_praise"
	{
		PRAISE1
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED6
		TELEFRAGGED4
	} //end type
	type "random_insult"
	{
		TAUNT7
		TAUNT8
		TAUNT
	} //end type
	type "random_misc"
	{
		MISC3
		MISC12
		MISC4
		"i'll kill anyone else laughing at my pants";
		"reload!!! reload!!!";
		"am i evil?  why, yes I am";
	} //end type
} //end chat luuzr
