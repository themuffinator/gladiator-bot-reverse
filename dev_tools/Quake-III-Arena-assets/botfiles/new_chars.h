//===========================================================================
//
// Name:			chars.h
// Function:		bot characteristics
// Programmer:		Mr Elusive (MrElusive@idsoftware.com)
// Last update:		1999-09-08
// Tab Size:		4 (real tabs)
//===========================================================================


//========================================================
//========================================================
//name
#define CHARACTERISTIC_NAME						0	//string
//gender of the bot
#define CHARACTERISTIC_GENDER					3	//string ("male", "female", "it")
//attack skill
#define CHARACTERISTIC_ATTACK_SKILL				4	//float [0, 1]
//weapon weight file
#define CHARACTERISTIC_WEAPONWEIGHTS			5	//string
//skill when aiming
#define CHARACTERISTIC_AIM_SKILL				7	//float [0, 1]
//accuracy when aiming
#define CHARACTERISTIC_AIM_ACCURACY				8	//float [0, 1]
//view angle difference to angle change factor
#define CHARACTERISTIC_VIEW_FACTOR				9	//float <0, 1]
//maximum view angle change
#define CHARACTERISTIC_VIEW_MAXCHANGE			10	//float [1, 360]
//reaction time in seconds
#define CHARACTERISTIC_REACTIONTIME				11	//float [0, 5]
//========================================================
//chat
//========================================================
//file with chats
#define CHARACTERISTIC_CHAT_FILE				12	//string
//name of the chat character
#define CHARACTERISTIC_CHAT_NAME				13	//string
//characters per minute type speed
#define CHARACTERISTIC_CHAT_CPM					14	//integer [1, 4000]
//tendency to insult/praise
#define CHARACTERISTIC_CHAT_INSULT				15	//float [0, 1]
//tendency to chat misc
#define CHARACTERISTIC_CHAT_MISC				16	//float [0, 1]
//tendency to chat at start or end of level
#define CHARACTERISTIC_CHAT_STARTENDLEVEL		17	//float [0, 1]
//tendency to chat entering or exiting the game
#define CHARACTERISTIC_CHAT_ENTEREXITGAME		18	//float [0, 1]
//tendency to chat when killed someone
#define CHARACTERISTIC_CHAT_KILL				19	//float [0, 1]
//tendency to chat when died
#define CHARACTERISTIC_CHAT_DEATH				20	//float [0, 1]
//tendency to chat when enemy suicides
#define CHARACTERISTIC_CHAT_ENEMYSUICIDE		21	//float [0, 1]
//tendency to chat when hit while talking
#define CHARACTERISTIC_CHAT_HITTALKING			22	//float [0, 1]
//tendency to chat when bot was hit but didn't dye
#define CHARACTERISTIC_CHAT_HITNODEATH			23	//float [0, 1]
//tendency to chat when bot hit the enemy but enemy didn't dye
#define CHARACTERISTIC_CHAT_HITNOKILL			24	//float [0, 1]
//tendency to randomly chat
#define CHARACTERISTIC_CHAT_RANDOM				21	//float [0, 1]
//tendency to reply
#define CHARACTERISTIC_CHAT_REPLY				22	//float [0, 1]
//========================================================
//movement
//========================================================
//tendency to crouch
#define CHARACTERISTIC_CROUCHER					24	//float [0, 1]
//tendency to jump
#define CHARACTERISTIC_JUMPER					25	//float [0, 1]
//tendency to jump using a weapon
#define CHARACTERISTIC_WEAPONJUMPING			26	//float [0, 1]
//tendency to use the grapple hook when available
#define CHARACTERISTIC_GRAPPLE_USER				27	//float [0, 1]	//use this!!
//========================================================
//goal
//========================================================
//item weight file
#define CHARACTERISTIC_ITEMWEIGHTS				28	//string
//the aggression of the bot
#define CHARACTERISTIC_AGGRESSION				32	//float [0, 1]
//the self preservation of the bot (rockets near walls etc.)
#define CHARACTERISTIC_SELFPRESERVATION			34	//float [0, 1]
//how likely the bot is to take revenge
#define CHARACTERISTIC_VENGEFULNESS				35	//float [0, 1]	//use this!!
//tendency to camp
#define CHARACTERISTIC_CAMPER					36	//float [0, 1]	//use this!!
//========================================================
//========================================================
//tendency to get easy frags
#define CHARACTERISTIC_EASY_FRAGGER				42	//float [0, 1]	//use this!!
//how alert the bot is (view distance)
#define CHARACTERISTIC_ALERTNESS				43	//float [0, 1]

