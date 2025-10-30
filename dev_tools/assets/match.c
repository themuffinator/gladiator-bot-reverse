//===========================================================================
//
// Name:				match.c
// Function:		match templates
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-05-12
// Tab Size:		3 (real tabs)
// Notes:			currently maximum of 10 match variables
//                this looks a little too easy :) but it's very fast
//                and with decent match variable interpretation it's
//                pretty flexible and certainly usable
//===========================================================================

#include "game.h"
#include "match.h"

/*
//3.14 death messages
VICTIM, " suicides." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " cratered." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " was squished." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " sank like a rock." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " melted." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " does a back flip into the lava." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " blew up." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " found a way out." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " saw the light." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " got blasted." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " was in the wrong place." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " tried to put the pin back in." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " tripped on her own grenade." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " tripped on his own grenade." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " blew herself up." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " blew himself up." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " should have used a smaller gun." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " killed herself." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " killed himself." = (MSG_DEATH, ST_DEATH_SUICIDE);
VICTIM, " was blasted by ", KILLER = (MSG_DEATH, ST_DEATH_BLASTER);
VICTIM, " was gunned down by ", KILLER = (MSG_DEATH, ST_DEATH_SHOTGUN);
VICTIM, " was blown away by ", KILLER, "'s super shotgun" = (MSG_DEATH, ST_DEATH_SUPERSHOTGUN);
VICTIM, " was machinegunned by ", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
VICTIM, " was cut in half by ", KILLER, "'s chaingun" = (MSG_DEATH, ST_DEATH_CHAINGUN);
VICTIM, " was popped by ", KILLER, "'s grenade" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
VICTIM, " was shredded by ", KILLER, "'s shrapnel" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
VICTIM, " ate ", KILLER, "'s rocket" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
VICTIM, " almost dodged ", KILLER, "'s rocket" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
VICTIM, " was melted by ", KILLER, "'s hyperblaster" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
VICTIM, " was railed by ", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
VICTIM, " saw the pretty lights from ", KILLER, "'s BFG" = (MSG_DEATH, ST_DEATH_BFG);
VICTIM, " was disintegrated by ", KILLER, "'s BFG blast" = (MSG_DEATH, ST_DEATH_BFG);
VICTIM, " couldn't hide from ", KILLER, "'s BFG" = (MSG_DEATH, ST_DEATH_BFG);
VICTIM, " caught ", KILLER, "'s handgrenade" = (MSG_DEATH, ST_DEATH_GRENADES);
VICTIM, " didn't see ", KILLER, "'s handgrenade" = (MSG_DEATH, ST_DEATH_GRENADES);
VICTIM, " feels ", KILLER, "'s pain" = (MSG_DEATH, ST_DEATH_GRENADES);
VICTIM, " tried to invade ", KILLER, "'s personal space" = (MSG_DEATH, ST_DEATH_TELEFRAG);
VICTIM, " died." = (MSG_DEATH, ST_DEATH_SUICIDE);
*/

