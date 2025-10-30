//===========================================================================
//
// Name:				defaul_c.c
// Function:		characteristics for default bot character
// Programmer:		MrElusive(mrelusive@demigod.demon.nl)
//						B.D. Squatt (squatt@demigod.demon.nl) 
// version:			19990517a
// Tab Size:		3 (real tabs)
//===========================================================================

//				********** Commentary by SQUATT, NOT BY MR.ELUSIVE *************
//bc is bot character

/*
	the following file contain the header files which define what each function
	mean (function being "CHARACTERISTIC_X"
*/
#include "chars.h"

/*
	The following skills usually need to have a value BETWEEN "0" and "1" (yes,
	you may also use..say "0.003"). For those of you who don't like programming or
	don't want to, see is as a percentile chance of doing something or not, with
	0 meaning never and 1 always or "as often as possible" (so putting a 1000000
	will be fruitless, as it will be scaled down to 1)
	Some "skills" require strings, but this is, I think, obvious.
*/

character "default"
{
	CHARACTERISTIC_NAME						"default"	//name
	CHARACTERISTIC_ALT_NAME					"default"	//alternative name (for boring people mode)
	//gender of the bot, i.e. male, female or something else; the program will
	//see if this says "male" or "female". It will make the bc an "it" if this
	//contains aything else
	CHARACTERISTIC_GENDER					"female"
	//reference to the file containing the bc's personalized chat lines
	CHARACTERISTIC_CHAT_FILE				"bots/defaul_t.c"
	//reference to the file containing the bc's personalized weapon weights
	CHARACTERISTIC_WEAPONWEIGHTS			"bots/defaul_w.c"
	//reference to the file containing the bc's personalized item weights
	CHARACTERISTIC_ITEMWEIGHTS				"bots/defaul_i.c"
	//
	//the time it takes for a bot to see an enemy ***AND*** react. The time is from 0
	//to whatever you enter in ***SECONDS***
	CHARACTERISTIC_REACTIONTIME			0
	CHARACTERISTIC_ATTACK_SKILL			0	//does the bot know rudimentary attacking
														//skills such as strafing, dodgeing, making coffe?
	CHARACTERISTIC_AIM_SKILL				0	//general weapons usage
	CHARACTERISTIC_AIM_ACCURACY			0	//mouse positioning on target
	CHARACTERISTIC_VIEW_ACCELERATION		0	//view angles (mouse) acceleration
	CHARACTERISTIC_VIEW_DEACCELERATION	0	//view angles (mouse) deacceleration
	CHARACTERISTIC_CROUCHER					0	//tendency to crouch in battle
	CHARACTERISTIC_JUMPER					0	//tendency to jump in battle
	//determines the bot's awareness and use of rocket-, grenade- and BFG-jumping.
	CHARACTERISTIC_WEAPONJUMPING			0
	//this is the bc chance to utilize the grappling hook (where available), with
	//a "1" being "as often as possible"
	CHARACTERISTIC_GRAPPLE_USER			0
	//
	//when a bc is killed by a client with a particular skin, it might get all
	//"frustrated" and start killing every client with the same skin it sees. The
	//higher the following value, the more likely it is that this will happen.
	CHARACTERISTIC_VENGEFULNESS			0
	CHARACTERISTIC_AGGRESSION				0	//how aggresively does the bot attack

	CHARACTERISTIC_FEAR						0
	//will determine the bc's suicidal tendencies at times.
	CHARACTERISTIC_SELFPRESERVATION		0
	//some bots really know how to do it
	CHARACTERISTIC_BUTTKISSER				0
	//how frustrated is the bot in general. Frustrated bc tend to be less carefull
	CHARACTERISTIC_FRUSTRATEDNESS			0
	//tendency to get easy frags
	CHARACTERISTIC_EASY_FRAGGER			0
	//how alert the bot i.e will it look around enough for enemies?
	CHARACTERISTIC_ALERTNESS				0
	//the bc's realization that certain spots are detrimental to it's health
	//because of spawning clients.
	CHARACTERISTIC_TELEFRAGAWARE			0
	//the bc's tendency to find a spot and ply his/her/it's outdoor hobby.
	CHARACTERISTIC_CAMPER					0
	//EGO of the bot. The higher the bc's EGO, the more likely it is to take
	//the leadership of a team, the less likely to take orders when in a team,
	//the shorter it will follow orders given by any team member.
	CHARACTERISTIC_EGO						0
	CHARACTERISTIC_TEAMSPIRIT				0
	//
	CHARACTERISTIC_WEAPONSWITCHER			0	//tendency to change weapon
	//some people (and bc's as well) do tend to switch skins at certain times.
	CHARACTERISTIC_SKIN_SWITCHER			0
	//tendency of the bc to randomly change it's name to something else.
	CHARACTERISTIC_NAME_SWITCHER			0
	//
//====================================================================================
	//reference to the bc's name that appears before a message it sends
	CHARACTERISTIC_CHAT_NAME				"default"
	//typing speed of the bc as in Characters Per Minute
	CHARACTERISTIC_CHAT_CPM					0
	//tendency the bot has to chat when entering or exiting a game
	CHARACTERISTIC_CHAT_ENTEREXITGAME	0
	//tendency the bot has to chat when it ends a level or starts a new one.
	CHARACTERISTIC_CHAT_STARTENDLEVEL	0
	//tendency the bot has to reply to messages of others
	CHARACTERISTIC_CHAT_REPLY				0
	//tendency the bot has to chat when it is killed
	CHARACTERISTIC_CHAT_DEATH				0
	//tendency the bot has to chat after it has killed another client
	CHARACTERISTIC_CHAT_KILL				0
	//tendency the bot has to chat using insults
	CHARACTERISTIC_CHAT_INSULT				0
	//tendency the bot has to type a miscellaneous message.
	CHARACTERISTIC_CHAT_MISC				0
	//tendency the bot has to randomly type/reply to a message
	CHARACTERISTIC_CHAT_RANDOM				0
//===================================================================================
	//does the bot client eat pizza while playing Q2
	CHARACTERISTIC_PIZZAPREFERENCE		0
	//likelyhood that the client is called by his mom while playing quake
	CHARACTERISTIC_CALL_PRONE				0
	//yep even bots need sleep every once in a while
	CHARACTERISTIC_BEDDY_BYE_TIME			0
	//some bots might have 3D glasses
	CHARACTERISTIC_EYE_COR_DEVICE			0
	//bc monitor gamme correction, 1 is pretty much fullbright
	CHARACTERISTIC_GAMMA_CORRECTION		0
	//resolution and 3D card user; a bc with a high value can "see" further
	CHARACTERISTIC_3DACCELERATOR			0
	//bot has sound on, off or 3D (0, 1, 2)
	CHARACTERISTIC_SOUNDTYPE				0
	//type of control device used (keyboard, mouse, keypad, 3dassassin, joystick)
	CHARACTERISTIC_CONTROLTYPE				0
} //end character default

