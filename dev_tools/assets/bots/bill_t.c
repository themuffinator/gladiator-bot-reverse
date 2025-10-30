//===========================================================================
//
// Name:				Bill_c.c
// Function:		Chat lines for Bill Gates (the bot character of course, who else?)
// Programmer:		B.D.Squatt (Squatt@demigod.demon.nl)
// Last update:	19990218 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

#include "ichat.h"

chat "bill"
{
	#include "teamplay.h"
	//
	type "enter_game"
	{
		HELLO3
		HELLO4
		HELLO5
	} //end type
	type "exit_game"
	{
		GOODBYE3
		GOODBYE4
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
		"mommy!!!! I've been gibbed again!!!";
		"the bsa will hunt you down";
		"i want my mommy!!!!";
		"just wait until the next windows upgrade!!!";
	} //end type
	type "death_praise"
	{
		D_PRAISE
	} //end type
	type "kill_insult"
	{
		KILL_INSULT3
		KILL_INSULT4
		KILL_INSULT5
		"if it isn't microsoft, it just isn't....";
		"microsoft wins again";
		"that's what happens to software pirates!";
		"you will go the commodore way!";
		"now if you upgraded to windows 2010....";
		">:(";
		KILL_INSULT
	} //end type
	type "kill_praise"
	{
		PRAISE1
		":)";
		">:)";
	} //end type
	type "kill_telefrag"
	{
		TELEFRAGGED3
		TELEFRAGGED4
	} //end type
	type "random_insult"
	{
		"hey, your windows is pirated!";
		"there is a virus on your hard disk";
		TAUNT3
		TAUNT4
		TAUNT5
	} //end type
	type "random_misc"
	{
		"anybody wanna buy my quake clone? ";
		"is it true that nobody likes me?";
		"i've got more money than skill";
		"if u can't beat 'em, buy 'em";
		"anybody got a dime to spare?";
		"i hate sun and java suck!!!";
		"640K ought to be enough for anybody";
		"if you can't make it good, make it look good.";
		"microsoft is better than netscape";
		"ms quake will soon be released";
		"anybody seen john carmack?";
		"doomed, I'm doomed";
		"rockets, I need rockets";
		"pull the other one";
		"atmosphere..that's it!";
		"did I do this level?";
		"anyone for motorcross madness?";
		"if it isn't microsoft, it just isn't....";
		MISC10
		MISC11
		MISC12
	} //end type
} //end chat bill
