//===========================================================================
//
// Name:				messia_c.c
// Function:		chats for the Shotgun Messiah
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "messiah"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO7
		HELLO2
		HELLO3
		HELLO4
	} //end type
	type "exit_game"
	{
		GOODBYE5
		GOODBYE3
		GOODBYE4
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
		DEATH_FEM_INSULT
		DEATH_FEM_INSULT1
	} //end type
	type "death_praise"
	{
		D_PRAISE
		"good one";
	} //end type
	type "kill_insult"
	{
		"next time, use a bigger gun";
		"all bow before the might of the shotgun messiah";
		"WHOAAAAAAH!!!!!!!!!!";
		"you should practice more often";
		KILL_INSULT25
		KILL_INSULT26
		KILL_INSULT27
		KILL_INSULT28
		KILL_INSULT30
		KILL_INSULT32
		KILL_INSULT31
	} //end type
	type "kill_praise"
	{
		PRAISE2
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED5
		TELEFRAGGED2
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT4
		TAUNT_FEM
		TAUNT_FEM1
		TAUNT_FEM2
	} //end type
	type "random_misc"
	{
		MISC12
		MISC11
		MISC10
		MISC9
	} //end type
} //end chat messiah
