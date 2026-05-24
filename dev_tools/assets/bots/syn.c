//===========================================================================
//
// Name:				syn.c
// Function:		synonyms
// Programmer:		Mr Elusive [MrElusive@demigod.demon.nl]
// Last update:	1998-12-16
// Tab Size:		3 [real tabs]
// Notes:			-
//===========================================================================

#include "game.h"
#include "syn.h"


CONTEXT_NEARBYITEM
{
	//armor
	[("Body Armor", 1), ("red armor", 1), ("armor red", 0), ("armor body", 0)]
	[("Combat Armor", 1), ("yellow armor", 1), ("armor yellow", 0), ("armor combat", 0)]
	[("Jacket Armor", 1), ("green armor", 1), ("armor green", 0), ("armor jacket", 0)]
	//weapons
	[("Shotgun", 1), ("sg", 0.5)]
	[("Super Shotgun", 1), ("ssg", 0.5), ("double shotgun", 0.7), ("db", 0.7), ("double barrel shotgun", 0)]
	[("Machinegun", 1), ("machine", 0), ("mg", 0)]
	[("Chaingun", 1), ("chain", 0.5), ("cg", 0)]
	[("Grenades", 1), ("hand grenades", 1)]
	[("Grenade Launcher", 1), ("gl", 0.7), ("grenade", 0)]
	[("Rocket Launcher", 1), ("rl", 0.7), ("rocket", 0)]
	[("HyperBlaster", 1), ("hyper", 0.5), ("hb", 0)]
	[("Railgun", 1), ("rail", 0.5), ("rg", 0)]
	[("BFG10K", 1), ("bfg", 1)]
#ifdef XATRIX
	[("Ionripper", 1), ("ion", 1)]
	[("Phalanx", 1), ("phalanks", 0), ("palanx", 0)]
#endif //XATRIX
#ifdef ROGUE
	[("ETF Rifle", 1), ("etf", 0.7)]
	[("Prox Launcher", 1), ("proximity", 1), ("proximity launcher", 1), ("pl", 0)]
	[("Plasma Beam", 1), ("plasmabeam", 1), ("plasma", 0.5)]
	[("Chainfist", 1), ("chain fist", 1)]
	[("Disruptor", 1), ("disintegrator", 1)]
#endif //ROGUE
	//powerups
	[("Quad Damage", 1), ("quad", 1)]
	[("Invulnerability", 1), ("invincebility", 1), ("invul", 0)]
	[("Silencer", 1), ("silence", 0)]
	[("Rebreather", 1), ("breather", 1)]
	[("Environment Suit", 1), ("envirosuit", 1), ("enviro", 1), ("enviro suit", 1), ("environ", 1), ("environ suit", 1), ("environmental suit", 1)]
	[("Ancient Head", 1), ("ancienthead", 0)]
	[("Adrenaline", 1), ("adreline", 0)]
	[("Bandolier", 1), ("bando", 0)]
	[("Ammo Pack", 1), ("pack", 1), ("backpack", 1), ("back pack", 1)]
#ifdef XATRIX
#endif //XATRIX
#ifdef ROGUE
#endif //ROGUE
} //end CONTEXT_NEARBYITEM

//flags
CONTEXT_CTFREDTEAM
{
	[("Red Flag", 0.5), ("base", 1), ("flag1", 0)]
	[("Blue Flag", 0.5), ("enemy flag", 1), ("flag2", 0), ("enemy base", 1)]
} //end CONTEXT_CTFREDTEAM
CONTEXT_CTFBLUETEAM
{
	[("Red Flag", 0.5), ("enemy flag", 1), ("flag1", 0), ("enemy base", 1)]
	[("Blue Flag", 0.5), ("base", 1), ("flag2", 0)]
} //end CONTEXT_CTFBLUETEAM

