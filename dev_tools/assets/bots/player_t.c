//===========================================================================
//
// Name:				player_c.c
// Function:
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "player"
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
		GOODBYE1
		GOODBYE3
		GOODBYE5
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
		DEATH_INSULT5
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT34
		KILL_INSULT35
		KILL_INSULT36
	} //end type
	type "kill_praise"
	{
		PRAISE
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		TELEFRAGGED1
		TELEFRAGGED2
		TELEFRAGGED6
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT3
		TAUNT7
	} //end type
	type "random_misc"
	{
		"i've got a lot of experience";
		"how do I use this thing? ";
		"who is that blue glowing guy";
		"what is that red glowing stuff?";
		"i don't remember this level";
		"where are all the good weapons at?";
		"why do you guys move so fast?";
		MISC11
		MISC13
		MISC15
		MISC9
	} //end type
} //end chat player