//client obituary messages
MTCONTEXT_CLIENTOBITUARY
{
	//suicides
	VICTIM, "commits suicide" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "takes the easy way out" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "has fragged ", GENDER_HIM, "self" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "took ", GENDER_HIS, " own life" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "can be scraped off the pavement" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "cratered" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "discovers the effects of gravity" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "was squished" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "was squeezed like a ripe grape" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "turned to juice" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "sank like a rock" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried unsuccesfully to breathe water" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to immitate a fish" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "must learn when to breathe" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "needs to learn how to swim" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "took a long walk of a short pier" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "might want to use a rebreather next time" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " didn't need a rebreather" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "melted" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "was dissolved" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "sucked slime" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "found an alternative way to die" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "needs more slime-resistance" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "might try on an environmental suit next time" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "does a back flip into the lava" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "was fried to a crisp" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought that lava was water" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "turned into a real hothead" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought lava was 'funny water'" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to hide in the lava" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " was fire resistant" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to emulate the ", GENDER_GOD = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "needs to rebind ", GENDER_HIS, " 'strafe' keys" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "blew up" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "found a way out" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "had enough for today" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "exit, stage left" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "has returned to real life(tm)" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "saw the light" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "got blasted" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "was in the wrong place" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "shouldn't play with equipment" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "can't move around moving objects" = (MSG_DEATH, ST_DEATH_SUICIDE);
#ifdef XATRIX
	VICTIM, "that's gotta hurt" = (MSG_DEATH, ST_DEATH_SUICIDE);
#endif //XATRIX
	VICTIM, "tried to put the pin back in" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "got the red and blue wires mixed up" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "held ", GENDER_HIS, " grenade too long" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to disassemble ", GENDER_HIS, " own grenade" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to grenade-jump unsuccessfully" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to play football with a grenade" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "shouldn't mess around with explosives" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tripped on ", GENDER_HIS, " own grenade" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "stepped on ", GENDER_HIS, " own pineapple" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "knows didley squatt about rocket launchers" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought up a novel new way to fly" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "blew ", GENDER_HIM, "self up" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " was Werner von Braun" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " had more health" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "found ", GENDER_HIS, " own rocketlauncher's trigger" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " had more armor on" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "blew ", GENDER_HIM, "self to kingdom come" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "should have used a smaller gun" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "shouldn't play with big guns" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "doesn't know how to work the BFG" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "has trouble using big guns" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "can't distinguish which end is which with the BFG" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "should try to avoid using the BFG near obstacles" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "tried to BFG-jump unsuccesfully" = (MSG_DEATH, ST_DEATH_SUICIDE);
#ifdef XATRIX
	VICTIM, "sucked into ", GENDER_HIS, " own trap" = (MSG_DEATH, ST_DEATH_SUICIDE);
#endif //XATRIX
#ifdef ROGUE
	VICTIM, "got caught in ", GENDER_HIS, " own trap" = (MSG_DEATH, ST_DEATH_SUICIDE);
#endif //ROGUE
	VICTIM, "commited suicide" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "went the way of the dodo" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought 'kill' was a funny console command" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "wanted one frag less" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "killed ", GENDER_HIM, "self" = (MSG_DEATH, ST_DEATH_SUICIDE);
	VICTIM, "thought ", GENDER_HE, " had one many frags" = (MSG_DEATH, ST_DEATH_SUICIDE);
	//kills
	//MOD_BLASTER:
	VICTIM, "(quakeweenie) was massacred by", KILLER, " (quakegod)!!!" = (MSG_DEATH, ST_DEATH_BLASTER);
	VICTIM, "was killed with the wimpy blaster by", KILLER = (MSG_DEATH, ST_DEATH_BLASTER);
	VICTIM, "died a wimp's death by", KILLER = (MSG_DEATH, ST_DEATH_BLASTER);
	VICTIM, "can't even avoid a blaster from", KILLER = (MSG_DEATH, ST_DEATH_BLASTER);
	VICTIM, "was blasted by", KILLER = (MSG_DEATH, ST_DEATH_BLASTER);
	//MOD_SHOTGUN:
	VICTIM, "was gunned down by", KILLER = (MSG_DEATH, ST_DEATH_SHOTGUN);
	VICTIM, "found ", GENDER_HIM, "self on the wrong end of", KILLER, "'s gun" = (MSG_DEATH, ST_DEATH_SHOTGUN);
	//MOD_SSHOTGUN:
	VICTIM, "was blown away by", KILLER, "'s super shotgun" = (MSG_DEATH, ST_DEATH_SUPERSHOTGUN);
	VICTIM, "had ", GENDER_HIS, " ears cleaned out by", KILLER, "'s super shotgun" = (MSG_DEATH, ST_DEATH_SUPERSHOTGUN);
	VICTIM, "was put full of buckshot by", KILLER = (MSG_DEATH, ST_DEATH_SUPERSHOTGUN);
	//MOD_MACHINEGUN:
	VICTIM, "was machinegunned by", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "was filled with lead by", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "was put full of lead by", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "was pumped full of lead by", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "ate lead dished out by", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "eats lead from", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	VICTIM, "bites the bullet from", KILLER = (MSG_DEATH, ST_DEATH_MACHINEGUN);
	//MOD_CHAINGUN:
	VICTIM, "was cut in half by", KILLER, "'s chaingun" = (MSG_DEATH, ST_DEATH_CHAINGUN);
	VICTIM, "was turned into a strainer by" = (MSG_DEATH, ST_DEATH_CHAINGUN);
	VICTIM, "was put full of holes by" = (MSG_DEATH, ST_DEATH_CHAINGUN);
	VICTIM, "couldn't avoid death by painless from" = (MSG_DEATH, ST_DEATH_CHAINGUN);
	VICTIM, "was put so full of lead by", KILLER, " you can call ", GENDER_HIM, " a pencil" = (MSG_DEATH, ST_DEATH_CHAINGUN);
	//MOD_GRENADE:
	VICTIM, "was popped by", KILLER, "'s grenade" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
	VICTIM, "caught", KILLER, "'s handgrenade in the head" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
	VICTIM, "tried to headbutt the handgrenade of" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
	VICTIM, "was shredded by", KILLER, "'s shrapnel" = (MSG_DEATH, ST_DEATH_GRENADELAUNCHER);
	//MOD_ROCKET:
	VICTIM, "ate", KILLER, "'s rocket" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "sucked on", KILLER, "'s boomstick" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "tried to play 'dodge the missile' with" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "tried the 'patriot move' on the rocket from" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "had a rocket stuffed down the throat by" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "got a rocket up the tailpipe by" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "tried to headbutt", KILLER, "'s rocket" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "almost dodged", KILLER, "'s rocket" = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "was spread around the place by", KILLER = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "was gibbed by", KILLER = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "has been blown to smithereens by", KILLER = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	VICTIM, "was blown to itsie bitsie tiny pieces by", KILLER = (MSG_DEATH, ST_DEATH_ROCKETLAUNCHER);
	//MOD_HYPERBLASTER:
	VICTIM, "was melted by", KILLER, "'s hyperblaster" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
	VICTIM, "was used by", KILLER, " for target practice" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
	VICTIM, "was hyperblasted by" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
	VICTIM, "was pumped full of cells by" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
	VICTIM, "couldn't outrun the hyperblaster from" = (MSG_DEATH, ST_DEATH_HYPERBLASTER);
	//MOD_RAILGUN:
	VICTIM, "was railed by", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "played 'catch the slug' with", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "bites the slug from", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "caught the slug from", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "got a slug put through ", GENDER_HIM, " by", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "was corkscrewed through ", GENDER_HIS, " head by", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "had ", GENDER_HIS, " body pierced with a slug from", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	VICTIM, "had ", GENDER_HIS, " brains blown out by", KILLER = (MSG_DEATH, ST_DEATH_RAILGUN);
	//MOD_BFG:
	VICTIM, "saw the pretty lights from", KILLER, "'s BFG" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "was diced by the BFG from" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "was disintegrated by", KILLER, "'s BFG blast" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "was flatched with the green light by" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "couldn't hide from", KILLER, "'s BFG" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "tried to soak up green energy from", KILLER, "'s BFG" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "was energized with 50 cells by" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "doesn't know when to run from" = (MSG_DEATH, ST_DEATH_BFG);
	VICTIM, "'saw the light' from" = (MSG_DEATH, ST_DEATH_BFG);
	//MOD_HANDGRENADE:
	VICTIM, "caught", KILLER, "'s handgrenade" = (MSG_DEATH, ST_DEATH_GRENADES);
	VICTIM, "should watch more carefully for handgrenades from" = (MSG_DEATH, ST_DEATH_GRENADES);
	VICTIM, "didn't see", KILLER, "'s handgrenade" = (MSG_DEATH, ST_DEATH_GRENADES);
	VICTIM, "feels", KILLER, "'s pain" = (MSG_DEATH, ST_DEATH_GRENADES);
	//MOD_TELEFRAG:
	VICTIM, "tried to invade", KILLER, "'s personal space" = (MSG_DEATH, ST_DEATH_TELEFRAG);
	VICTIM, "is less telefrag aware than" = (MSG_DEATH, ST_DEATH_TELEFRAG);
	VICTIM, "should appreciate scotty more like" = (MSG_DEATH, ST_DEATH_TELEFRAG);
	//MOD_GRAPPLE:
	VICTIM, "was caught by", KILLER, "'s grapple" = (MSG_DEATH, ST_DEATH_GRAPPLE);
	//
#ifdef XATRIX
	//MOD_RIPPER
	VICTIM, "ripped to shreds by", KILLER, "'s ripper gun" = (MSG_DEATH, ST_DEATH_RIPPER);
	//MOD_PHALANX:
	VICTIM, "was evaporated by", KILLER = (MSG_DEATH, ST_DEATH_PHALANX);
	//MOD_TRAP:
	VICTIM, "caught in trap by", KILLER = (MSG_DEATH, ST_DEATH_TRAP);
#endif //XATRIX
#ifdef ROGUE
	//MOD_CHAINFIST:
	VICTIM, "was shredded by", KILLER, "'s ripsaw" = (MSG_DEATH, ST_DEATH_CHAINFIST);
	//MOD_DISINTEGRATOR:
	VICTIM, "lost his grip courtesy of", KILLER, "'s disintegrator" = (MSG_DEATH, ST_DEATH_DISRUPTOR);
	//MOD_ETF_RIFLE:
	VICTIM, "was perforated by", KILLER = (MSG_DEATH, ST_DEATH_ETFRIFLE);
	//MOD_HEATBEAM:
	VICTIM, "was scorched by", KILLER, "'s plasma beam" = (MSG_DEATH, ST_DEATH_HEATBEAM);
	//MOD_TESLA:
	VICTIM, "was enlightened by", KILLER, "'s tesla mine" = (MSG_DEATH, ST_DEATH_TESLA);
	//MOD_PROX:
	VICTIM, "got too close to", KILLER, "'s proximity mine" = (MSG_DEATH, ST_DEATH_PROX);
	//MOD_NUKE:
	VICTIM, "was nuked by", KILLER, "'s antimatter bomb" = (MSG_DEATH, ST_DEATH_NUKE);
	//MOD_VENGEANCE_SPHERE:
	VICTIM, "was purged by", KILLER, "'s vengeance sphere" = (MSG_DEATH, ST_DEATH_VENGEANCESPHERE);
	//MOD_DEFENDER_SPHERE:
	VICTIM, "had a blast with", KILLER, "'s defender sphere" = (MSG_DEATH, ST_DEATH_DEFENDER_SPHERE);
	//MOD_HUNTER_SPHERE:
	VICTIM, "was killed like a dog by", KILLER, "'s hunter sphere" = (MSG_DEATH, ST_DEATH_HUNTERSPHERE);
	//MOD_TRACKER:
	VICTIM, "was annihilated by", KILLER, "'s disruptor" = (MSG_DEATH, ST_DEATH_TRACKER);
	//MOD_DOPPLE_EXPLODE:
	VICTIM, "was blown up by", KILLER, "'s doppleganger" = (MSG_DEATH, ST_DEATH_DOPPLEGANGER);
	//MOD_DOPPLE_VENGEANCE:
	VICTIM, "was purged by", KILLER, "'s doppleganger" = (MSG_DEATH, ST_DEATH_DOPPLEGANGER);
	//MOD_DOPPLE_HUNTER:
	VICTIM, "was hunted down by", KILLER, "'s doppleganger" = (MSG_DEATH, ST_DEATH_DOPPLEGANGER);
	#endif //ROGUE
	//unknown death
	VICTIM, " died." = (MSG_DEATH, ST_DEATH_SUICIDE);
} //end MTCONTEXT_CLIENTOBITUARY