CONTEXT_NORMAL
{
	//contractions
	[("do not", 1), ("don't", 1), ("dont", 1)]
	[("have not", 1), ("haven't", 1), ("havent", 1)]
	[("would not", 1), ("wouldn't", 1), ("wouldnt", 1)]
	[("should not", 1), ("shoudn't", 1), ("shoudnt", 1)]
	[("could not", 1), ("couldn't", 1), ("couldnt", 1)]
	[("have not", 1), ("haven't", 1), ("havent", 1)]
	[("had not", 1), ("hadn't", 1), ("hadnt", 1)]
	[("can not", 1), ("can't", 1), ("cant", 1), ("cannot", 1)]
	[("will not", 1), ("won't", 1), ("wont", 1)]
	[("did not", 1), ("didn't", 1), ("didnt", 1)]
	[("is not", 1), ("isn't", 1), ("isnt", 1), ("aint", 1)]
	//are
	[("I am", 1), ("I'm", 1), ("Im", 1)]
	[("you are", 1), ("you're", 1), ("youre", 1)]
	[("he is", 1), ("he's", 1), ("hes", 1)]
	[("she is", 1), ("she's", 1), ("shes", 1)]
	[("it is", 1), ("it's", 1), ("its", 1)]
	[("they are", 1), ("they're", 1), ("theyre", 1)]
	[("we are", 1), ("we're", 1), ("were", 1)]
	[("what is", 1), ("what's", 1), ("whats", 1)]
	[("that is", 1), ("that's", 1), ("thats", 1)]
	[("how is", 1), ("how's", 1), ("hows", 1)]
	[("why is", 1), ("why's", 1), ("whys", 1)]
	[("who is", 1), ("who's", 1), ("whos", 1)]
	[("where is", 1), ("where's", 1), ("wheres", 0)]
	//will
	[("I will", 1), ("I'll", 1)]
	[("you will", 1), ("you'll", 1)]
	[("he will", 1), ("he'll", 1)]
	[("she will", 1), ("she'll", 1)]
	[("it will", 1), ("it'll", 1)]
	[("they will", 1), ("they'll", 1), ("theyll", 1)]
	[("we will", 1), ("we'll", 1)]
	[("how will", 1), ("how'll", 1)]
	[("that will", 1), ("that'll", 1)]
	//would
	[("I would", 1), ("I'd", 1)]
	[("you would", 1), ("you'd", 1)]
	[("he would", 1), ("he'd", 1)]
	[("she would", 1), ("she'd", 1)]
	[("it would", 1), ("it'd", 1)]
	[("they would", 1), ("they'd", 1)]
	[("we would", 1), ("we'd", 1)]
	//have
	[("I have", 1), ("I've", 1), ("Ive", 1)]
	[("you have", 1), ("you've", 1), ("youve", 1)]
//	[("he has", 1), ("he's", 1)]									//conflicting with "he is"
//	[("she has", 1), ("she's", 1)]								//conflicting with "she is"
//	[("it has", 1), ("it's", 1)]									//conflicting with "it is"
	[("they have", 1), ("they've", 1), ("theyve", 1)]
	[("we have", 1), ("we've", 1), ("weve", 1)]
//	[("I had", 1), ("I'd", 1), ("Id")]							//conflicting with "I would"
//	[("you had", 1), ("you'd", 1), ("youd", 1)]				//conflicting with "you would"
//	[("he had", 1), ("he'd", 1), ("hed", 1)]					//conflicting with "he would"
//	[("she had", 1), ("she'd", 1)]								//conflicting with "she would"
//	[("it had", 1), ("it'd", 1)]									//conflicting with "it would"
//	[("we had", 1), ("we'd", 1)]									//conflicting with "we would"
	//ing chops
	[("being", 1), ("bein", 1)]
	[("going", 1), ("goin", 1)]
	[("having", 1), ("havin", 1)]
	[("doing", 1), ("doin", 1)]
	[("hanging", 1), ("hangin", 1)]
	[("living", 1), ("livin", 1)]
	//slang
	[("cool", 1), ("kool", 1), ("kwl", 1), ("kewl", 1)]
	[("you", 1), ("ya", 0), ("u", 0)]
	[("are", 1), ("r", 1)]
	[("why", 1), ("y", 1)]
	[("people", 1), ("ppl", 1), ("folks", 1)]
	[("thanx", 1), ("thanks", 1), ("tx", 1), ("tnx", 1)]
	[("this", 1), ("diz", 1)]
	[("men", 1), ("boyz", 1)]
	[("women", 1), ("girlz", 1)]
	[("pics", 1), ("pix", 1)]
	[("everyone", 1), ("everybody", 1)]
	//
	[("going to", 1), ("gonna", 1)]
	[("got to", 1), ("gotta", 1)]
	[("want to", 1), ("wanna", 1)]
	//
	[("waypoint", 1), ("way-point", 1), ("wp", 1)]
	[("checkpoint", 1), ("check-point", 1), ("cp", 1)]
	//numbers
	[("0", 1), ("zero", 1)]
	[("1", 1), ("one", 1)]
	[("2", 1), ("two", 1)]
	[("3", 1), ("three", 1)]
	[("4", 1), ("four", 1)]
	[("5", 1), ("five", 1)]
	[("6", 1), ("six", 1)]
	[("7", 1), ("seven", 1)]
	[("8", 1), ("eight", 1)]
	[("9", 1), ("nine", 1)]
	[("10", 1), ("ten", 1)]
	[("11", 1), ("eleven", 1)]
	[("12", 1), ("twelve", 1)]
	[("13", 1), ("thirteen", 1)]
	[("14", 1), ("fourteen", 1)]
	[("15", 1), ("fifteen", 1)]
	[("16", 1), ("sixteen", 1)]
	[("17", 1), ("seventeen", 1)]
	[("18", 1), ("eighteen", 1)]
	[("19", 1), ("nineteen", 1)]
	[("20", 1), ("twenty", 1)]
	[("30", 1), ("thirty", 1)]
	[("40", 1), ("fourty", 1)]
	[("50", 1), ("fifty", 1)]
	[("60", 1), ("sixty", 1)]
	[("70", 1), ("seventy", 1)]
	[("80", 1), ("eighty", 1)]
	[("90", 1), ("ninety", 1)]
	[("100", 1), ("a hundred", 1), ("one hundred", 1)]
	[("1000", 1), ("a thousand", 1), ("one thousand", 1)]
	[("100000", 1), ("a hundred thousand", 1), ("one hundred thousand", 1)]
	[("1000000", 1), ("a million", 1), ("one million", 1)]
} //end CONTEX_NORMAL

CONTEXT_REPLY
{
	[("that you", 0), ("that I", 0)]
	[("my", 0), ("your", 0)]
	[("you", 0), ("me", 0)]
	[("your", 0), ("my", 0)]
	[("me", 0), ("you", 0)]
	[("mine", 0), ("yours", 0)]
	[("am", 0), ("are", 0)]
	[("yours", 0), ("mine", 0)]
	[("yourself", 0), ("myself", 0)]
	[("myself", 0), ("yourself", 0)]
	[("are", 0), ("am", 0)]
	[("I", 0), ("you", 0)]
} //end CONTEXT_REPLY

