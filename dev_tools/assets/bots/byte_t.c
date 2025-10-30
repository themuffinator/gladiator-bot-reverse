//===========================================================================
//
// Name:				byte_c.c
// Function:		chats for Byte bot character
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "byte"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO6
		HELLO5
		HELLO2
	} //end type
	type "exit_game"
	{
		GOODBYE
		GOODBYE2
		GOODBYE4
	} //end type
	type "start_level"
	{
		STARTLEVEL1
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
		DEATH_INSULT1
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT19
		KILL_INSULT20
		KILL_INSULT21
	} //end type
	type "kill_praise"
	{
		PRAISE2
		"watch yer back next time ";
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED4
	} //end type
	type "random_insult"
	{
		TAUNT1
		TAUNT5
		TAUNT8
	} //end type
	type "random_misc"
	{
		MISC4
		MISC5
		MISC6
		MISC7
	} //end type
} //end chat byte