//entered the game message
MTCONTEXT_ENTERGAME
{
	//enter game message
	NETNAME, " entered the game" = (MSG_ENTERGAME, 0);
} //end MTCONTEXT_ENTERGAME

//initial team command chat messages
MTCONTEXT_INITIALTEAMCHAT
{
	//help someone (and meet at the rendezvous point)
	"(", NETNAME, "): help ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_HELP, ST_NEARITEM);
	"(", NETNAME, "): help ", TEAMMATE = (MSG_HELP, ST_SOMEWHERE);
	"(", NETNAME, "): ", ADDRESSEE, " help ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_HELP, $evalint(ST_NEARITEM|ST_ADDRESSED));
	"(", NETNAME, "): ", ADDRESSEE, " help ", TEAMMATE = (MSG_HELP, $evalint(ST_SOMEWHERE|ST_ADDRESSED));

	//accompany someone (and meet at the rendezvous point) ("hunk follow me", "hunk go with babe", etc.)
	"(", NETNAME, "): ", "accompany "|"go with "|"follow ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM, " for ", TIME = (MSG_ACCOMPANY, $evalint(ST_NEARITEM|ST_TIME));
	"(", NETNAME, "): ", "accompany "|"go with "|"follow ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_ACCOMPANY, ST_NEARITEM);
	"(", NETNAME, "): ", "accompany "|"go with "|"follow ", TEAMMATE, " for ", TIME = (MSG_ACCOMPANY, $evalint(ST_SOMEWHERE|ST_TIME));
	"(", NETNAME, "): ", "accompany "|"go with "|"follow ", TEAMMATE = (MSG_ACCOMPANY, ST_SOMEWHERE);
	"(", NETNAME, "): ", ADDRESSEE, " accompany "|" go with "|" follow ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM, " for ", TIME = (MSG_ACCOMPANY, $evalint(ST_NEARITEM|ST_ADDRESSED|ST_TIME));
	"(", NETNAME, "): ", ADDRESSEE, " accompany "|" go with "|" follow ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_ACCOMPANY, $evalint(ST_NEARITEM|ST_ADDRESSED));
	"(", NETNAME, "): ", ADDRESSEE, " accompany "|" go with "|" follow ", TEAMMATE, " for ", TIME = (MSG_ACCOMPANY, $evalint(ST_SOMEWHERE|ST_ADDRESSED|ST_TIME));
	"(", NETNAME, "): ", ADDRESSEE, " accompany "|" go with "|" follow ", TEAMMATE = (MSG_ACCOMPANY, $evalint(ST_SOMEWHERE|ST_ADDRESSED));

	//get the flag in CTF
	"(", NETNAME, "): get ", THE_ENEMY, "flag" = (MSG_GETFLAG, 0);
	"(", NETNAME, "): go get ", THE_ENEMY, "flag" = (MSG_GETFLAG, 0);
	"(", NETNAME, "): capture ", THE_ENEMY, "flag" = (MSG_GETFLAG, 0);
	"(", NETNAME, "): go capture ", THE_ENEMY, "flag" = (MSG_GETFLAG, 0);
	"(", NETNAME, "): ", ADDRESSEE, " get ", THE_ENEMY, "flag" = (MSG_GETFLAG, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " go get ", THE_ENEMY, "flag" = (MSG_GETFLAG, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " capture ", THE_ENEMY, "flag" = (MSG_GETFLAG, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " go capture ", THE_ENEMY, "flag" = (MSG_GETFLAG, ST_ADDRESSED);
	" near "|" at ", 
	//defend/guard a key area
	"(", NETNAME, "): ", "defend "|"guard ", "the "|"checkpoint "|"waypoint "|"", KEYAREA, " for ", TIME = (MSG_DEFENDKEYAREA, ST_TIME);
	"(", NETNAME, "): ", "defend "|"guard ", "the "|"checkpoint "|"waypoint "|"", KEYAREA = (MSG_DEFENDKEYAREA, 0);
	"(", NETNAME, "): ", ADDRESSEE, " defend "|" guard ", "the "|"checkpoint "|"waypoint "|"", KEYAREA, " for ", TIME = (MSG_DEFENDKEYAREA, $evalint(ST_ADDRESSED|ST_TIME));
	"(", NETNAME, "): ", ADDRESSEE, " defend "|" guard ", "the "|"checkpoint "|"waypoint "|"", KEYAREA = (MSG_DEFENDKEYAREA, ST_ADDRESSED);

	//camp somewhere ("hunk camp here", "hunk camp there", "hunk camp near the rl", etc.)
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "near "|"at ", "the "|"checkpoint "|"waypoint "|"", KEYAREA, " for ", TIME = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_NEARITEM|ST_TIME));
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "near "|"at ", "the "|"checkpoint "|"waypoint "|"", KEYAREA = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_NEARITEM));
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "there "|"over there ", " for ", TIME = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_TIME|ST_THERE));
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "there"|"over there" = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_THERE));
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "here "|"over here ", " for ", TIME = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_TIME|ST_HERE));
	"(", NETNAME, "): ", ADDRESSEE, " camp ", "here"|"over here" = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_HERE));
	//go to (same as camp)
	"(", NETNAME, "): ", ADDRESSEE, " go to ", "the "|"checkpoint "|"waypoint "|"", KEYAREA = (MSG_CAMP, $evalint(ST_ADDRESSED|ST_NEARITEM));


	//rush to the base in CTF
	"(", NETNAME, "): ", ADDRESSEE, "rush base" = (MSG_RUSHBASE, 0);
	"(", NETNAME, "): ", ADDRESSEE, "rush to base" = (MSG_RUSHBASE, 0);
	"(", NETNAME, "): ", ADDRESSEE, "rush to the base" = (MSG_RUSHBASE, 0);
	"(", NETNAME, "): ", ADDRESSEE, "go to base" = (MSG_RUSHBASE, 0);
	"(", NETNAME, "): ", ADDRESSEE, "go to the base" = (MSG_RUSHBASE, 0);

	//become the team leader
	"(", NETNAME, "): ", TEAMMATE, " will be ", THE_TEAM, "leader" = (MSG_STARTTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " want to be ", THE_TEAM, "leader" = (MSG_STARTTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " wants to be ", THE_TEAM, "leader" = (MSG_STARTTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " is the ", TEAM, "leader" = (MSG_STARTTEAMLEADERSHIP, 0);
	"(", NETNAME, "): I am ", "the leader"|"the team leader"|"team leader"|"leader" = (MSG_STARTTEAMLEADERSHIP, ST_I);

	//stop being the team leader
	"(", NETNAME, "): ", TEAMMATE, " is not ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " does not want to be ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " quits being ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, 0);
	"(", NETNAME, "): ", TEAMMATE, " stops being ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, 0);
	"(", NETNAME, "): I will not be ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, ST_I);
	"(", NETNAME, "): I do not want to be ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, ST_I);
	"(", NETNAME, "): I quit being ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, ST_I);
	"(", NETNAME, "): I stop being ", THE_TEAM, "leader" = (MSG_STOPTEAMLEADERSHIP, ST_I);

	//wait for someone
	"(", NETNAME, "): ", ADDRESSEE, " wait for ", TEAMMATE = (MSG_WAIT, $evalint(ST_SOMEWHERE|ST_ADDRESSED));
	"(", NETNAME, "): ", ADDRESSEE, " wait for me" = (MSG_WAIT, $evalint(ST_SOMEWHERE|ST_ADDRESSED|ST_I));
	"(", NETNAME, "): ", ADDRESSEE, " wait for ", TEAMMATE, " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_WAIT, $evalint(ST_NEARITEM|ST_ADDRESSED));
	"(", NETNAME, "): ", ADDRESSEE, " wait for me", " near "|" at ", "the "|"checkpoint "|"waypoint "|"", ITEM = (MSG_WAIT, $evalint(ST_NEARITEM|ST_ADDRESSED|ST_I));

	//ask what someone/everyone is doing
	"(", NETNAME, "): ", ADDRESSEE, " what are you doing?" = (MSG_WHATAREYOUDOING, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " what are you doing" = (MSG_WHATAREYOUDOING, ST_ADDRESSED);
	"(", NETNAME, "): what are you doing ", ADDRESSEE, "?" = (MSG_WHATAREYOUDOING, ST_ADDRESSED);
	"(", NETNAME, "): what are you doing ", ADDRESSEE = (MSG_WHATAREYOUDOING, ST_ADDRESSED);

	//join a sub team
	"(", NETNAME, "): ", ADDRESSEE, " create team ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " create squad ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " join team ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " join squad ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " we are team ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " we are squad ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " are in team ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " are in squad ", TEAMNAME = (MSG_JOINSUBTEAM, ST_ADDRESSED);

	//leave a sub team
	"(", NETNAME, "): ", ADDRESSEE, " leave your team" = (MSG_LEAVESUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " leave your squad" = (MSG_LEAVESUBTEAM, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " ungroup" = (MSG_LEAVESUBTEAM, ST_ADDRESSED);

	//dismiss
	"(", NETNAME, "): ", ADDRESSEE, " dismissed" = (MSG_DISMISS, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " dismiss" = (MSG_DISMISS, ST_ADDRESSED);

	//remember checkpoint
	"(", NETNAME, "): ", "checkpoint "|"waypoint ", NAME, " is at gps ", POSITION = (MSG_CHECKPOINT, 0);
	"(", NETNAME, "): ", "checkpoint "|"waypoint ", NAME, " is at ", POSITION = (MSG_CHECKPOINT, 0);
	"(", NETNAME, "): ", ADDRESSEE, " checkpoint "|" waypoint ", NAME, " is at gps ", POSITION = (MSG_CHECKPOINT, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " checkpoint "|" waypoint ", NAME, " is at ", POSITION = (MSG_CHECKPOINT, ST_ADDRESSED);

	//patrol
	"(", NETNAME, "): patrol from ", KEYAREA, " for ", TIME = (MSG_PATROL, ST_TIME);
	"(", NETNAME, "): patrol from ", KEYAREA = (MSG_PATROL, 0);
	"(", NETNAME, "): ", ADDRESSEE, " patrol from ", KEYAREA, " for ", TIME = (MSG_PATROL, $evalint(ST_ADDRESSED|ST_TIME));
	"(", NETNAME, "): ", ADDRESSEE, " patrol from ", KEYAREA = (MSG_PATROL, ST_ADDRESSED);

	//create new formation
	"(", NETNAME, "): ", ADDRESSEE, " create a new ", FORMATION, " formation" = (MSG_CREATENEWFORMATION, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " we are going to create a new ", FORMATION, " formation" = (MSG_CREATENEWFORMATION, ST_ADDRESSED);
	"(", NETNAME, "): ", ADDRESSEE, " we are going to create a new formation called ", FORMATION = (MSG_CREATENEWFORMATION, ST_ADDRESSED);

	//formation position
	"(", NETNAME, "): ", ADDRESSEE, " your formation position is ", POSITION, " relative to ", TEAMMATE = (MSG_FORMATIONPOSITION, ST_ADDRESSED);

	//form a known formation
	"(", NETNAME, "): ", ADDRESSEE, " form the ", FORMATION, " formation" = (MSG_DOFORMATION, ST_ADDRESSED);

	//the formation intervening space
	"(", NETNAME, "): ", ADDRESSEE, " the formation intervening space is ", NUMBER, " meter" = (MSG_FORMATIONSPACE, $evalint(ST_ADDRESSED|ST_METER));
	"(", NETNAME, "): ", ADDRESSEE, " the formation intervening space is ", NUMBER, " feet" = (MSG_FORMATIONSPACE, $evalint(ST_ADDRESSED|ST_FEET));
} //end MTCONTEXT_INITIALTEAMCHAT


MTCONTEXT_TIME
{
	TIME, " mins"|" minutes" = (MSG_MINUTES, 0);
	TIME, " secs"|" seconds" = (MSG_SECONDS, 0);
} //end MTCONTEXT_TIME

MTCONTEXT_PATROLKEYAREA
{
	"the "|"checkpoint "|"waypoint "|"", KEYAREA, " to ", MORE = (MSG_PATROLKEYAREA, ST_MORE);
	"the "|"checkpoint "|"waypoint "|"", KEYAREA, " and back", " to the start"|"" = (MSG_PATROLKEYAREA, ST_BACK);
	"the "|"checkpoint "|"waypoint "|"", KEYAREA, " and reverse" = (MSG_PATROLKEYAREA, ST_REVERSE);
	"the "|"checkpoint "|"waypoint "|"", KEYAREA = (MSG_PATROLKEYAREA, 0);
} //end MTCONTEXT_PATROL

MTCONTEXT_TEAMMATE
{
	"me"|"I" = (MSG_ME, 0);
} //end MTCONTEXT_TEAMMATE

MTCONTEXT_ADDRESSEE
{
	"everyone"|"everybody" = (MSG_EVERYONE, 0);
	TEAMMATE, " and "|", ", MORE = (MSG_MULTIPLENAMES, 0);
	TEAMMATE = (MSG_NAME, 0);
} //end MTCONTEXT_ADDRESSEE


