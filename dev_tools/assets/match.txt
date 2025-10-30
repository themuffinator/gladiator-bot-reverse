//===========================================================================
//
// Name:				match.h
// Function:		match template defines
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-05-12
// Tab Size:		3 (real tabs)
//
//===========================================================================

//match template contexts
#define MTCONTEXT_CLIENTOBITUARY		1
#define MTCONTEXT_ENTERGAME			2
#define MTCONTEXT_INITIALTEAMCHAT	4
#define MTCONTEXT_TIME					8
#define MTCONTEXT_TEAMMATE				16
#define MTCONTEXT_ADDRESSEE			32
#define MTCONTEXT_PATROLKEYAREA		64

//message types
#define MSG_DEATH							1		//death message
#define MSG_ENTERGAME					2		//enter game message
#define MSG_HELP							3		//help someone
#define MSG_ACCOMPANY					4		//accompany someone
#define MSG_DEFENDKEYAREA				5		//defend a key area
#define MSG_RUSHBASE						6		//everyone rush to base
#define MSG_GETFLAG						7		//get the enemy flag
#define MSG_STARTTEAMLEADERSHIP		8		//someone wants to become the team leader
#define MSG_STOPTEAMLEADERSHIP		9		//someone wants to stop being the team leader
#define MSG_WAIT							10		//wait for someone
#define MSG_WHATAREYOUDOING			11		//what are you doing?
#define MSG_JOINSUBTEAM					12		//join a sub-team
#define MSG_LEAVESUBTEAM				13		//leave a sub-team
#define MSG_CREATENEWFORMATION		14		//create a new formation
#define MSG_FORMATIONPOSITION			15		//tell someone his/her position in a formation
#define MSG_FORMATIONSPACE				16		//set the formation intervening space
#define MSG_DOFORMATION					17		//form a known formation
#define MSG_DISMISS						18		//dismiss commanded team mates
#define MSG_CAMP							19		//camp somewhere
#define MSG_CHECKPOINT					20		//remember a check point
#define MSG_PATROL						21		//patrol between certain keypoints
//
#define MSG_ME								100
#define MSG_EVERYONE						101
#define MSG_MULTIPLENAMES				102
#define MSG_NAME							103
#define MSG_PATROLKEYAREA				104
#define MSG_MINUTES						105
#define MSG_SECONDS						106

//death message sub types
#define ST_DEATH_SUICIDE				1
#define ST_DEATH_BLASTER				2
#define ST_DEATH_SHOTGUN				3
#define ST_DEATH_SUPERSHOTGUN			4
#define ST_DEATH_MACHINEGUN			5
#define ST_DEATH_CHAINGUN				6
#define ST_DEATH_GRENADES				7
#define ST_DEATH_GRENADELAUNCHER 	8
#define ST_DEATH_ROCKETLAUNCHER		9
#define ST_DEATH_HYPERBLASTER			10
#define ST_DEATH_RAILGUN				11
#define ST_DEATH_BFG						12
#define ST_DEATH_TELEFRAG				13
#define ST_DEATH_GRAPPLE				14
//
#define ST_DEATH_RIPPER					15
#define ST_DEATH_PHALANX				16
#define ST_DEATH_TRAP					17
//
#define ST_DEATH_CHAINFIST				18
#define ST_DEATH_DISRUPTOR				19
#define ST_DEATH_ETFRIFLE				20
#define ST_DEATH_HEATBEAM				21
#define ST_DEATH_TESLA					22
#define ST_DEATH_PROX					23
#define ST_DEATH_NUKE					24
#define ST_DEATH_VENGEANCESPHERE		25
#define ST_DEATH_DEFENDER_SPHERE		26
#define ST_DEATH_HUNTERSPHERE			27
#define ST_DEATH_TRACKER				28
#define ST_DEATH_DOPPLEGANGER			29

//command sub types
#define ST_SOMEWHERE						0
#define ST_NEARITEM						1
#define ST_ADDRESSED						2
#define ST_METER							4
#define ST_FEET							8
#define ST_TIME							16
#define ST_HERE							32
#define ST_THERE							64
#define ST_I								128
#define ST_MORE							256
#define ST_BACK							512
#define ST_REVERSE						1024

//word replacement variables
#define VICTIM								0
#define KILLER								1
#define GENDER_HE							4		// "he"						"she"					"it"
#define GENDER_HIS						4		// "his"						"her"					"it's"
#define GENDER_HIM						4		//	"him"						"her"					"it"
#define GENDER_GOD						4		// "god of hell-fire"	"goddess Pele"		"demigod"
//
#define THE_ENEMY							7
#define THE_TEAM							8
#define TEAM								9
//team message variables
#define NETNAME							0
#define ADDRESSEE							1
#define ITEM								2
#define TEAMMATE							3
#define TEAMNAME							3
#define KEYAREA							4
#define FORMATION							4
#define POSITION							4
#define NUMBER								4
#define TIME								5
#define NAME								5
#define MORE								5


