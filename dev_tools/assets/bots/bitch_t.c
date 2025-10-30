//===========================================================================
//
// Name:				bitch_c.c
// Function:		Chat lines for the Quad Bitch character
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "bitch"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO
		HELLO1
		HELLO2
	} //end type

	type "exit_game"
	{
		GOODBYE1
		GOODBYE3
		GOODBYE5
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
		DEATH_INSULT1
		DEATH_FEM_INSULT
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT
		KILL_INSULT2
		KILL_INSULT8
		KILL_INSULT12
		KILL_INSULT16
		KILL_INSULT24
		KILL_INSULT29
	} //end type
	type "kill_praise"
	{
		PRAISE3
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED1
		TELEFRAGGED6
		TELEFRAGGED
		"did i do that?";
	} //end type
	type "random_insult"
	{
		TAUNT3
		TAUNT7
		TAUNT_FEM1
		TAUNT_FEM3
		TAUNT_FEM5
		TAUNT_FEM7
		"what are we waiting for? christmas?";
		"your momma is an 8086";
		"i'm real and you are all bots";
		"you're all stupid campers";
		"any one of you know Mr Elusive?";
		"lets go to another server";
		"let's go and kill ", 0;
		"anyone know where ", 0, " is?";
	} //end type
	type "random_misc"
	{
		MISC4
		MISC8
		MISC12
		MISC6
		"i like unreal better";
		"give me doom anyday";
		"what do you guys want from me?";
		"did tim willits do this level?";
		"did you know we are going to port quake3 to the unreal engine?";
		"these gladiator bots suck man!";
		"hey! there are some bugs in this bot code!";
		"did i program this?";
	} //end type
} //end chat bitch
