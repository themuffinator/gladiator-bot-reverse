//===========================================================================
//
// Name:				omicro_c.c
// Function:		chat lines for the demigoddess
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218a (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "omicron"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO
		HELLO1
		HELLO2
		HELLO3
		HELLO6
		HELLO7
	} //end type
	type "exit_game"
	{
		GOODBYE2
		GOODBYE4
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
		DEATH_INSULT4
		DEATH_FEM_INSULT1
		"resurgam";//I shall rise again
		"non placet"; //it does not please
		"plebian";
		"oops!!";
		"ouch!";
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT31
		KILL_INSULT32
		KILL_INSULT33
	} //end type
	type "kill_praise"
	{
		PRAISE1
		PRAISE2
	} //end type
	type "kill_telefrag"
	{
	} //end type
	type "random_insult"
	{
		TAUNT3
		TAUNT5
		TAUNT7
		TAUNT_FEM
		TAUNT_FEM1
		TAUNT_FEM2
		TAUNT_FEM3
		TAUNT_FEM4
		TAUNT_FEM6
	} //end type
	type "random_misc"
	{
		"errare est humanum";//to err is human
		"exitus acta probat";//the outcome justifies the deed
		"hodie mihi, cras tibi";//me today, you tomorrow
		"facta non verba";//deeds, not words
		"aut vincere aut mori";//to conquer or die
		"cogito, ergo sum";//I think, therefore I am
		"omicron optimus maximus";//omicron best and greatest
		"proh pudor!"; //oh, for shame
		"aut gladiator bots aut nullus"; //either gladiator bots or nothing
		"aut vincere aut mori"; //to conquer or die
		MISC2
		MISC4
		MISC6
		MISC8
	} //end type
} //end chat omicron
