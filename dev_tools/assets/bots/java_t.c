//===========================================================================
//
// Name:				Java_t.c
// Function:		chats for Java Man
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	1999-02-18 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "java"
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
		GOODBYE
		GOODBYE1
		GOODBYE2
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
		KILL_INSULT2
		"smoke my cigahs";
		"Subject has been terminated";
	} //end type
	type "kill_praise"
	{
		PRAISE
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED
		"sorry ", 0, "didn't see you there. ;)";
		TELEFRAGGED1
		TELEFRAGGED2
	} //end type
	type "random_insult"
	{
		TAUNT
		TAUNT1
		TAUNT2
		"smoke my cigahs";
		"don't mess with an austrian";
		"your thorax is mine ", 0, exclamation;
	} //end type
	type "random_misc"
	{
		MISC
		MISC1
		MISC2
		MISC3
		"my suit is prettier than batman's";
		"i'm cuter than that bat eared playboy";
		"achtung baby!!";
		"subject will be terminated";
		"no problemo";
		"no way jose";
		"smoke my cigahs";
		"achtung!!";
	} //end type
	
} //end chat java

