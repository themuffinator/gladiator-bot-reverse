//===========================================================================
//
// Name:				teamplay.h
// Function:		teamplay q & a
// Programmer:		MrElusive (MrElusive@demigod.demon.nl)
// Last update:	1999-04-12 (by Squatt)
// Tab Size:		3 (real tabs)
//===========================================================================

	//the bot doesn't know who someone is
	type "whois"
	{
		"who is ", 0;
		"who uses the name ", 0;
		"don't know of any team mate ", 0;
	} //end type
	//the bot doesn't know where someone is hanging out
	type "whereis"
	{
		"where is ", 0;
		"don't know where ", 0, " is";
		"can someone tell me where i can find ", 0;
		"where is ", 0, " hanging out?";
		"where the f*ck is ", 0;
	} //end type
	//the bot asks where you are
	type "whereareyou"
	{
		"where are you ", 0;
		"where the hell are you, ", 0, "?";
		"don't know where you are ", 0;
		"tell me where you are ", 0;
		"tell me where to find you, ", 0;
	} //end type
	//cannot find something
	type "cannotfind"
	{
		"don't know where to find a ", 0;
		"where can I find a ", 0, "?";
		"where the hell is a ", 0;
	} //end type
	//start helping
	type "help_start"
	{
		"I'm coming to help you ", 0;
		"help is on it's way ", 0;
		"hang in there ", 0, " I'm on my way to help you";
		"hang in there ", 0, " I'm coming to help";
		"keep the enemy busy ", 0, " help is on it's way";
		"the cavalry is coming to the rescue, ",0;
	} //end type
	//start accompanying
	type "accompany_start"
	{
		"i'll be your companion ", 0;
		"i'll follow you around ", 0;
		"yes sir, i'll accompany you ", 0;
		"ok ", 0, " i'll accompany you";
		"ok ", 0, " lead the way";
		"yes sir, following you around will be my mission in life ", 0;
		"lead the way ", 0, " i'll follow";
		"just call me tonto, ", 0;
	} //end type
	//stop accompanying
	type "accompany_stop"
	{
		"i'm going my own way now ", 0;
		"i've got anough of this follow the leader stuff ", 0;
		"being your companion has been nice while it lasted ", 0, " :)";
		"i've got other appointments ", 0, " i won't follow you anymore";
	} //end type
	//cannot find companion
	type "accompany_cannotfind"
	{
		"i can't find you ", 0, " i'm going my own way now";
		"can't find you ", 0, ", ask someone else";
		"i'm gonna do something else because i can't find you ", 0;
		"where are you hiding, ", 0 , "? i give up already";
	} //end type
	//arrived at companion
	type "accompany_arrive"
	{
		"at your service ", 0;
		"your wish is my command ", 0;
		"ready to go ", 0;
		"ready for your command ", 0;
		"i'm in position ", 0;
		"awaiting your command ", 0;
		"at your command ", 0;
		"as you wish ", 0;
		"your orders ", 0;
		"ready when you are ", 0;
		"set and ready ", 0;
		"reporting for duty, ", 0;
		"just lead the way, ", 0;
		"YES SIR!!!!";
		"reporting for duty sir!";
	} //end type
	//start defending a key area
	type "defend_start"
	{
		"i'm gonna defend the ", 0;
		"i'll guard the ", 0;
		"i'm going to defend the ", 0;
		"leave defending the ", 0, " to me";
		"leave guarding the ", 0, " to me";
		"yes sir, i'll guard the ", 0;
	} //end type
	//stop defending a key area
	type "defend_stop"
	{
		"i'll stop defending the ", 0;
		"i've had enough of defending the ", 0;
		"i'm through guarding the damn ", 0;
		"i've been defending the ", 0, " for long enough now";
		"have someone else defend the ", 0, " i'm going to do something else";
		"have someone else defend the ", 0, " i'm off";
		"have someone else defend the ", 0, " i quit";
	} //end type
	//start camping
	type "camp_start"
	{
		"ok I'll camp ", 0;
		"I love campings ", 0;
		"yes sir I'll camp ", 0;
		"I'd love to camp ", 0;
		"leave camping to me ", 0;
	} //end type
	//stop camping
	type "camp_stop"
	{
		"I've had enough of camping here";
		"I'm through comping here";
		"i've been camping here long anough now";
		"I hate camping here I'm off";
	} //end type
	//in camp position
	type "camp_arrive" //0 = one that ordered the bot to camp
	{
		"I'm in position ", 0;
		"I'll take care of the enemy from here ", 0;
	} //end type
	//start patrolling
	type "patrol_start" //0 = locations
	{
		"I'm gonna patrol from ", 0;
	} //end type
	//stop patrolling
	type "patrol_stop"
	{
		"I'm through with patrolling";
	} //end type
	//start trying to capture the enemy flag
	type "captureflag_start"
	{
		"i'm gonna capture the enemy flag";
		"i'm off to grab the enemy flag";
		"i'm going to get the enemy flag";
		"i'm on my way to capture the enemy flag";
		"the enemy flag will be mine!";
	} //end type
	//the bot joined a sub-team
	type "joinedteam"
	{
		"i'm in team ", 0;
		"i'm on team ", 0;
		"i joined team ", 0;
		"i've joined team ",0;
		"ok ", 0, " is my team";
		"yes sir, ", 0, " is my team";
		"yes sir, i joined team ", 0;
		"ok i joined team ", 0;
	} //end type
	//bot leaves a sub team
	type "leftteam" //0 = team name
	{
		"i left team ", 0;
		"i'm outa team ", 0;
		"i'm not in ", 0, " anymore";
	} //end type
	//the checkpoint is invalid
	type "checkpoint_invalid"
	{
		"invalid checkpoint";
	} //end type
	//confirm the checkpoint
	type "checkpoint_confirm" //0 = name, 1 = gps
	{
		"checkpoint ", 0, " at ", 1, " confirmed";
	} //end type
	//the bot is helping someone
	type "helping"
	{
		"i'm helping ", 0;
		"i try to help ", 0;
	} //end type
	//the bot is accompanying someone
	type "accompanying"
	{
		"i'm accompanying ", 0;
		"i'm following ", 0;
		"i cover ", 0, "'s back";
		"i'll be ", 0, "'s bodyguard";
	} //end type
	//the bot is defending something
	type "defending"
	{
		"i'm defending the ", 0;
		"i'm guarding the ", 0;
		"the ", 0," is under my supervision";
		"i shall guard the ", 0;
		"no need to panic, i'm guarding the ", 0;
	} //end type
	//the bot is camping
	type "camping"
	{
		"i'm camping";
	} //end type
	//the bot is patrolling
	type "patrolling"
	{
		"i'm patrolling";
	} //end type
	//the bot is capturing the flag
	type "capturingflag"
	{
		"i'm trying to capture the flag";
		"i'm getting the enemy flag";
		"i'm stealing the enemy flag";
		"i'm laying my paws on the enemy flag now";
	} //end type
	//the bot is rushing to the base
	type "rushingbase"
	{
		"i'm rushing to the base";
		"i'm going to the base";
		"i'm running to the base";
		"i'm heading for the base";
	} //end type

