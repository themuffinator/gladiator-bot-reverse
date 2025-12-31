//===========================================================================
//
// Name:				chars.h
// Function:		bot characteristics
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-04-11
// Tab Size:		3 (real tabs)
//===========================================================================


//========================================================
//========================================================
//name
#define CHARACTERISTIC_NAME						0	//string
//alternative name
#define CHARACTERISTIC_ALT_NAME					1	//string
//bot ego
#define CHARACTERISTIC_EGO							2	//float [0, 1]
//gender of the bot
#define CHARACTERISTIC_GENDER						3	//string ("male", "female", "it")
//attack skill
#define CHARACTERISTIC_ATTACK_SKILL				4	//float [0, 1]
//weapon weight file
#define CHARACTERISTIC_WEAPONWEIGHTS			5	//string
//tendency to change weapon
#define CHARACTERISTIC_WEAPONSWITCHER			6	//float [0, 1]
//skill when aiming
#define CHARACTERISTIC_AIM_SKILL					7	//float [0, 1]
//accuracy when aiming
#define CHARACTERISTIC_AIM_ACCURACY				8	//float [0, 1]
//view angles (mouse) acceleration
#define CHARACTERISTIC_VIEW_ACCELERATION		9	//float [0.1, 1800]
//view angles (mouse) deacceleration
#define CHARACTERISTIC_VIEW_DEACCELERATION	10	//float [0.1, 1800]
//reaction time in seconds
#define CHARACTERISTIC_REACTIONTIME				11	//float [0, 5]
//========================================================
//chat
//========================================================
//file with chats
#define CHARACTERISTIC_CHAT_FILE					12	//string
//name of the chat character
#define CHARACTERISTIC_CHAT_NAME					13	//string
//characters per minute type speed
#define CHARACTERISTIC_CHAT_CPM					14	//integer [1, 4000]
//tendency to insult/praise
#define CHARACTERISTIC_CHAT_INSULT				15	//float [0, 1]
//tendency to chat misc
#define CHARACTERISTIC_CHAT_MISC					16	//float [0, 1]
//tendency to chat at start or end of level
#define CHARACTERISTIC_CHAT_STARTENDLEVEL		17	//float [0, 1]
//tendency to chat entering or exiting the game
#define CHARACTERISTIC_CHAT_ENTEREXITGAME		18	//float [0, 1]
//tendency to chat when killed someone
#define CHARACTERISTIC_CHAT_KILL					19	//float [0, 1]
//tendency to chat when died
#define CHARACTERISTIC_CHAT_DEATH				20	//float [0, 1]
//tendency to randomly chat
#define CHARACTERISTIC_CHAT_RANDOM				21	//float [0, 1]
//tendency to reply
#define CHARACTERISTIC_CHAT_REPLY				22	//float [0, 1]
//========================================================
//movement
//========================================================
//movement skill
#define CHARACTERISTIC_MOVESKILL					23	//float [0, 1]
//tendency to crouch
#define CHARACTERISTIC_CROUCHER					24	//float [0, 1]
//tendency to jump
#define CHARACTERISTIC_JUMPER						25	//float [0, 1]
//tendency to jump using a weapon
#define CHARACTERISTIC_WEAPONJUMPING			26	//float [0, 1]
//tendency to use the grapple hook when available
#define CHARACTERISTIC_GRAPPLE_USER				27	//float [0, 1]
//========================================================
//goal
//========================================================
//item weight file
#define CHARACTERISTIC_ITEMWEIGHTS				28	//string
//health gatherer
#define CHARACTERISTIC_HEALTHGATHERER			29	//float [0, 1] ***********
//armor gatherer
#define CHARACTERISTIC_ARMORGATHERER			30	//float [0, 1] ***********
//how much the bot is aware of someone's personal space
#define CHARACTERISTIC_TELEFRAGAWARE			31	//float [0, 1]
//the aggression of the bot
#define CHARACTERISTIC_AGGRESSION				32	//float [0, 1]
//how much fear the bot has
#define CHARACTERISTIC_FEAR						33	//float [0, 1]
//the self preservation of the bot
#define CHARACTERISTIC_SELFPRESERVATION		34	//float [0, 1]
//how likely the bot is to take revenge
#define CHARACTERISTIC_VENGEFULNESS				35	//float [0, 1]
//tendency to camp
#define CHARACTERISTIC_CAMPER						36	//float [0, 1]
//========================================================
//========================================================
//favourite class
#define CHARACTERISTIC_FAVCLASS					37	//integer [0, 100] ***********
//tendency to change name
#define CHARACTERISTIC_NAME_SWITCHER			38	//float [0, 1]
//tendency to change skin
#define CHARACTERISTIC_SKIN_SWITCHER			39	//float [0, 1]
//how much of a frustrated teen is the bot
#define CHARACTERISTIC_FRUSTRATEDTEEN			40	//float [0, 1] ***********
//how frustrated is the bot in general
#define CHARACTERISTIC_FRUSTRATEDNESS			41	//float [0, 1]
//tendency to get easy frags
#define CHARACTERISTIC_EASY_FRAGGER				42	//float [0, 1]
//how alert the bot is
#define CHARACTERISTIC_ALERTNESS					43	//float [0, 1]
//bot has sound on, off or 3D
#define CHARACTERISTIC_SOUNDTYPE					44	//integer [0, 2]
//resolution and 3D card user
#define CHARACTERISTIC_3DACCELERATOR			45	//integer [0, 1]
//type of control device used (keyboard, mouse, keypad, 3dassassin, joystick)
#define CHARACTERISTIC_CONTROLTYPE				46	//integer [0, 4]
//how much of a team spirit the bot has
#define CHARACTERISTIC_TEAMSPIRIT				47	//float [0, 1]
//does the bot client eat pizza while playing Q2
#define CHARACTERISTIC_PIZZAPREFERENCE			48	//float [0, 1]
//likelyhood that the client is called by his mom while playing quake
#define CHARACTERISTIC_CALL_PRONE				49	//float [0, 1]
//yep even bots need sleep every once in a while
#define CHARACTERISTIC_BEDDY_BYE_TIME			50	//float [0, 24]
//some bots might have glasses
#define CHARACTERISTIC_EYE_COR_DEVICE			51	//integer [0, 2]
//bot monitor gamme correction, 1 is pretty much fullbright
#define CHARACTERISTIC_GAMMA_CORRECTION		52	//float [0, 1]
//some bots really know how to do it
#define CHARACTERISTIC_BUTTKISSER				53	//float [0, 1]

