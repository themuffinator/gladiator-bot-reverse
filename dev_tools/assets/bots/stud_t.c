//===========================================================================
//
// Name:				stud_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "stud"
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
		GOODBYE
		GOODBYE2
		GOODBYE5
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
		DEATH_INSULT
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT
		KILL_INSULT1
		KILL_INSULT34
		KILL_INSULT24
	} //end type
	type "kill_praise"
	{
		PRAISE2
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED2
		TELEFRAGGED5
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT2
		TAUNT6
	} //end type
	type "random_misc"
	{
		MISC3
		MISC6
		MISC9
		MISC12
		MISC15
	} //end type
} //end chat stud
