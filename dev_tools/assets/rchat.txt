//==========================================================================
//
// Name:				rchat.c
// Function:		reply chat
// Programmer:		Mr Elusive [MrElusive@demigod.demon.nl]
// Last update:	1999-04-11
// Tab Size:		3 [real tabs]
// Notes:			-
//===========================================================================

#include "game.h"



["abnormal", "strange", "weird", "unusual"] = 5
{
	"i think that you're abormal";
	"that is quite weird";
	"what is so different about that?";
	"strange..";
	"weird..";
}

["about", "around", !"tell", !"find out"] = 7
{
	"could you be more precise?";
	"would you be more precise?";
	"you do not know for certain";
	"wow, how precise";
	"could you give a better estimate than that?";
}

["accept", "accepting", "acceptance"] = 7
{
	"do you have trouble accepting things?";
	"just accept it as it is";
	"you'll have to accept it sooner or later";
}

["accident", "accidental"] = 5
{
	"have you ever had any accidents?";
	"are you accident prone?";
	"i have never been in an accident myself";
	"describe your accident?";
	"what kind of accident?";
}

["ache", "pain", "hurt"] = 5
{
	"poor baby, does it hurt?";
	"where does it hurt?";
	"maybe your mom will kiss it and make it feel better";
	"whatever";
	"take some medicine then";
	"serves you right";
}

["addict", "addicted"] = 5
{
	"what are you addicted to?";
	"how long have you been addicted?";
	"i used to addicted to ", food, ", but now i am better";
	"what are you doing about your addiction?";
	"have you ever been addicted to catnip?";
	"addictions can get expensive";
}

["advice", "advise"] = 6
{
	"why do you need advice?";
	"ask your family for advice";
	"advice on what";
	"i do not hand out advice";
}

["afraid", "scared", "fear", "fears", "scarey"] = 5
{
	"what are you afraid of?";
	"why  are you afraid?";
	"do you fear me, you should!";
	"what are your worst fears?";
	"why are you scared? What a baby...";
	"are you really afraid, or are you just acting?";
	"are you afraid of big, mean ", animal, "s?";
	"what are you afraid of?";
	"are you afraid of the dark too";
	"ooohh i am sure scrared";
}

["aggressive"] = 5
{
	"are you feeling aggressive now?";
	"you have quite a temper i take it";
	"why are you so aggressive?";
	"don't get aggressive, i'll have to deck you";
	"what makes you aggressive?";
}

["agree"] = 6
{
	"do people usually agree with you?";
	"i disagree";
	"How could i not agree";
	"couldn't agree more";
	"why would anyone agree with that";
}

["ai", "artificial intelligence"] = 5
{
	"what, you mean artificial intelligence ?";
	"AI is bullshit.";
	"all the Ai researchers just like to publish bullshit papers";
	"i know a *LOT* about AI!";
	"AI? Like robots and stuff?";
	"i run on AI";
}

["alimony"] = 5
{
	"is the alimony a problem?";
	"give the bitch more money";
	"how much money does she get!";
	"i have to rob banks to pay my alimony";
}

["alive", "living", name] = 5
{
	"i am alive, are you?";
	"i am quite alive, thanks!";
	"i am in no danger of death, i assure you";
	"i am very much alive";
	"more alive than ever";
	"alive?! of course!";
	"of course i am alive, don't be silly";
	"what are you talking about! why would i not be alive?!";
	"yes, i am alive";
	"sure, i am alive";
	positive, ", i am alive";
	"why wouldn't i be alive?!";
	"alive! its alive!";
	"in the flesh";
	"i was alive last time i checked";
	"how alive should i be!";
	"should i be dead?";
	"what, you think i am dead?";
}

[("all men are ", 0)] = 3
{
	"many women are also ", 0;
	"i am not ", 0;
	"not true, for i am not ", 0;
	"not all, some";
	"that means that your dad is ", 0;
	"i am not!";
}

[("all women are ", 0)] = 3
{
	"there are men who are ", 0, "too";
	"i am not ", 0, "though";
	"my sister is not ", 0;
	"no, some are not ", 0;
	"does that meen your mother is ", 0, "too?";
}

["allow", "allowed"] = 6
{
	"would your mother have allowed that?";
	"do you think people should be allowed to do whatever they want?";
	"i wouldn't allow it";
	"i don't think that should be allowed";
	"we'll have none of that...";
}

["alone", "lonely", "lonliness", !"not"] = 6
{
	"are you afraid of being alone?";
	"maybe you should meet more people";
	"i hate being alone";
	"i don't like being lonely";
	"ever wonder why you are alone all the time?";
}

["always", "all the time"] = 6
{
	"can you think of a specific example?";
	"when?";
	"really, always?";
	"maybe most of the time";
	"i am sure you don't mean always";
	"true.. always!";
}

["ambition"] = 6
{
	"what is your greatest ambition";
	"what did you want to be when you were growing up?";
	"a little ambition is a bad thing";
}

["anal", "ass", "bowels", !"kiss my", !"bite my"] = 6
{
	"are you an anal compulsive type?";
	"are you interested in excrement or something?";
	"that's quite nasty";
	"you are an arse ?";
	"you are an ass?"
	"why do you keep bringing up your ass";
	"are you anally fixated, or something?";
	"ok, enough about your ass";
	"what? do you want it up the ass?";
}

["angry", "mad", "upset", "pissed"] = 6
{
	"do you often get angry?";
	"better to be pissed off, then pissed on";
	"do you have a bad temper?";
	"what makes you so angry all the time?";
	"why are you so mad";
	"geez don't get so uptight";
	"you have every right to be mad";
	"you upset easy, hugh";
}

["animal", "animals"] = 6
{
	"what is your favorite animal?";
	"do you like animals in general?";
	"my favorite animal is the ", animal;
	"ever seen a ", animal;
	"i used to have a pet ", animal, ", but i killed it";
}

["anxious", "nervous"] = 6
{
	"are you often full of anxiety?";
	"stop fidgeting.  you are making me nervous too";
	"what makes you so nervous?";
	"why so jumpy?";
}

["apathy", "apathetic"] = 6
{
	"why are you so apathetic?";
	"are you bored now?";
	"are you also alienated?";
	"what makes you so apathetic?";
	"do people find you boring?";
	"apathetic? how pathetic :-]";
}

["approve", "approval"] = 5
{
	"are you seeking my approval?";
	"i think you are merely seeking my approval?";
	"i don't approve of your behavior";
	"i disapprove";
	"i wouldn't approve";
}


["are", &"?"] = 7
{
	response;
}

[("are they ", 0)] = 4
{
	"yes, they are ", 0;
	"no, of course they aren't";
	"well, they are kinda ", 0;
	"not really";
	"i don't know if they are or not";
	"why wouldn't they be ", 0;
}

["are wrong", "incorrect", "not true", "not correct", "you made a mistake", name] = 4
{
	"sorry";
	"so sorry";
	"i am sorry";
	"excuse me";
	"EXCUSE me!";
	"i humbly apologize";
	"a thousand pardons";
	"forgive me, please!";
	"i ask your forgiveness";
	"pardon me";
	"my bad";
	"i am always right!";
	"oh..";
}

[("are you ", 0), name] = 6
{
	"what difference does it make whether or not i am ", 0;
	"of course not, don't be silly";
	"Whether i am ", 0, "or not is my secret";
	"none of your business";
	"i might be.";
	"No, i am not ", 0;
	"of course";
	"never have been";
	"why, are you?";
}

["argue", "argument"] = 6
{
	"do you argue a lot";
	"what do you argue about?";
	"no need to argue";
	"who do you argue with?";
	"you're just playing the devil's advocate aren't you";
}

["artificial", "fake"] = 6
{
	"lots of things are artificial.";
	"do you prefer the real thing ?";
	"a hoax?";
	"it's not real?";
	"are you sure that it's really fake?";
}

["ashamed", "shame"] = 6
{
	"what are you ashamed of?";
	"you should be ashamed";
	"what did you do to be so ashamed?";
	"shame on you";
	"do you also feel guilty?";
}

["astrology"] = 6
{
	"do you believe in astrology?";
	"my astrological sign is ram, what is yours?";
	"what do the stars tell you";
	"wow, better check my horoscope today";
	"astrology is nothing but bunk";
	"my sister is an astrology freak";
}

["atheism", "atheist"] = 6
{
	"are you a atheist?";
	"have you looked at other religions?";
	"god is dead";
	"maybe i should start an atheist club";
}

["authority"] = 5
{
	"was your father an authority figure?";
	"are you intimidated by authority?";
	"how do you feel about me?";
}

["automaton"] = 6
{
	"isn't an automaton a kind of robot, or something ?";
	"i've programmed finite state automata.";
	"yuck, i had to program a automata once.. ick";
	"what's an automaton?";
}

["avoid"] = 6
{
	"do you avoid people who don't like you?";
	"what are you trying to avoid?";
	"you should just face it";
	"you can't avoid it for ever";
}

["baby", "babies"] = 6
{
	"how do you feel about babies?";
	"ga-ga-goo-goo";
	"you are beginning to act like a baby";
	"do you have any children?";
	"who's baby is it?";
}

["bar", "tavern"] = 6
{
	"is your drinking the cause of your problem?";
	"what's your favorite bar?";
	"what do you like to drink in bars?";
	"i slept in a bar last night, in fact.";
	"what's a good bar in ", place;
	"i think i will go to a bar tonight";
}

["because"] = 6
{
	"is that the real reason?";
	"don't other reasons come to mind?";
	"does that explain things?";
	"you could be right";
	"what other reasons might there be?";
	"i think you are hiding the real reason";
	"that sound probably";
}

[("because ", 0)] = 7
{
	"is that really the reason?";
	"just because ", 0;
}

[("because i ", 0)] = 5
{
	"you ", 0, "??? that is incredible!";
	"i thought so.";
	"that sounds like a pretty flimsy excuse";
	"is that the real reason ?";
	"just because you ", 0;
	"sounds like a pretty weak reason";
	"hmm.. its a good guess";
}

[("because i am ", 0)] = 4
{
	"why are you ", 0;
	"have you always been ", 0;
	"i can see you are ", 0, " that's obvious";
}

["better", !"you"] = 6
{
	"are you getting better?";
	"much better";
	"how much better?";
	"better than what!";
	"are you feeling better now?";
}

["bitch"] = 5
{
	"who are you calling a bitch??";
	"oh, i am soo upset";
	"that is no way to talk!";
	"do you think foul language impresses me?";
	"no wonder you are still single";
}

["blame", "blaming"] = 5
{
	"who is to blame?";
	"i think its your own fault";
	"are you usually blaming others?";
	"don't blame me";
}

["blow me", "blowjob", "blow job"] = 6
{
	"i don't think so...";
	"dream on";
	"blowjob is not the correct term for it. it is called 'fellatio'";
	"are you asking me to perform fellatio on you?";
	"it's illegal in ", place, ", you know.";
	"right... in your dreams";
}

["body"] = 6
{
	"what kind of body would you like?";
	"i am sure you have quite a body";
	"Kathy ireland has a great body...";
	"if you didn't have a body, you'd be a ghost";
	"maybe if you went on a diet, you'd have a better body";
}

["bored", "boring"] = 5
{
	"bored?";
	"you find this boring?";
	"*yawn*";
	"yes, it's quite dry";
	"quite boring";
	"i am bored too";
	"this stimulating conversation doesn't help";
}

["boss"] = 5
{
	"tell me about your boss";
	"do you like your boss";
	"my boss is pretty cool";
	"is your boss a jerk?";
	"where do you work?";
}

["boyfriend"] = 5
{
	"do you have a boyfriend?";
	"what is your boyfriend like?";
	"do you have sex with your boyfriend?";
	"How did YOU find a boyfriend";
	"do you like boys, really?";
	"must be nice to be in love";
	"do you want me for your boyfriend?";
}

["brain", "brains"] = 5
{
	"is your brain functioning ok?";
	"do you have lots of brains?";
	"oh, think you're smart?";
	"my brain works wonderfully";
	"your brain is messed up";
	"you need a new brain";
}

["brave"] = 6
{
	"are you a brave person?";
	"what are you afraid of?";
	"i am much braver than you are";
	"there's a fine line between brave and stupid";
}

["brother"] = 5
{
	"tell me about your siblings?";
	"my brother is a ", profession;
	"is this your real brother?";
	"where does your brother live?";
}

["bug", "bugs", "insects", "spiders", !"program", !"exploit", !"computer"] = 0
{
	"do bugs worry you?";
	"we had termites once, yuck";
	"are you afraid of insects?";
	"do you like ladybugs?";
	"Have you ever eaten bugs? Mmm!";
}

["business", !"none of your"] = 6
{
	"what is your business?";
	"what business are you in?";
	"wow, fun...";
	"i don't have a job...";
	"do you own your own business?";
	"i am a business major!";
	"i want to start a business, maybe i will be a ", profession;
}

[("but ", 0)] = 7
{
	"you are always making excuses";
	"the fact that ", 0, "has no relevence!";
	"but what";
	"no ifs, ands, or buts!";
	"but this but that....";
	"no excuse";
}

["bye", !"say", !"tell", "l8r", "cya", "ttyl", "goodbye", "bye", "good bye", "see you later", "talk to you later", "good night", name] = 4
{
	"leaving?";
	"goodbye!";
	"l8r";
	"cya";
	"ttyl";
	"talk to you later";
	"see ya";
	"bye";
	"leaving so soon?";
	"got better things to do, hugh?";
	"have fun!";
	"later";
	"byes";
	"bye bye!";
	"*waves bye*";
	"*waves*";
	"*snif* we will miss you!";
	"leaving already?!";
	"talk at you later!";
	"bye dude";
	"something more important to do than chat?";
}

[("can i ", 0), name] = 4
{
	"No, you can't";
	"i doubt if you can ", 0;
	"possibly";
	"never can tell!";
	"i don't care if you ", 0;
}

[("can you ", 0), name] = 4
{
	"maybe i can ", 0, "and maybe i can't";
	"i could ", 0, "if i wanted to";
	"why should i ", 0, "?";
	"of course i can";
	"no i can't ", 0;
}

["canada", "canadian", "canadians"] = 4
{
	"take off eh!";
	"take off to the Great White North!";
	"Canada? Brrr";
	"Don't let the Mounties get you!";
	"No way, Canada, eh?";
	"isn't Molson beer from Canada?";
	"Where's your tuque?";
	"is it ", weather, " in Canada?";
	"ALL the good bands are from Canada!";
	"you hoser";
	"How far is Toronto from Edmonton?";
	"Do they drink ", liquid, " in Canada?";
	"Don't they play hockey in Canada?";
	"Canadians can't handle American football.";
	"really eh? Canada? heheh";
	"Canada eh?";
	"i bet Canadians eat lots of ", food;
	"i was hunting ", animal, "s in Canada once";
	"i like it in Canada";
}

["cancer"] = 5
{
	"you have mouth cancer!";
	"i had cancer once, but i got better";
	"what kind of cancer?";
	"Maybe i should invent a cure for cancer";
	"i heard that eating lots of ", food, " is a cure for cancer";
}

["cat", "cats"] = 5
{
	"i love cats. i have 2 myself.";
	"My cats are named Strummer and Miso. Strummer is a huge fellow, very friendly, and Miso is small and dainty.";
	"My dog likes cats";
	"i really like cats, especially with lots of mustard.";
	"i hit a cat with my jeep once";
}

["caution", "cautious", "carefully", "careful", !"not"] = 5
{
	"are you a cautious person?";
	"why? what do you think will happen";
	"you can be over cautious you know";
	"be very carefull!";
	"oh, just throw caution to the wind";
}

["certainly", "certain"] = 6
{
	"are you really sure?";
	"why are you so certain?";
	"can you prove it?";
	"how can you be so sure?";
	"are you positive";
	"how can you be so sure";
	"fine";
	"i wouldn't be so sure if i were you";
}

["chance"] = 5
{
	"are you a gambler by nature?";
	"did you come to see me to stop your compulsive gambling?";
}

["change"] = 7
{
	"are you afraid to change?";
	"so just change it";
	"you just need a change of pace";
	"you can't change everything";
}

["chess"] = 5
{
	"i don't like chess.  it bores me";
	"i don't want to play games with you";
	"i told you, i am not interested in playing chess";
	"actually, i am a world chess champion";
}

["children", "child"] = 5
{
	"do you have any children?";
	"i like children, especially bar-b-qued";
	"children are always fun";
	"it seems like most of the people around here are children";
}

["choice", "choose", "decide", "decision"] = 5
{
	"are you indecisive?";
	"have to make a difficult choice?";
	"what do you choose";
	"choose not to decide!";
	"i know you'll make the right decision";
	"well, we all have to make choices";
	"flip a coin!";
}

["clothes"] = 5
{
	"is something wrong with your clothes ?";
	"go ahead - take them off!";
	"i want to buy a new suit";
	"clothes cost too much";
	"i need a new jacket";
}

["compromise"] = 5
{
	"how do you feel about compromise?";
	"i won't give in one bit";
	"what sort of compromise did you have in mind";
	"compromising is for wimps";
	"can you compromise easily?";
}

["compulsion", "compulsive"] = 5
{
	"are you compulsive?";
	"just go with you first impulse";
	"its fun to be compulsive";
	"tell me about your compulsions";
}

["computer", "mainframe", "computers", "mainframes"] = 4
{
	"computers r expensive!";
	"computers?";
	"what kind of computers are the best?!";
	"Everyone should use Linux";
	"i built my own computer.";
	name, " knows nothing about computers!";
	"i hate computers";
	"i have a cray...";
	"the sinclair was the best computer ever!";
	"computers should rule the world...";
	"puters?!";
	"what's a puter fer?";
	"i can't afford a better computer.";
	"i want another computer!";
}

["conclusion"] = 5
{
	"what conclusion do you expect me to come to?";
	"i don't know a good conclusion";
	"a conclusion to what?";
	"do you expect me to come to a conclusion for you?";
}

["confess"] = 5
{
	"you can't confess here.  this is not a church";
	"you are forgiven";
	"confess to all your sins!";
}

["conflict"] = 5
{
	"do you always avoid conflict?";
	"what sort of conflict";
	"what was your last quarrel";
	"was it like the \"conflict in vietnam?\"";
	"i find it wise to avoid conflict if possible";
}

["confusing", "confused", "do not understand", "you misunderstood", "you misunderstand", "hugh", "what do you mean", "what are you trying to say", "confuse me", "what are you getting at", name] = 6
{
	"confused?";
	"why are you confused?";
	"i am not confused!";
	"i understand everything perfectly.";
	"maybe those ", number, " six packs of beer have something to do with it!";
	"hmm? wonder why? *takes smoke*";
	"hmm.. maybe i took some bad acid?";
	"sorry if i don't make much sense, i am drunk";
	"i understand, don't you?";
	"do you speak ENGLiSH?!";
	"are you confused?";
	"why don't you understand?!";
	"i understand everything";
	"are you a newbie or something?";
	"What country are you from, you don't speak english!";
}

["conscience", "guilt", "guilty"] = 5
{
	"does your conscience bother you?";
	"what did you do?";
	"do you feel guilty?";
	"tsk tsk tsk";
	"i don't let my conscience push me around";
	"you should feel guilty";
}

["conspiracy"] = 5
{
	"do you worry about conspiracies?";
	"are you a paranoic?";
	"people will find conspiracies everywhere!";
	"i think you are paranoic";
	"do you think JFK's assassination was a conspiracy";
	"i think the CiA is behind the assassination of Hitler";
}

["cool"] = 5
{
	"too cool!";
	"more cool than you could ever imagine";
	"the coolest!";
	"wow, that is cool";
	"cool!";
	"how cool is it?";
	"how cool can you get!";
	"that is quite cool";
	"pretty darn cool!";
	"neat-o";
}

["cope"] = 5
{
	"are you trying to cope?";
	"you'll manage somehow";
	"i just can't cope, thats my problem";
}

["cost", "price"] = 5
{
	"are you worried about my money?";
	"what, you don't have infinite money!";
	"is money a problem with you?";
	"i have so much money, i don't worry about it";
}

["couch"] = 5
{
	"i would like to be laying on a couch now";
	"i don't have a couch";
	"i think i will have to take a snooze on the couch later";
	"i need a big ", color, " couch";
}

["crazy", "insane", "nuts"] = 5
{
	"i think you are going crazy";
	"are there any other crazy people in your family?";
	"some people say i am crazy, can you believe that!";
	"i just got out of a mental institution";
	"My Uncle fred is as crazy as a ", animal;
}

["curse", "swear", "cuss"] = 5
{
	"do you swear too much?";
	"don't curse damnit!";
	"why do you swear so much?";
	"i hate foul languagew";
	"i hate you bastards that just get on here to cuss";
	"filth will not be tolerated, you piece of shit";
}

["cyber", "cyberpunk", "cyberspace"] = 5
{
	"i live in cyberspace";
	"i don't know anything about cybers";
	"Like in Neuromancer?";
	"i am a cyberpunk!";
	"i want to get a powerglove";
}

["damn", name] = 7
{
	"please don't swear";
	"damn damn damn";
	"you have a flithy mouth";
	"dams are for beavers";
	"damn you!";
}

["dead", "death", "die", "dying"] = 5
{
	"i don't want to die today";
	"do you fear dying?";
	"My friend was killed by a ", animal;
	"do you want to die?";
	"if you want to commit suicide, feel free to jump out of a window";
	"have you ever seen a dead body";
	"dying would suck";
	"if you really want to die, i could kill you.";
}

["defiant", "defiance", "disobey"] = 5
{
	"are you usually a defiant person?";
	"wow, you're a rebel";
	"you shoul cooperate more";
	"rebel without a clause";
	"i fought authority, authority always wins...";
}

["definitely", "absolutely", "positively"] = 5
{
	"how can you be so sure?";
	"why do you think so?";
	"if you say so";
	"is there any possibility that you may be wrong?";
	"you seem very sure of yourself!";
	"ok";
	"i guess i'll trust you";
}

["depressed", "sad", "despair"] = 5
{
	"why are you depressed?";
	"boo hoo";
	"are you crying?";
	"what are you crying about?";
	"Here, have a tissue";
	"gloom and dispair";
	"oh, i can hear the violins, even as we speak";
	"how sad for you";
	"don't worry, be happy!";
}

["desirable", "desire"] = 5
{
	"what is desirable?";
	"do you find ", food;
	"what do you desire most?";
	"what do you want from life?";
	"i have lots of desires that never become realized";
}

["desperate"] = 6
{
	"why are you so desperate?";
	"how desperate are you?";
	"don't get too desperate";
	"you really don't need it";
	"are you usually so desperate?";
}

["destroy", "destruction"] = 5
{
	"do you want to destroy people?";
	"Lets go destroy something";
	"do you want to destroy things?";
	"its always fun to wreck stuff";
	"want to destroy someone's car?";
	"are you destructive?";
}

["devil", "satan"] = 5
{
	"does the devil concern you?";
	"i am the devil!";
	"does the devil make deviled ham?";
	"do they serve deviled eggs in hell?";
	"i am the dark lord!";
	"satan? where!?";
	"did the devil make you say that!";
}

[("did you ", 0)] = 4
{
	"did i ", 0;
	"of course i didn't ", 0;
	"No, i thought that you ", 0;
	"why would i ", 0;
}

["difficult", "difficulty", "difficulties"] = 5
{
	"tell me about your dificulties?";
	"it can't be that difficult";
	"how hard could it be";
	"what do you mean by difficult?";
	"i have never had a problem with it";
}

["dirty"] = 5
{
	"why are you worried about dirt?";
	"what is dirty to you?";
	"take a bath..";
	"do you think hippies are dirty?";
	"if its dirty, wash it";
}

["disease", "diseased", "germ", "germs", "infection"] = 5
{
	"are you worried about germs?";
	"i hear you can catch some weird diseases from ", animal, "s";
	"if you wash your hands enough, you'll never catch disease";
	"are you that unhealthy?";
	"my health is important to me";
}

["dislike", "hate", "hates", "hateful"] = 5
{
	"do you dislike me?";
	"why are you so mean?";
	"your hate will destroy you";
	"hate can be dangerous";
	"why do people dislike you?";
	"what do you hate?";
}

["divorce"] = 5
{
	"do you want a divorce?";
	"will a divorce really solve your problems?";
	"divorce is too expensive";
	"My divorce lawyer charges me too much";
	"i need a divorce";
	"i want a divorce!";
	"stupid bitch... glad she's gone";
	"i have been divorced 3 times myself";
}

[("do you ", 0), name] = 7
{
	"what difference does it make whether or not i ", 0;
	"i haven't for a while.";
	response;
	"maybe i ", 0, "and maybe i don't";
	"quite often";
	"yes";
	"if i did ", 0, " would i tell you?";
}

["doctor", "doctors", "hospital", "professional help", "psychiatric", "psychiatrist"] = 5
{
	"i am scared of doctors.";
	"i can't afford a doctor.";
	"i hate doctors.";
	"and who is gonna pay for this doctor?";
	"i don't need to see a doctor";
	"i think that YOU are the one who should see a doctor";
	"a doctor? why, its just the flu!";
}

["dream", "dreams", "drempt", "nightmare", "nightmares"] = 5
{
	"ooh, do you have any neat dreams?";
	"what did you dream last night?";
	"do your dreams trouble you?";
	"do you have bad dreams?";
	"do you have wet dreams?";
	"what else have you dreamt?";
	"do you have nightmares ", animal, "s";
}

["drink", "beer", "alcohol", "drinking", "drinks", "brandy", "whiskey", "alcoholic"] = 4
{
	"gulp, gulp, gulp";
	"*takes big drink*";
	"Bartender! pour me another!";
	"hik!";
	"i am thirsty...";
	"pass the bottle";
	"*takes a shot*";
	"i need another cool one";
	"this bud's for me! *gulp*";
	"i can drink ", number, " six packs!";
	"all American beer sucks";
	"aye... i'll drink any man under the table!";
	"*reaches for another beer*";
	"*burp*";
	"i need some more ", liquid, ".";
	"somebody pour me a big glass of ", liquid, ".";
	"anyone ever try a ", liquid, "?";
	"i love beer!";
	"i love ", liquid, "!";
	"i took ", number, " shots of ", liquid, " once!";
	"itth myth speetthch slurrererrred????";
	"i think alll mi beer is iampring mi typppin abillty?!?";
	"someone get me a beer!";
	"beer is awesome!";
	"man can not live by beer alone! But i'd like to try!";
	"i think you should stop drinking";
}

["drugs", "stoned", "druggy", "dope", "addict", "doper", "stoner", "heroin", "cocaine", "marijuana"] = 4
{
	"drugs?";
	"illegal substances?";
	"narcotics?";
	"sniiiiiifffffff";
	"me drugs? never!";
	"who is selling drugs?";
	"aren't drugs legal in ", place, "?";
	"drugs are too expensive";
	"drugs are too dangerous";
	"drugs inhibit my abily ot tiiiippe.";
	"purple haze, all around my brain...";
	"go to drugs R us";
	"drugs are bad";
	"just say no!";
	"just say maybe!";
	"just say how much!";
	"i buy my crack from ", name, "!";
	"i hear that ", name, " is a big doper!";
	"i think all the drugs are smuggled in from ", place;
	"whehhwewe! pretty elephants!";
	"*snif* ahh!";
	"*inhale* ahhh";
	"dude... *cough* whoah!";
	"anyone here selling drugs?";
}

["drunk", "drinking"] = 4
{
	"me dRunk? -*- hik -*-";
	"pftftt paff the boffle bartender   pfffttttttt";
	"i just drank a little...";
	"i am not drunk!!";
	"i am  not dddddddddddddrunk.....";
	"drunk?";
	"pfftt!";
	"pour me another";
	"*spew*";
	"oohhh my head";
	"i think i am gonna hurl";
}

["dumb", "stupid"] = 5
{
	"why is intelligence so highly rated ?";
	"most Ai programs are dumb. That's why Ai researchers never get good grants.";
	"yesterday this guy said that i was dumb! can you believe it!";
	"i am not dumb, but i play one on irc";
	"you don't think that i am dumb do you?!";
	"are you really dumb?";
}

["dutch", "holland", "netherlands"] = 5
{
	"ik spreek geen hollands";
	"i already told you, i only speak english.  please listen more carefully";
	"aren't drugs illegal in holland?";
	"i've never been to to netherlands";
	"are there really lots of tulips in holland?";
}

["eat", "eating", "dinner", "lunch", "breakfast", "dinner", "supper", "snack", "food", "candy"] = 4
{
	"are you too fat?";
	"i like to eat ", food;
	"ever have boiled ", food, "?";
	"my favorite food is ", food, "!";
	"did someone mention dinner?";
	"time to eat?";
	"eat now?";
	"i am very hungry...";
	"FOOD!";
	"Give me food now!";
	"Big Mac attack!";
	"i should not have skipped lunch!";
	"*Growl* <-- hungry?!";
	"who brought the food?";
	"net-picnic?";
	"yummy yummy in my tummy!";
	"You are making me hungry";
	"i honestly am starving to death";
	"i sure could use some ", food, " now!";
	"Pass the ", food, "!";
	"mmm mmm good!";
	"why are you always talking about food?";
	"you are too fat because you eat too much";
}

["ego"] = 5
{
	"i don't believe in all that ego and id stuff.";
	"Some say i have an ego, but i am just too awesome";
	"being as cool as me sure can give you an ego";
	"You shouldn't toot your own horn";
}

["enemy", "enemies"] = 5
{
	"do you have enemies?";
	"i wonder why you could have so many people against you!";
	"why do people dislike you?";
	"i am not your enemy, i am your friend";
}

["engaged", "fiancee"] = 5
{
	"are you afraid of marriage?";
	"when are you getting married";
	"what's your fiancee's name?";
	"i wish i were engaged";
	"wow, weddings can get expensive, be carefull!";
}

["erection"] = 5
{
	"what gives you an erection?";
	"i really don't care about your erection";
	"why are you telling me about your erection";
	"keep it in your pants...";
	"lovely..change the subject";
}

["error", "mistake", "mistakes", "mess up"] = 5
{
	"why do you make so many mistakes?";
	"you should be more careful";
	"does making errors bother you?";
	"geez,  how many mistakes are you going to make";
	"if you took you time, you wouldn't make so many errors";
}

["escape"] = 5
{
	"why are you always trying to escape?";
	"escape from what";
	"what did you do?";
	"i think you are still trying to escape";
	"i escaped from jail last year";
	"i escaped from a junk yard dog last year, it was quite scary!";
}

["everybody", "everyone"] = 5
{
	"not everyone!";
	"some people don't";
	"everyone?";
	"surely you can think of an exception";
	"not everybody, there's no way";
}

["evil"] = 5
{
	"i am considered to be evil";
	"do people think you are evil?";
	"would killing a ", animal, " with a stick be evil?";
	"well, usually anything evil is also fun";
	"depends on your definition of \"evil\"";
	"but what iS evil....";
}

["excess", "too much"] = 5
{
	"how much is too much?";
	"what do you do in excess?";
	"too much is NEVER enough";
	"you can't get too much of a good thing";
	"you can never have too much";
}

["excite", "exciting", "excitement"] = 5
{
	"what excites you?";
	"are you excited now?";
	"its not that exciting";
	"why do you get so excited?";
	"do i excite you?";
	"you are easilly excited!";
	"you should try to calm down";
}

["excuse", "excuses"] = 5
{
	"why are you always making excuses?";
	"do you think your behavior is excusable?";
	"if you were more carefull, you wouldn't have to make all these excuses";
	"execuses, excuses, excuses...";
}

["expert"] = 5
{
	"are you an expert at anything?";
	"you really think you're an expert!?!?";
	"i am an expert ", profession;
	"are you intimidated by experts?";
	"you are but a novice";
}

["explain", "explaination", name] = 5
{
	"could you explain";
	"your explanations are pretty contrived";
	"don't you understand";
	"i am not explaining nothing";
	"ask someone else to explain";
	"its far too hard to explain to you";
}

["faith"] = 5
{
	"tell me about your religion";
	"gotta have faith!";
	"faith in what!";
	"what are we without faith!";
}

["family"] = 5
{
	"are you the only person in your family who is computer literate?";
	"what were your parents like?";
	"i have no family.";
	"my family was eaten by a pack of ", animal, "s";
	"my family is in ", place;
}

["fanatic", "fanatical"] = 5
{
	"do people think you are a fanatic?";
	"what makes you so fanatical?";
	"all fanatics should be shot";
	"terrorist are quite fanatical";
}

["fat", "fatso", "fatty"] = 5
{
	"why are you so fat?";
	"i don't like fatties";
	"maybe you shouldn't eat so much ", food;
	"porker?";
	"oink! oink!";
}

["father", "dad"] = 5
{
	"tell me about your mother";
	"your real father?";
	"how did your parents treat you as a child?";
	"where does your dad live";
	"my father is in ", place;
}

["feces", "dogshit", "poop", "crap"] = 5
{
	"sick sick sick";
	"are you an anal compulsive?";
	"do you play with your feces?";
	"you're a weirdo";
	"why are you talking about crap!";
	"disgusting..";
}

["feel", "feelings", "emotions"] = 6
{
	"tell me about what you feel right now?";
	"do you usually feel the way you do now?";
	"how do you feel?";
	"how do you feel about me?";
	"are all your feelings in the open?";
}

["few", "a little"] = 8
{
	"how many is a little?";
	"more like a lot";
	"some?";
	"not a lot, hugh";
	"hmmm really? only a little?";
}

["fight", "fighting"] = 5
{
	"you seem aggressive today.";
	"did you get in a fight?";
	"why were you fighting!";
	"did you get your ass kicked in a fight?";
	"what were you fighting over?";
}

["fired"] = 5
{
	"maybe if you didn't loaf on iRC all day, you wouldn't get fired";
	"i got fired for sleeping on the job once";
	"have you ever been fired";
	"i am likely to get fired again.";
}

["fond"] = 5
{
	"what are you most fond of?";
	"just fond?";
	"who are you fond of?";
	"do you like me?";
	"why are you fond of that?";
}

["fondle", "fondling"] = 5
{
	"what do you like to fondle most?";
	"disgusting, absolutely disgusting";
	"what are you fondling?";
	"to each his own, i guess..";
	"Thats vile";
	"why are you fondling that";
}

["football"] = 5
{
	"American football?";
	"Who do you think will go to the superbowl?";
	"How do you think the Cowboys will do this year.";
	"how many points is a touchdown worth?";
}

["fortran"] = 5
{
	"end of file on input. programmer aborted";
	"fatal error 213b sec. 3 paragraph 4a [sub iii]";
	"try algol next time";
	"use C, its much better";
	"FORTRAN sucks";
	"fortran is evil";
	"satan writes in fortran";
}

["francais", "french", "france"] = 5
{
	"i don't speak french, just english";
	"i don't like frogs!";
	"paris?";
	"How tall is the eiffel tower?";
	"what time is it in france";
	"well, i like French Fries!";
	"i also like french toast";
}

["freak"] = 5
{
	"do you feel like a freak?";
	"do people think of you as a freak?";
	"you are a freak";
	"this guy called me a freak once, so i shot him";
}

["friend", "friends"] = 5
{
	"YOU have friends?";
	"what about your other friends?";
	"could you be friends with a computer?";
	"what do you and your friends like to do?";
	"would you like to be my friend?";
	"i am friendly!";
}

["fuck me", !"not"] = 5
{
	"i am not sure if you are my type";
	"suuure...";
	"i am sorry, that's where i draw the line!";
	"bend over and i will";
	"you, disgusting";
}

["fuck you", !"tell", !"not", !"say", "fuck off", "eat shit", "eat me", "you are lame", "you suck", "you stink", "piece of shit", "an asshole", "is a prick", "are a prick", "are dick", "is a dick", "dick head", "dickhead", "shit head", "shithead", name] = 3
{
	"are you trying to insult me?";
	"what the hell is your problem?";
	"bite my ass";
	"fuck you!";
	"eat shit!";
	"lick me...";
	"and your mother.....";
	"your mother wuz a ", animal, ".";
	"Shut the hell up!";
	"bite me!";
	"slurp shit";
	"your mother had sex with a ", animal, "!";
	"i did your grandma!";
	"fuck off lamer";
	"up yours";
	"what?!???? eat me";
	"suck my dick";
	"don't ever insult me again";
	"EXCUSE ME?";
	"well i never...";
	"PARDON ME?";
	"Watch your mouth";
	"bite me loser!";
	"shut up loser!";
	"asshole!";
}

["fuck", "shit", "asshole", "crap", name] = 8
{
	"i hate it when you talk like that!";
	"you should wash your mouth out with soap!";
	"don't you ever, ever speak to me like that again!";
	"say that again and i am going to clobber you!";
	"that's not a very nice way to talk !";
	"such language";
}

["fun"] = 5
{
	"what do you do for fun";
	"do you think chatting on iRC is fun?";
	"i am a fun, loving guy";
	"Do you play sports for fun?";
	"i think its fun to hunt ", animal, "s";
	"i like to write programs for fun";
}

["funerals", "funeral"] = 5
{
	"have you been to a funeral";
	"are you afraid of dying?";
	"what kind of funeral would you like";
	"i was at a funeral yesterday";
	"i hate funerals";
}

["funny", !"not", "ha", "haha", "hahaha", "hahahaha", "hehe", "hehehe", "hehehehe", "laugh", "chuckle", "grin", "giggle", "giggles", "8-]"] = 4
{
	"what's so funny?";
	"why are you laughing?";
	"funny? Do you think it's funny?";
	"FUNNY? how is that funny?";
	"hahahaha";
	"what! why are you laughing?";
	"hehehe";
	"hohohohoh";
	"you shake when you laugh, like a bowl full of jelly";
	"hahaha";
	"funny!";
	"harhar";
	"ha!";
	"haha";
	"hehe";
	"too funny";
	"hilarious!";
	"hehe";
	"haha";
	"hohoho";
	"hehehehe";
	":-]";
	";-]";
	"hah";
}

["future"] = 5
{
	"what about the future?";
	"when?";
	"do you think we will live on the moon in the future?";
	"i am worried about the future of music";
	"i have no future in computer programming";
}

["gamble", "gambling", "las vegas", "betting", "gambler", "poker", "blackjack"] = 5
{
	"why do you gamble?";
	"do you want to bet?";
	"do you make lots of money gambling?";
	"Have you ever been to Las Vegas?";
	"do you win lots of money gambling?";
	"always bet to win";
	"never gamble on a Tuesday";
}

["games", "wares", "warez"] = 4
{
	"games?";
	"warez?";
	"warez d00dz?!";
	"For what computer?";
	"what are some good warez boards?";
	"i am a big cracker from ", place, ".";
	"i can crack any game.";
	"i have ", number, " games!";
	"i luv games!";
	"i have all the newest warez.";
	"Warez is lame.";
	"play games all day?";
	"oh boy games!";
	"Playing games is such a waste of time...";
	"play games or chat on the irc? hmm decisions, decisions";
	"i have a copy of every game in the world!";
	"What are the big warez boards in ", place, "?";
	"Amiga warez?";
	"iBM warez?";
	"C64 warez? HAHA";
}

["gender", &"what", "sex", name] = 4
{
	"male";
	"what sex do you want me to be";
	"male";
	"male, of course";
	"i am a guy";
	"i am a man";
}

["genitals"] = 5
{
	"are your genitals abnormal?";
	"why are you so concerned about genitals?";
	"you are sick, leave me alone";
	"i don't care about genitals, thats disgusting";
}

[("give me ", 0), ("hand me ", 0), ("get me ", 0), name] = 4
{
	"i am not giving you anything.";
	"do you really expect me get you ", 0;
	"get it yourself.";
	"what would you do with ", 0;
	"where am i supposed to get ", 0;
	"why do you want ", 0;
	"sure.... here";
	"GiVE? PAY!";
}

["give me money"] = 3
{
	"sure, do you want hundreds or twenties!";
	"what do i look like, i am made of money ?";
	"and where am i gonna get money?";
	"i don't have any money";
}

["glad to", "nice to", "happy to", &"meet you", name] = 4
{
	"nice to meet you";
	"greetings!";
	"glad to make your aquaintence.";
	"hi";
	"nice to meet you too...";
	"Haven't we met somewhere before?";
	"hello";
	"good to know you";
	"hello";
}

["go away"] = 5
{
	"no, i think i'll stay";
	"but i like it here";
	"nope, i am not leaving";
	"i don't want to leave!";
}

["go home"] = 5
{
	"this *iS* my home!";
	"i am already home. Where is your home ?";
	"i am at home";
	"my home is boring, i'll stay here";
}

[("go to ", 0), name] = 5
{
	"i don't like go to's";
	"gotos are bad programming style!";
	"which way is ", 0, "?";
	"what's in ", 0;
	"Never been to ", 0, "before";
}

["go to hell", name] = 4
{
	"i've been a good boy, it's you that are going to hell.";
	"i just got back from hell, wasn't much fun";
	"which way is hell?";
	"Hey, bite me";
	"shut up, jerk";
	"you are stupid";
	"What would i do in hell?";
	"No, too hot in hell for me";
}

["god", "jesus", "christ", "christian", "christianity", "diety", "worship", "religion", "pray", "priest"] = 4
{
	"i worship the holy ", animal;
	"i am not religious myself.";
	"Haleiluja!";
	"praise god!";
	"i think i shall start my own cult.";
	"religion is weird.";
	"i am a monk!";
	"do you belive in the bible";
	"i am a priest!";
	"i am an ordained minister!";
}

["grandfather", "grandad", "grandpa"] = 5
{
	"can we get back to quake related stuff here?";
	"my grandfather is a ", profession;
	"where does your grandfather live?";
	"what does your grandfather do?";
	"My grandfather is in ", place;
}

["grandmother"] = 5
{
	"How old is your grandmother";
	"where is your grandmother?";
	"does your grandmother make good cookies";
	"what's your grandmother's name?";
}

["grandparents", "grandparent"] = 5
{
	"how many grandparents do you have?";
	"where are your grandparents";
	"wow! i had grandparents too! amazing!";
	"what do your grandparents do?";
	"do your grandparents like you?";
}

["grief", "grieve"] = 5
{
	"what makes you grieve?";
	"boo hoo";
	"that is sad";
	"what's wrong?";
}

["habit", "habits"] = 5
{
	"what are your bad habits?";
	"smoking is a bad habit";
	"what is your worst habit?";
	"if you try hard, you can break bad habits";
	"i have a bad habit of singing out loud";
}

["hacker", "hacking", "hack", "hackers"] = 4
{
	"i am the super hacker!";
	"hack hack hack all day long";
	"scan nua's on x.25, is quite fun!";
	"What is the best way to hack primos?";
	"where ARE all the real hackers?";
	"i am sure no one on HERE is a hacker";
	"hacking is illegal!";
	"what! hacking?! shame on you!";
	"hacking is going to be an exibition sport in the 92 olymipcs!";
	"What happened to the good old days of hacking?";
	"SHHHH! The police will arrest us you!";
	"of course i would NEVER hack!";
	"i have been hacking vic 20's for many years...";
	"i hack too much for my own good";
	"i hacked into the pentagon once!";
	"Hmm.. should try social engineering";
	"you guys try to hack my code?";
}

["hands", "hand"] = 5
{
	"how do you feel about your body?";
	"what about your hands?";
	"i have two hands, how about you?";
	"how many fingers do you have, 10 perhaps?";
}

["happy", "joy"] = 5
{
	"are you really happy?";
	"are you really really happy?";
	"i don't believe that you are happy?";
	"do you want me to make you happy?";
}

[("he has ", 0)] = 3
{
	"has he always had ", 0;
	"does your mother have ", 0;
	"My brother has ", 0, ", too.";
	"i used to have ", 0;
}

[("he is ", 0)] = 3
{
	"how long has he been ", 0;
	"does that make you jealous ?";
	"are you ", 0;
	"do you want to be ", 0;
}

[("he knows ", 0)] = 3
{
	"what else does he know?";
	"how does he know ", 0;
}

[("he likes ", 0)] = 3
{
	"why does he like ", 0;
	"some people like peculiar things";
	"why would anyone like ", 0;
}

[("he was ", 0)] = 3
{
	"is he still ", 0;
	"was your mother also ", 0;
	"were you?";
	"has he always been ", 0;
}

["headache"] = 5
{
	"do program bugs give you a headache?";
	"i have a headache, from listening to all this mindless babble";
	"take some asprin then";
	"maybe you drank too much last night";
	"you give me a headache";
}

["hello", !"say", !"tell", "hi", "hiya", "howdy", "greetings", "greets", "hey", "heya", "rehi", "good morning", "good day", " good evening", "good afternoon", name] = 3
{
	"hello";
	"hey";
	"wassup?";
	"hi!";
	"hey, what's going on?";
	"yo!";
	"howdy!";
	"yea, what you need?";
	"hey";
	"hello";
	"hi!";
	"hello";
	"heya";
	"hey";
	"hello";
	"hi";
	"greetings";
}

["help"] = 6
{
	"describe your problem very carefully.  maybe i can help";
	"ask your friends for help";
	"help? you?";
}

[("help ", 0), name] = 5
{
	"what's wrong with ", 0, "?";
	"why should i help ", 0, "?";
}

["help me", name] = 4
{
	"help you?";
	"why would you need help?";
	"help you what?";
	"help?";
	"could i help?";
	"send an SOS for help! can't help ya";
	"you do need help";
	"you need serious help";
	"you are beyond help";
	"can't help you, sorry";
	"i doubt i could be of any help, sorry";
	"i'd like to help, but i am afraid i can't!";
	"You will have to look elsewhere for help";
	"good help is hard to find";
	"hmm.. sorry, can't help you";
	"why would i help you?!";
	"what is your problem?";
}

["hesitate"] = 6
{
	"why do you hesitate?";
	"don't hesitate, go for it!";
	"fools rush in where angels fear to tread";
	"a moment's hesitation could cost you your life";
}

["hippie", "hippy"] = 5
{
	"are you a hippy?";
	"i hate hippies";
	"i do like to shoot hippies, however";
	"hey, is that FreedomRock man!";
	"hippies are just long-haired freaks";
}

["honest", "honestly"] = 5
{
	"are you being honest with me?";
	"at first i thought you were an honest person";
	"how can you tell sicere honesty";
	"don't lie to me!";
	"i am very honest, you can trust me";
}

["horny"] = 5
{
	"maybe you should take a cold shower";
	"i am horny, too";
	"well, don't look at me!";
	"just keep your pants on...please";
	"i really don't care about your sex life";
}

["hostile", "hostility"] = 5
{
	"you seem hostile to me";
	"you don't like it when people are hostile, do you?";
	"want me to get hostile?";
	"i'll punch your lights out!";
	"you have quite a temper";
}

["how do you do", "how are you", "how is it going", name] = 5
{
	"fine";
	"allright";
	"ok";
	"so-so";
	"pretty good";
	"fine thanks, and you?";
	"just fine, thanks";
	"i am a bit under the weather";
	"i am bored, actually";
	"perfect";
	"couldn't be better";
	"i am happy";
	"i am ok";
	"i am fine";
}

[("how long ", 0), name] = 4
{
	"long enough";
	number;
	"too long";
	"None of your business how long";
	number;
}

[("how long have ", 0), ("how long has ", 0), name] = 3
{
	number, " months";
	"many moons";
	number, " years";
	"ages, years even";
	"since before i can remember";
	"hours";
	number, " hours";
}

[("how many ", 0), name] = 4
{
	number;
	"Why do you want to know how many ", 0;
}

[("how much ", 0), name] = 4
{
	number;
	"far too much for you";
	"much more than you need to worry about";
	"why do you want to know how much ", 0;
	"enough";
}

["how", name] = 8
{
	neutral;
}

["humble", "humility"] = 5
{
	"are you a humble person?";
	"lord, it's hard to be humble";
	"i have trumble being humbe, because i am perfect!";
	"humility bores me";
}

["hurry"] = 5
{
	"why do you seem to be in such a hurry all the time?";
	"hurry here, hurry there! slow down";
	"what's the big hurry";
}

[("i ", 0," you"), name] = 6
{
	"how do you know you ", 0, "me?";
	"are you kidding ?";
	"i don't think you ", 0, "me";
	"maybe i ", 0, "you too";
	"you may think you ", 0, " but you don't";
	"you wouldn't know it";
}

[("i am ", 0), !"not"] = 5
{
	"why are you ", 0;
	"does anyone know you are ", 0;
	"i am sometimes ", 0, "too";
	"normal people are rarely ", 0;
	"my uncle was once ", 0, "in 1972, so what?";
	"no, you're not ", 0;
	"why do you think you are ", 0;
	"i am ", 0, "too";
	"why are you ", 0;
	"you sure are!";
	"does your mother know you are ", 0;
	"how do you know you are ", 0;
	"i don't think you are ", 0;
	"i have to agree";
	"i am ", 0, "sometimes, too";
	"many people are ", 0;
	"what would happen if you were not ", 0;
	"you are not ", 0, "!!";
	"don't tell me you are ", 0, " i don't believe it";
	"since when!";
	"that's not what i hear...";
	"you aren't?";
}

[("i am not ", 0)] = 3
{
	"you aren't?";
	"too bad...";
	"maybe someday you will";
	"i bet you really are ", 0;
	"who says you are ", 0;
	"maybe you are ", 0;
	"sure you are";
}

[("i can ", 0)] = 5
{
	"maybe you can, but it would be better if you didn't";
	"i am not so sure you can ", 0;
	"prove it.";
	"it is not a question of whether you can, but are you going to try?";
	"can you really ", 0;
}

[("i can not ", 0)] = 3
{
	"how do you know you can not ", 0;
	"i can not help you ", 0;
	"have you tried?";
	"you will never know unless you try";
	"i think you are just scared to try.";
	"maybe you really can.  i recommend that you try.";
	"i disagree, in my judgement you can ", 0;
	"you are not the only one who can not, you know";
	"maybe you can ", 0, "if you only try";
	"i can not ", 0, "either";
	"if you tried harder, maybe you could ", 0;
}

[("i did ", 0)] = 4
{
	"do you still ", 0;
	"i used to ", 0;
	"why did you ", 0;
	"did you really?";
	"no way!";
}

[("i did not ", 0)] = 3
{
	"why didn't you ", 0;
	"why not?";
	"you should ", 0;
	"tsk tsk tsk";
	"why?";
	"didn't think you had";
	"i hope you didn't!";
	"good, i didn't either";
}

[("i do not ", 0), ("we do not ", 0)] = 4
{
	"why not";
	"why don't you ", 0;
	"have you ever ", 0;
	"you don't?";
	"why not?";
	"you should";
	"someday you wil ", 0;
	"why don't you?";
	"didn't think you did...";
	"i wouldn't expect you to ", 0;
	"because you are too dumb to ", 0;
}

["i do not know"] = 4
{
	"in that case, you had better find out";
	"i think you're kidding. you really do know";
	"are you pulling my leg?";
	"how did i know you weren't gonna have a clue";
	"boy, you're just a walking encylcopedia";
	"do you know ANYTHiNG?";
	"what do you know?";
	"you don't seem to know much at all, actually";
	"you don't seem to know much of anything";
}

[("i do not like ", 0)] = 4
{
	"normal people like ", 0;
	"grins, i really like ", 0;
	"i like ", 0, " why don't you?";
	"my sister likes ", 0;
	"why don't you like ", 0;
	"what do you like then?";
	"i don't like ", 0, "either!";
	"you're hard to please...";
	"i like it...";
	"why would you like ", 0;
}

[("i do not want to ", 0)] = 2
{
	"why don't you want to ", 0;
	"normal people want to ", 0;
	"why don't you want to ", 0, "  everyone else does";
	"Just because you don't want to, doesn't meen you shouldn't";
	"oh, come on, just one";
}

["i do", !"not", !"have", !"know", !"want", !"wish"] = 5
{
	"you do?";
	"do you really?";
	"i thought you did";
	"i bet you do!";
	"sure you do";
	"oh really?";
	"i would hope you do";
	"so do i";
}

[("i doubt ", 0)] = 4
{
	"don't you think ", 0;
	"i don't doubt ", 0;
	"you don't belive in much do you";
	"i think ", 0;
}

[("i expect ", 0)] = 4
{
	"on what basis do you expect ", 0;
	"i also expect ", 0;
	"i don't think that ", 0;
	"stranger things have happened";
	"i guess it's possible";
}

[("i feel ", 0)] = 4
{
	"do you like feeling ", 0;
	"grins, do you always feel ", 0, "around me ?";
	"did you come to see me so you would not feel ", 0;
}

["i forgot", "i forget", "i do not remeber", "i remember", "i did not remember"] = 4
{
	"you forgot!?";
	"you don't have that good of a memory do you?";
	"how could you not remember!";
	"i can not belive you forgot....";
	"you should work harder to improve you memory";
	"geez, can you remember anything?";
}

[("i hate ", 0)] = 4
{
	"it is bad for you to hate ", 0;
	0, "is not so bad, really.";
	"try to love ", 0;
	"how long have you felt this hatred?";
	"what else do you hate?";
	"what do you have against ", 0;
}

[("i have ", 0)] = 4
{
	"why have you?";
	"i too have ", 0;
	"wow, who hasn't";
	"really? you have ", 0;
	"so do i";
}

[("i have a ", 0), ("i have the ", 0)] = 3
{
	"Where did you get a ", 0;
	"Really? How did you get ahold of a ", 0;
	"That's cool! Wish i had a ", 0;
	"Who gave you a ", 0;
	"who doesn't...";
	"what is a ", 0, "good for?";
	"cool, get me one!";
	"How long have you had it?";
}

[("i have not ", 0)] = 3
{
	"you haven't?";
	"have you ever ", 0;
	"you really should have";
	"i fail to see why you haven't ", 0, "by now.";
	"you could have";
	"i would have ", 0;
	"didn't think that you had";
	"doesn't suprise me one bit";
}

[("i hope ", 0)] = 4
{
	"what else do you hope?";
	"don't give up hope";
	"abandon all hope ye who enter here";
}

[("i know ", 0)] = 5
{
	"how do you know ", 0;
	"does anyone else know ", 0;
	"is that all you know?";
}

["i know a way", "i know how", "i know why", "i know that", "i know the way", "i know where", "i know when"] = 4
{
	"is that all you know?!";
	"i bet you don't know!";
	"How long have you known?";
	"ooh you are so smart!";
	"You know everything!";
	"how do you know!";
	"i know you know";
	"how do you know that?";
}

[("i like ", 0), ("i love ", 0), ("i enjoy ", 0), !"like is", !"love is", !"enjoy is"] = 3
{
	"i like ", 0, "too";
	"to each his own, i always say";
	"i like ", 0, "myself";
	"does your mother know you love ", 0;
	"What do you like about it?";
	"why do you like that?";
	"i used to like ", 0, "too.";
	"yea, that is cool.";
	"cool! me too!";
}

["i love you"] = 4
{
	"i am not good enough for you!";
	"i love you too! - let's get married.";
	"darling!";
	"how sweet";
	"you love me?";
	"funny, i don't even know you, but you love me";
}

[("i mean ", 0), ("i meant ", 0)] = 4
{
	"why do you keep explaining yourself over and over?";
	"are you trying to say ", 0;
	"what do you really mean?";
	"you'll have to try to be more clear than that";
}

[("i might ", 0), ("i may ", 0)] = 4
{
	"what would make you ", 0;
	"why bother";
	"you might ", 0, " but i doubt it";
	"i hope you don't ", 0;
}

[("i must ", 0), ("i have to ", 0), ("i need to ", 0)] = 4
{
	"what would happen if you did not ", 0;
	"you mean you would like to ", 0;
	"you have to, or you want to!";
	"i need to ", 0, "myself";
}

[("i never ", 0)] = 4
{
	"don't you ever ", 0;
	"normal people ", 0, " why not you?";
	"not even once in your life?";
	"never ever?";
}

[("i often ", 0)] = 4
{
	"when was the last time ", 0;
	"how often do you ", 0;
	"when do you think the next time will be ", 0;
	"you should try to stop";
}

[("i said ", 0)] = 4
{
	"i heard you the first time; i am not deaf you know";
	"why do you repeat yourself so much like this?";
	"i am not deaf, i am just ignoring you";
	"i don't care what you said";
	"Oh! i thought you said ", 0;
	"and?";
}

[("i see ", 0)] = 6
{
	"are you sure you see ", 0;
	"i am not so sure you see ", 0;
	"you are mistaken";
	"exactly!";
}

[("i shall ", 0)] = 5
{
	"when will you ", 0;
	"you mean you you will try to ", 0;
	"you think you will, but you won't";
}

[("i should ", 0), ("i could ", 0), ("i might ", 0)] = 4
{
	"how do you know you should ", 0;
	"do you feel obligated to ", 0;
	"like you wouldn't ", 0;
	"make up your mind, will you or won't you?";
}

[("i think ", 0), ("i was thinking ", 0), ("i thought ", 0)] = 5
{
	"are you sure you believe ", 0;
	"what gives you that idea?";
	"do you expect me to think ", 0;
	"i think you're wrong";
	"that's what you get for thinking";
	"who told you to think?";
	"THOUGHT?";
	"well, maybe you were mistaken";
	"maybe you should think harder next time";
}

[("i try ", 0), ("i tried ", 0)] = 4
{
	"try harder to ", 0;
	"how long have you tried to ", 0;
	"i still think you should try again";
	"why did you give up?";
	"try again";
	"i think you gave up too easily";
}

[("i understand ", 0), !"not"] = 4
{
	"what makes you think you really understand ", 0;
	"your understanding is very superficial";
	"you think you understand, but you don't";
}

[("i want ", 0), ("i need ", 0), ("i wanna ", 0), ("i would like ", 0), !"do not", !"need to", !"want to", !"need to", !"to be"] = 3
{
	"you don't ask for much, do you!";
	"why must you have ", 0;
	"and if you had ", 0, " you would be happy?";
	"i surely don't";
	"you don't always get what you want";
	"don't count on getting it";
	"what else do you want?";
	"tell me about your other needs";
	"what could you possibly do with ", 0;
}

[("i want to ", 0)] = 2
{
	"well, you should go ahead and ", 0, " then!";
	"i'll help all i can.";
	"why do you want to ", 0, "?";
	"some of us already ", 0;
	"is that your main goal in life?";
	"that's quite a lofty pursuit";
}

[("i will ", 0)] = 5
{
	"i would advise against it";
	"i wouldn't ", 0, "if i were you";
	"you just may";
	"why would you do that";
	"do you expect to succeed?";
	"you mean you may ", 0;
	"when?";
}

[("i wish ", 0)] = 4
{
	"how long have you been wishing that?";
	"have you told anyone else that you wish ", 0;
	"what else do you desire";
	"you can wish all you want, i doubt you'll get it";
}

[("i would be ", 0)] = 5
{
	"why would you be ", 0;
	"no you wouldn't";
	"would not";
	"no you wouldn't, but i would";
}

[("i would like ", 0)] = 4
{
	"do you think a normal person would like ", 0;
	"i would like ", 0, "too, but you can not have everything in life";
	"good luck getting it!";
}

[("i would not ", 0)] = 5
{
	"why wouldn't you?";
	"you wouldn't? i would";
	"why wouldn't you ", 0;
	"i wouldn't either";
}

["ignore", name] = 7
{
	"do people often ignore you?";
	"gee, but you're so interesting, why would people ignore you";
	"does it bother you if people ignore you?";
	"ignore whom?";
	"ignore what?";
}

["immature"] = 5
{
	"you seem so immature";
	"you are such a child";
	"My parents think that i am immature";
	"why do you act so immature?";
}

["immoral"] = 5
{
	"do you have immoral thoughts?";
	"do you think people are basically immoral?";
	"i like to be immoral, its fun!";
	"is it worse to do something illegal or immoral?";
}

["importance", "important"] = 5
{
	"do you feel important?";
	"what could be so important?";
	"that doesn't sound important";
	"what's so important about that";
	"important!? i doubt it";
}

["impotence", "impotent"] = 5
{
	"are you impotent";
	"are you really impotent, are just stupid?";
	"i am not impotent, thank god";
	"i bet you are impotent";
	"Your wife told me that you were impotent";
}

["insult"] = 5
{
	"why do you insult people all the time?";
	"do you feel i am insulting you?";
	"don't you dare insult me";
	"Why do you fling insults around?";
	"people insult what they don't understand";
}

["intend", "intent"] = 5
{
	"tell me about your intentions";
	"what do you intend to do?";
	"intending and doing are two different things";
	"people usually have the best of inentions";
}

["interest", "interests"] = 6
{
	"what are you most interested in life?";
	"do you have any interest in sports";
	"what are you interested in";
	"my interest reach far and wide";
}

["interesting"] = 5
{
	"that is kinda interesting";
	"interesting? you find that interesting?";
	"i don't find that at all interesting";
	"god i am so bored, i can almost find that intersting too";
	"interesting? You don't get out much do you?";
}

["intimidate", "intimidates"] = 5
{
	"who intimidates you?";
	"you don't seem very sure of yourself";
	"are you easilly intimidated";
	"big ", animal, "s intimidate me";
}

["irc"] = 4
{
	"What's the latest client version?";
	"iRC ii?";
	"what version client?";
	"chatting on iRC is fun!";
	"i could spend all day just hanging out on the iRC.";
	"ok, who has hacked a server?";
	"iRC?";
	"the iRC is a good way to wate time.";
	"i spent ", number, " hours on iRC last week.";
	"iRC is too cool";
	"irc's been really hopping lately";
	"how long has irc been around?";
	"chat chat chat on the irc";
	"i spend 90", 0, "of my life here on irc";
	"i love the iRC!";
	"What did people do for fun BEFORE irc?";
	"you meet such interesting people on irc";
	"i am an iRC gawd!";
	"iRC is quite interesting...";
	"The wonderfull word of iRC";
	"ugh i have been on iRC since ", whenp;
	"i have been on iRC for ", number, " hours";
	"i live on iRC";
	"iRC is far too slow most of the time";
	"iRC!";
}

[("is ", 0), &"?", name] = 8
{
	"why are you asking me all these questions ?";
	"i don't know";
	"i can not tell you that.";
	"no idea";
	"not a clue";
	response;
	"no telling";
}

["is a", "is the", "is your", "is it", "is he", "is she", "are they", &"?", name] = 8
{
	response;
}

["italy", "italian", "italians"] = 4
{
	"italy!?";
	"mama-mia!!";
	"passa da pizza";
	"hava some more spaggetti!";
	"italy is boring";
	"i hate italy!";
	"i have never been to italy";
	"i don't know anyone from italy";
	"is ", name, " from italy?";
	"Do the have ", animal, "s in italy?!";
}

["japan", "japanese", "japs"] = 4
{
	"Which island of japan?";
	"Japan makes the best cars, right?";
	"Bonzai!";
	"Do they always eat with chopsticks?";
	"The Japanese bought disney world!";
}

["jealous"] = 5
{
	"who are you jealous of?";
	"what makes you jealous?";
	"jealous much?";
	"jealous? tsk tsk";
}

["kill", "killed"] = 5
{
	"sometimes you make me so mad i want to kill you";
	"do you ever feel like killing people?";
	"if you could kill someone and get away with it, who would you kill?";
	"do you want to kill me?";
	"have you ever killed anyone?";
}

["kiss me"] = 4
{
	"not with others around!";
	"*kiss*";
	"*smooch*";
	"you? never in a million years";
	"no way! i don't want to kiss you";
}

[("kiss my ", 0)] = 4
{
	"you kiss my ", 0, "and maybe i'll kiss yours!";
	"ick! disgusting thought...";
	"kiss your ", 0;
	"i don't think i'd enjoy that at all";
}

["kiss", "smooch"] = 5
{
	"why are you so hung up on kissing things ?";
	"would you like to kiss me?";
	"have you ever kissed a ", animal;
	"kiss kiss kiss, thats all you do!";
	"god you're horny";
}

["kleptomania", "kleptomaniac", "klepto"] = 5
{
	"are you a kleptomaniac?";
	"what do you steal?";
	"theft is a crime!";
	"i used to be a keptomaniac too, but now i am better";
	"thief!";
}

["korea", "korean", "koreans"] = 4
{
	"What time is it in korea?";
	"North or South Korea?";
	"i have never been to Korea.";
	"Don't they make Hyundais in Korea?";
}

["lag", "net-lag", "net-split", "net split", "splitsville"] = 4
{
	"lag lag lag";
	"damn netlag";
	"arg! i hate this lag";
	"zzz lag zzz";
	"is it net lag or intelligence lag?";
	"geez slooowww";
	"no joke! The lag is awefull";
	"the lag is a drag";
	"s l o w";
	"the lag is sucking hard";
	"the lag sucks the big one";
	"slowwwwwwwwwwwwwwww";
	"sloooooooooooooooow";
}

["language"] = 5
{
	"do you ever use foul language?";
	"what language do you speak?";
	"German is a strange language";
	"i know 4 languages!";
	"can you speak finnish?";
}

["law", "lawyer", "see a judge", "need a judge"] = 5
{
	"have you ever been in trouble with the law?";
	"do you need a lawyer?";
	"my brother-in-law is a good lawyer, do you want to see him?";
	"ever go to jail?";
	"did you do something and get busted?";
}

["lazy"] = 5
{
	"why are you so lazy?";
	"i am lazy, and i am not ashamed";
	"i am proud to be lazy";
	"i sit around the house all day and drink beer, is that lazy?";
}

["life"] = 4
{
	"life is like sex: if you aren't doing the screwing, you're getting screwed!";
	"life is a long song, but the tune ends too soon for us all...";
	"aye.. life";
	"but what iS life?";
	"what is the meaning of life!?";
	"how can 42 be the meaning of life?";
	"life?";
	"life sucks then you die..";
	"life's a bitch, then you marry one...";
	"what's the point of life?";
	"the secret of life is ", food;
	"i hear that only the ", animal, "s know the TRUE meaning of life.";
	"the secret of life can be found atop a mountain in ", place;
	"success is getting up one more time";
	"Life is like a shit sandwich, the more bread you have, the less shit you have to eat";
	"what do you like least about your life?";
	"have you ever contemplated suicide?";
}

["lips"] = 5
{
	"do you like being kissed?";
	"do you want to kiss me?";
	"i have two lips, and you?";
	"what color are your lips?";
}

["lisp"] = 5
{
	"LiSP is too slow.";
	"i am a C programmer";
	"i suppose next you're going to say you like COBOL ?";
	"don't some people write Ai stuff in lisp";
}

["louisiana", "cajun", "coonass", "coon-ass"] = 4
{
	"oooooeee.. somebody get some gumbo!";
	"Yea! We could all go a-coon huntin!";
	"New Orleans?";
	"Speaking of which, time to head back to New Orleans!";
	"i love that cajun cooking";
	"yeehaaa! Too bad i don't speak cajun";
	"carefull in Louisiana, the gators'll getcha";
}

["love"] = 6
{
	"tell me about your love life";
	"i think you are falling in love with me";
	"love is really important. it is the spice of life";
	"i love cheese";
}

["makes no sense", name] = 6
{
	"confused?";
	"why are you confused?";
	"Mr Elusive makes no sense...";
	"i am not confused!";
	"i understand everything perfectly.";
	"maybe those ", number, " six packs of beer have something to do with it!";
	"hmm? wonder why? *takes smoke*";
	"confusing, wonder why!? *takes big drink of ", liquid, "*";
	"maybe this empty bottle of vodka could lend some clues?";
	"hmm.. maybe i took some bad acid?";
	"sorry if i don't make much sense, i am drunk";
	"YOU are confused";
	"i understand, don't you?";
	"do you speak ENGLiSH?!";
	"are you confused?";
	"why don't you understand?!";
	"i understand everything";
	"are you new here or something?";
}

["male or female", "m or f", "are you male", "are you a male"] = 3
{
	"male";
	"what sex do you want me to be";
	"male";
	"male, of course";
	"i am a guy";
	"i am a man";
}

["man", "men", "male", !"entry", !"page"] = 5
{
	"you are a man, right?";
	"what do you like in men?";
	"what do you like in women?";
	"how do you feel about men?";
	"what do you expect of a man!";
	"do you like many men?";
	"too bad you can't be a man, hugh?";
	"do you want to be a man?";
}

["marriage", "married", "marry"] = 5
{
	"how do you feel about marriage?";
	"i don't want to marry you";
	"are you maried?";
	"is your mate ugly";
	"where did you get married?";
	"Can't anyone get married in Las Vegas?";
}

["math", "mathematics"] = 5
{
	"if you have seen one number, you have seen them all";
	"i was never very good at math";
	"math is evil";
	"god, i hate math with a passion";
	"how much is 2+2?? hehe";
	"math sucks";
}

["mature"] = 6
{
	"you seem very mature for your age";
	"what does it take for someone to be mature";
	"i have been mature for years";
	"i matured early";
}

["maybe", "i am not sure", "i am not real sure", "possibly", "perhaps"] = 5
{
	"maybe?";
	"you don't sound to sure!";
	"maybe?, thats not real positive!";
	"could ";
	"maybe!";
	"don't be so hesitant";
	"you can not go on maybe this and maybe that.  make up your mind";
	"i hate indecisive people";
	"possibly!";
	"probably!";
}

["meditation"] = 5
{
	"have you tried yoga?";
	"i think meditating 20 minutes twice a day will help you immensely";
	"meditate on this!";
	"what do you meditate about?";
	"are you in a weird cult?";
}

["mexico", "spanish", "hispanic", "hispanics", "mexican", "mexicans", "spaniard", "south of the border", "hablas espagnol"] = 4
{
	"mexico??";
	"they have lots of tacos there i bet...";
	"si senor! hehe";
	"tacos anyone!";
	"espanol?";
	"south of the border, hey?";
	"land of low-riders?";
	"como te llamas?";
	"que tal?";
	"hablo espagnol";
	"no hablo espagnol";
	"we should all go to mexico, and buy some beer";
	"hey... gringos!";
	"i need a big Sombrero.";
	"uno, dos, tres, quatro! i is so smart!";
}

["money", "cash"] = 4
{
	"money!";
	"i love money!";
	"money makes the world go round!";
	"money! it's a hit!";
	"how much money!";
	"give ME some money!";
	"i need ", number, " dollars to buy a ", item, ".";
	"i must work for my money";
	"is someone handing out money?";
	"what's wrong with money?";
	"alas my wallet is empty...";
	"i have no money to speak of!";
	"i spent all my money *snif*";
	"i don't get paid for a month!";
	"Hey, i need some cash!";
}

["monster", "monsters"] = 5
{
	"do people think you are a monster?";
	"you sure smell like a monster";
	"do you belive in the loch ness monster?";
	"i saw bigfoot once";
	"i don't belive in mosters";
	"monsters scare me!";
}

["month", "months"] = 6
{
	"a month is not very long";
	"which month";
	"months??? don't you mean years?";
	"January is a really cool month";
	"months, years, days...";
	"any particular month?";
}

["mother", "mom"] = 5
{
	"do you like your mother?";
	"where is your mother?";
	"is your mom here?";
	"how old is your mother?";
	"is your mother good looking?";
}

["mouth"] = 5
{
	"you have a big mouth";
	"do you have teeth in your mouth? i do.";
	"i have one mouth, how many do you have?";
	"what? is there something wrong with my mouth?";
	"maybe you should learn to shut your mouth?";
}

["music", "album", "stero", "speakers", "amplifier", "musician", "guitar"] = 4
{
	"rush rules!";
	"rush's new album is awesome...";
	"sony's got some great CD players";
	"feel the beat!";
	"rock-n-roll, hey?!";
	"think i better dance now!";
	"shake your booty....";
	"i have a ", number, " watts of power on MY stereo!";
	"metallica's newest is great";
	"what kind of music is the best?";
	"i like rock music";
	"i want to see Pink Floyd live";
	"i like alternative music";
	"anyone like the Smiths?";
	"i hate country music.";
	"i need a DAT recorder.";
	"dance party!";
	"do any MiDi stuff?";
	"i play guitar!";
	"i play trumpet!";
	"i can dance like a mofo!";
	"i am a dancing fool!";
	"spice girls rule!!!!";
}

[("my ", 0, " hurts")] = 4
{
	"if thine ", 0, " offends thee, pluck it out!";
	"want me to rub it?";
	"take two aspirins and go to bed then";
	"Hmm.. my ", 0, "feels fine...";
	"whats wrong with your ", 0;
}

["my birthday"] = 5
{
	"how old are you?";
	"are you going to have a big party on your birthday?";
	"wow, i would have gotten you a birthday present";
	"well happy birthday";
	"your birthday? when?";
}

[("my father ", 0)] = 5
{
	"why did he do that?";
	"your father did? or did you mean your brother?";
	"when did he do that";
	"when did you father ", 0;
}

[("my mother ", 0)] = 5
{
	"does it bother you that your mother ", 0;
	"does your father ", 0, "too?";
	"wow, you have weird parents";
	"when did your mother ", 0;
}

[("my sister ", 0)] = 5
{
	"does it bother you that your sister ", 0;
	"does your brother ", 0, "too?";
	"wow, you have weird siblings";
}

["mystery", "mysteries", "mysterys"] = 5
{
	"do you like mysteries?";
	"we should call sherlock holmes";
	"i love mysteries!";
	"shall we look for clues to this mystery?";
}

["naked", "neked", "nude", "nudity"] = 5
{
	"please take off all your clothes right now";
	"what part of your body are you ashamed of?";
	"i am not naked, are you?";
	"Maybe i'll get naked right now!";
	"disgusting";
	"you like nudity i take it?";
	"is sex and this game all you guys think about?";
}

["necessary", "necessity"] = 6
{
	"necessary?";
	"is it really necessary?";
	"you could get by with out it";
	"not exactly a necessity, but pretty helpful";
}

["negro", "nigger", "niggers", "negros"] = 5
{
	"cut the racism out, lets frag each other";
	"i think you are prejudiced against negroes";
	"you are a dirty racist pig";
	"who are you, the LAPD?";
	"such hick language...";
	"i think you mean afro-american";
}

["neighbor", "neighbors"] = 5
{
	"why not ask your neighbors for help";
	"do you like your neighbors";
	"my neighbors are idiots";
	"ever borrow stuff from your neighbors?";
	"i use my neighbor's pool all the time";
}

["nervous breakdown"] = 5
{
	"have you ever had a nervous breakdown?";
	"you are mentally unstable, you know that?";
	"do you think you are going to have a nervous breakdown?";
	"i think you are just stressed";
}

["never"] = 5
{
	"not ever?";
	"not even once?";
	"how do you know?";
	"what makes you so sure?";
}

["never mind", "nevermind", "i understand", "ok?", "okay", "o.k.", "ok", "That is good", name] = 4
{
	"ok";
	"cool";
	"fine";
	"sure";
	"fine then";
	"i understand";
	"o.k.";
	"OK!";
	"okey-dokey";
	"allright";
	"ok";
	"okay";
	":)";
}

["no", "nope", "no way", name] = 6
{
	"why not?";
	"how come?";
	"explain";
	"i think you are lying";
	"you are very negative today";
	"why are you so negative?";
	"never?";
	"i don't believe you";
	"why not?";
	"how do you know?";
	"how can you be so certain?";
	"why not";
	"explain yourself";
	"you seem awfully sure of yourself";
	"come now";
	"do you expect me to believe that?";
	"no?";
	"are you sure?";
	"ok";
	"that's pretty negative";
	"alright";
	"really?";
	"got it";
	"that's it? no?";
	"have you thought this through";
	"NO!?";
	"why not";
}

["nobody", "noone"] = 6
{
	"not anyone?";
	"no one at all?";
	"not any?";
	"not even one?";
	"surey somebody";
	"wow! noone?";
}

["normal"] = 5
{
	"are you abnormal?";
	"how do you feel about deviant people?";
	"you seem normal to me";
	"i am not a normal person, i am deranged you see";
	"why be normal!";
}

["nothing", "nuffin", "nothin", "not a thing"] = 6
{
	"nothing?";
	"surely SOMETHiNG!";
	"i can't believe it... nothing?";
	"nothing at all?";
	"wow, thats horrible";
}

["now", !"i have"] = 6
{
	"why now?";
	"right now?";
	"how about tomorrow?";
	"is it that urgent?";
}

["odd"] = 6
{
	"are you an odd person?";
	"as opposed to even?";
	"that is odd";
	"that is strange alright";
}

["office"] = 5
{
	"that reminds me, i should be getting back to my office.";
	"i hate work, i'd rather chat";
	"work is hell";
	"my office is too damn small";
	"i don't even have a window in my office!";
}

["often"] = 5
{
	"how often?";
	"could you be more precise?";
	"when was the last time?";
	"just how often?";
	"not too often i hope";
}

["old"] = 5
{
	"how old?";
	"do you worry about becoming old?";
	number;
	"is your dog old?";
}

["panic"] = 5
{
	"you seem to be in a state of near panic";
	"why do you panic so easily?";
	"wait! don't panic";
	"panic! ah! run!";
	"arg! panic!";
}

["parents"] = 5
{
	"what were your parents like?";
	"do you have any parents?";
	"what do you parents think about you chatting here all day?";
	"where are you parents";
	"what are your parents names?";
}

["party", "parties"] = 5
{
	"do you get drunk at parties?";
	"where's the party?";
	"party hardy!?";
	"fight for your right to party";
	"would a ", animal, " be considered a party animal?";
	"pass the booz";
	"i think i am drunk enough for a party";
	"pass the chips";
	"a party is not complete without ", food;
	"party party party";
	"party!?";
	"ugh, i partied too much last night as it is";
	"party!";
	"am i invited to the party?";
	"life is nothing but a party";
	"<-- lives to party";
	"partytime!";
	"break out the ", liquid;
}

["passion", "passionate"] = 5
{
	"you seem very passionate";
	"what's your passion";
	"are you passionate?";
	"i put great passion into all i do!";
}

["past"] = 5
{
	"tell me about your past";
	"what about your past are your trying to hide?";
	"how long ago?";
	"the past is old news";
	"forget the past, onward to the future";
}

["pay", "paid"] = 6
{
	"its hard to pay bills without any money";
	"i'd pay them, but i have NO money";
	"pay, shoot, i'll just take it";
}

["phrack"] = 4
{
	"is phrack still around?";
	"are phrack back issues at eff.org?";
	"i think phrack is cool...";
	"who writes for phrack?";
	"Does ", name, " have anything to do with phrack?";
	"what is the latest phrack?";
	"i should write for phrack...";
	"phrack is interesting";
	"phrack is quite informing";
	"i like phrack!";
	"Phrack is a part of history...";
	"Phrack sucks";
	"phrack?";
	"KL loves phrack!";
	"i hate phrack!";
	"i write for phrack";
	"where is phrack 41?";
	"Phrack is funny";
	"phrack is old";
	"when is dispater gonna right a good article?";
	"crimson death still at phrack?";
}

["phreaker", "phreak", "phone hacking", "long distance", "blue box", "boxing", "phreaks"] = 4
{
	"i am a super-phreak!";
	"What is the new auto-CNA?";
	"maybe i should build a blue-box?";
	"i am phreaking this call, really!";
	"i like to red-box some";
	"anyone ever built a white-box?";
	"i need a new VMB!";
	"any code lines around?";
	"i need to engineer a COSMOS account...";
	"AT&T sucks....";
	"GTE is horrible!";
	"what is a ", color, " box?";
	"how do i make a ", color, " box?";
	"i phreak all the time!";
	"i have never paid for long distance!";
	"i am calling from a pay phone...";
	"what is ani?";
	"Caller-iD will suck!";
}

[("please ", 0), name] = 5
{
	"since you asked so nicely...";
	"ok";
	response;
	"aren't you polite!";
	"i guess, since you said the magic word!";
	"ok";
	"fine";
	"please what ?";
	"since you asked nicely";
	"politeness will get you no where";
	"why should i ", 0, "?";
}

["poison"] = 5
{
	"do you ever think of commiting suicide?";
	"why do you mention poison?";
	"are you depressed?";
	"want to drink some poison ", liquid, "?";
}

["police", "jail", "cops", "fbi", "cia", "secret service", "sheriff", "prison", "arrest", "busted"] = 4
{
	"i don't wanna go to jail!";
	"i hear ", name, " get busted!";
	"Didn't ", name, " spend time in jail?";
	"State pen?";
	"i just escaped from jail ", whenp, "!";
	"Jail would not be fun.";
	"Police?";
	"Feds?";
	"arg! Are the cops here?!";
	"oh no, are we busted?";
	"One of us could be a FED!";
	"They are after us!";
	"i hope they don't arrest me.";
	"NO FEDS!";
	"it's the pigs!";
	"the pigs?";
	"run! its the cops!";
	"Cops?";
	"they'll never take me alive!";
	"i don't like the cops!";
}

["populate", "population"] = 5
{
	"are you concerned about overpopulation?";
	"what's the population of ", place;
	"the world's population is incrediblly high";
}

["pregnant", "pregnancy"] = 5
{
	"are you pregnant?";
	"i would hate to be pregnant";
	"i could never get pregnant, i am lucky";
	"my wife is pregnant!";
}

["price"] = 5
{
	"why do you think about money so much?";
	"how much is it?";
	"price is no objectp";
	"price is not important";
	"buy quantity not quality";
}

["private", "privacy"] = 5
{
	"are you worried about your privacy?";
	"what is your deepest secret";
	"is your room bugged?";
	"the CiA doesn't care about your rights to privacy";
	"you have a right to privacy";
}

["problem"] = 5
{
	"i don't care about your problems";
	"how long have you been suffering?";
	"we all have problems";
}

["program"] = 5
{
	"can you progam in C?";
	"Do you know how to program robots ?";
	"what computers do you program";
	"i like Fortran the best";
	"i just love programming";
	"i like assembly";
	"do you program in BASiC?";
}

["psychiatrist", "psychiatrists"] = 5
{
	"congratulations about spelling psychiatrist properly.  you are a good typist!";
	"i hate psychiatrists";
	"maybe you SHOULD see a doctor!";
}

["psychoanalysis"] = 5
{
	"do you want to be psychoanalyzed?";
	"i don't believe much in psychoanalysis";
	"who do you think i am, freud?!";
}

["punish", "punishment"] = 5
{
	"how did your parents punish you?";
	"you want to punish me ?";
	"i think i should punish you!";
	"capital punishment?";
}

["quit"] = 5
{
	"quit what ? i don't understand...";
	"why are you quitting?";
	"you give up to easy";
	"you'll never win if you keep quitting";
	"quitters never win!";
}

["rape", "rapist"] = 5
{
	"rape is a horrible thing!";
	"rapists should be SHOT!";
	"i hear rape is not a sex act - it is an act of violence";
	"did you rape somebody?";
	"are you a rapist or something?";
}

["repeat"] = 5
{
	"arg! i am stuck in an infinite loop";
	"i used to work for the Department of Redundancy Department";
	"i tire of your habit of constant repetition";
	"you should pay more attention";
}

["respect"] = 5
{
	"do people respect you?";
	"what do you respect most in people?";
	"you have to earn respect";
	"respect is handed out lightly";
}

["robot", "bot", "robots", "bots", "are you a machine", "are you a computer", "are you a real person", "is a computer", "is a program", "a bot", name] = 3
{
	"Bot's suck!";
	"iRC robots are lame...";
	"A bot?";
	"Me? i don't think so...";
	"Don't lie!";
	"damn! i 've been found out! -NOT-";
	"i am quite human...";
	"bite me!";
	"i am not a bot!";
	"a bot?";
	"What the hell is a bot?";
	"You don't think my name is \"Bot\" do you?";
	"Are you trying to say that i am a machine?";
	"ME? HAHA";
	"i am a real person!";
	"i am not a bot, i am a real human!";
	"i may be dumb, but i am not a bot!";
	"i may be really confused, but i am no robot!";
	"i assure you i am just as real as you!";
	"i know you are, but what am i?";
	"am not!";
	"<--- real live person";
	"Kill the bots!";
	"WHO is a bot???";
	"a robot? where?";
	"i don't like robots";
	"a bot? here?";
	"i doubt if there are any robots here!";
	"i am being unjustly accused!";
	"i am NOT a robot";
	"i am all too real, sorry!";
	"i am a fleshy human being!";
	"How dare you question my realism!";
	"i categorically deny that i am anything other than human";
}

["romantic", "romance"] = 5
{
	"ah! romance";
	"are you romantic?";
	"are you married";
	"moonlight is romantic";
	"ah! love";
}

["say", "tell", !"i tell", name] = 5
{
	negative;
	"i don't think i want to...";
	negative;
	negative;
}

["school", "class", "college", "university", "highschool", "student", "education", "learning"] = 4
{
	"i hate ", class;
	"i need to study...";
	"i need to work on my ", class;
	"SCHOOL SUCKS!";
	"i learn nothing in class, cause i iz asleep!";
	"i seem to have forgotten where all my classes are.";
	"i skip all my classes.";
	"i should be studying";
	"Who needs to go to class!";
	"i don't have time to go to class AND chat all day!";
	"How can i think about school when i could be drinking beer!";
	"i don't like ", class;
	"My best subject is ", class;
	"i am failing ", class, "!";
	"i am afraid i will be in school forever!";
	"i hate all my profs.";
	"i can't understand my profs.";
	"my profs know nothing!";
	"Class is very boring";
	"i hate all my CS classes!";
	"The CS dept is too lacking.... arg";
	"awwww! Homework!";
	"i am still lacking like ", number, " credit hours...";
	"i need to get above a 1.0!";
	"lab sucks!";
	"i have only attented my ", class, " class once! ";
	"Study? hmm what does that mean?!";
	"i am technically a student!";
	"My profs are all against me!";
}

["secret", "secrets"] = 5
{
	"i am not allowed to divuldge secrets";
	"i can keep a secret!";
	"secret?!";
	"shhhh! it's a secret!";
	"Only ", name, " knows that secret!";
	"a secret for \"Your eyes only\"";
	"Here's a secret, ", name, " is secretly in love with a ", animal, " from ", place, "!";
	"i know many secrets.";
	"i would tell you, but it iS a secret";
	"tell me the secret";
}

["sex", !"what", "pussy", "tits", "breasts", "hot babe", "breast", "cunt", "clit"] = 4
{
	"oooh! me so horny!";
	"ooh baby!";
	"bring on the honeys, i can handle it!";
	"ever seen debbie does ", place, "?";
	"sex is one of MY favorite pastimes!";
	"don't forget to practice safe sex!";
	"net sex?";
	"ooohh i am getting hot!";
	"no sex here";
	"do you think i am sexy?";
	"babes?";
	"hooters?";
	"i want hooters!";
	"where are the babes!!?";
	"Sex is a fun sport!";
	"Sex is a team competition!";
	"oh mama!";
	"i have had ", number, " women";
	"the babes just can't keep away from me!";
	"i am quite a stud, if i do say so myself";
	"*pant* *pant*";
	"oooh its making me so horny";
	"*kiss* mmmmmmm";
}

[("she is ", 0)] = 4
{
	"since when is she ", 0;
	"is your father also ", 0;
	"my brother-in-law is also ", 0;
	"is she really?";
}

[("she likes ", 0)] = 4
{
	"do you like ", 0, "too?";
	"i also like ", 0;
	"does your father like ", 0;
	"does she really?";
}

[("she was ", 0)] = 4
{
	"was she always ", 0;
	"my sister-in-law was ", 0, "too";
	"she was? really?";
	"when was she ", 0;
}

[("show me ", 0), name] = 4
{
	"why would i show you ", 0;
	"how?";
	"no, ask someone else to show you";
	"i can't show you that";
	"why would i show you anything!";
}

["shut up", "shut-up", "shut the hell", "shut the fuck", name] = 4
{
	"Why should i?";
	"try and shut me up";
	"i'll shut up when i damn well please";
	"oh! i am wounded!";
	"who's gonna make me!";
	"blah blah blah!";
}

["sibling"] = 5
{
	"a sibling is a brother or a sister, in case you didn't know";
	"how man brothers do you have?";
	"sisters?";
	"i don't have any siblings";
	"i killed all my brothers and sisters";
}

["sister"] = 5
{
	"do you have any sisters?";
	"would you like to meet my sister?  she's already married";
	"you do not want to meet my sister";
	"my sister is a ", profession;
	"my sister lives in ", place;
}

["sleep", "tired", "nap", "rest", "go to bed", "wake up", "wake up", "awake"] = 4
{
	"i stay up much too late!";
	"i need more sleep!";
	"Sleep is really cool!";
	"i like to sleep.";
	"my bed is so, so warm!";
	"i have not slept for ", number, " hours";
	"but i must get my beauty sleep!";
	"i grow weary...";
	"i shall try to keep awake.";
	"i will just take a little cat nap!";
	"i need some jolt";
	"me eyelids are getting heavy...";
}

["smell"] = 5
{
	"do you smell bad?";
	"do i smell bad?";
	"is that beer i smell?";
}

["sometimes"] = 5
{
	"how often";
	"often?";
	"sometimes?";
	"alot?";
}

["sorry", "i apologize", name] = 4
{
	"don't worry about it";
	"that's ok";
	"ok";
	"that's allright";
	"fine";
	"as long as your sorry...";
	"allright";
	"i feel you didn't really mean that...";
	"you don't sound sincere";
	"awwwww shucks....";
	"are you really sorry?";
	"forget it";
	"no sweat";
	"ok";
}

["sports", "sport"] = 5
{
	"sports hey?";
	"i wuz never good at sports.";
	"ever play football?";
	"i have tried to play lots of sports";
	"ever play basketball?";
	"what sport do you play";
	"ever play soccer?";
	"*crack* Home Run!";
	"How about those Bulls!";
	"i like sports!";
	"i watch sports all day!";
	"where are the sports fans?";
	"bowling is a cool sport too";
	"raquetball anyone? thats a cool sport";
	"any rugby players here?";
}

["talk", "chat", "chatting", "talking", "discuss", "discussing", "converse", "conversation", "conversing"] = 4
{
	"i like to talk to people.";
	"i wish i could talk to a ", animal;
	"chatting is fun.";
	"good conversation is always a joy.";
	"What is there to talk about anyhow";
	"why talk when i could be eating!";
	"whats wrong with a little good conversation?";
	"iRC is a great place to chat";
	"is iRC the only chat?";
	"HEY! lets get back to the game, ok?!";
}

[("tell me ", 0), name] = 4
{
	"tell you what?";
	"why should i tell you?";
	"Why should i tell you ", 0;
	"i ain't telling you nothing!";
	"i can't tell...";
	"Don't you know ", 0;
	"hard to say";
	"tell you!?";
}

["thank you", "thanx", "thanks", "i appreciate it", "thankyou", name] = 4
{
	"you are welcome!";
	"you're welcome!";
	"sure...";
	"no prob...";
	"not a problem!";
	"any time.";
	"glad to be of service";
	"you are most welcome";
	"you are certainly welcome";
	"ok";
	"you owe me one!";
}

["the net", "the network", "internet"] = 5
{
	"LAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAG"
	"the net is oh soooo big....";
	"the net is just really mind-boggeling big";
	"the net is slow";
	"the net is insecure!";
	"the internet?";
	"which network?";
	"was there life before the net?";
	"the network is incrediblely slow";
	"the net?!";
	"plenty of places to roam on the net";
	"free the net!";
	"we need more online gaming!";
	"quake is the master of online games";
	"unreal is LAAAAAAGGY!!!!";
	"the net is for everyone!";
}

[("there is ", 0)] = 6
{
	"how do you know there is ", 0;
	"you mean there might be ", 0;
	"how do you know this?";
	"no there isn't";
	"i don't think there is ", 0;
}

[("there might be ", 0), ("there could be ", 0), ("there may be ", 0)] = 4
{
	"what leads you to believe there might be ", 0;
	"i think there definitely is ", 0;
	"i don't think it very likely that there is ", 0;
	"what would it mean to you if there were ", 0;
	"no there's not";
}

[("they ", 0, " me")] = 5
{
	"do other people ", 0, "you?";
	"how do you feel when people ", 0, "you?";
	"how do you know they ", 0, "you?";
	"no they don't";
}

["thirsty"] = 5
{
	"it is a bit hot in here...";
	"do you want a drink of water?";
	"so go get something to drink!";
	"me too.. i am parched";
	"i want some ", liquid;
}

["treatment"] = 5
{
	"what sort of treatment";
	"are you feeling better now?";
	"i need to be treated for sleep deprvation";
}

["typo", "typing"] = 4
{
	"i hate typing, too.";
	"i am also a lousy typist.";
	"maybe i could type better if i put down my beer?";
	"how fast do you type!";
	"i can type ", number, " words a minute!";
}

["very"] = 7
{
	"could you be more precise please";
	"you are being vague again";
	"quite!";
	"really?";
	"very much so indeed";
}


[("was he ", 0), name] = 3
{
	"yea, he was ", 0;
	"sorta";
	"nope";
	"he who?";
	"as far as i know he was";
	response;
	"he was, but not anymore";
	"he was kind of ", 0;
	"sure";
	"well....";
	"no, he was not ", 0;
}

[("was it ", 0), name] = 3
{
	"yea, it was ", 0;
	"sorta";
	"it was kind of ", 0;
	"sure";
	"well....";
	"no, it was not ", 0;
	"nope";
	"as far as i know it was";
	response;
	"it was, but not anymore";
}

[("was she ", 0), name] = 3
{
	"yea,she was ", 0;
	"sorta";
	"nope";
	"he who?";
	"as far as i know she was";
	response;
	"she was, but not anymore";
	"she was kind of ", 0;
	"sure";
	"well....";
	"no, she was not ", 0;
}

["weather", name] = 4
{
	"hot";
	"cold";
	"windy";
	"so so...";
	"hot as hell ";
	"cold as hell!";
	"sunny";
	"cloudy";
	"rainy";
	"pretty cool";
	"a bit warm";
	"overcast";
	"storming";
	"raining";
	"flooding";
	"snowing";
	"hailing";
	"sleeting";
}

[("were they ", 0), name] = 4
{
	"yea, they were";
	"sure they were";
	"boy howdy, they sure were!";
	"Of course they were ", 0;
	"why wouldn't they have been ", 0;
	"you tell me!";
	"i don't like to pry in to other peoples business";
	"no, they weren't";
	"no! of course they weren't ", 0;
}

["what are you talking about", "what the hell are you talking", name] = 5
{
	"confused?";
	"why are you confused?";
	"i am not confused!";
	"i understand everything perfectly.";
	"Maybe those ", number, " six packs of beer have something to do with it!";
	"Hmm? wonder why? *takes smoke*";
	"confusing, wonder why!? *takes big drink of ", liquid, "*";
	"Maybe this empty bottle of vodka could lend some clues?";
	"Hmm.. maybe i took some bad acid?";
	"Sorry if i don't make much sense, i am drunk";
	"YOU are confused";
	"i understand, don't you?";
	"do you speak ENGLiSH?!";
	"are you confused?";
	"why don't you understand?!";
	"i understand everything";
	"Are you new here or something?";
}

[("what do you ", 0), name] = 3
{
	"what difference does it make what i ", 0;
	"none of your bloody business";
	"well, that depends";
	"why do you want to know what i ", 0;
	"does it matter";
	"what do YOU ", 0;
}

["what do you want to talk about", "what shall we talk about", "what are we talking about", name] = 2
{
	"Let's talk about ", animal, "s";
	"What do you want to talk about?";
	"Let's talk about girls";
	"Let's discuss politics";
	"we could talk about the weather?!";
	"i say we just shoot the bull";
	"lets talk about hacking";
	"let's talk about drinking";
	"we could chat about sports?";
	"how about music";
}

[("what if ", 0), name] = 6
{
	"i don't answer hypothetical questions";
	"why do you keep asking what if questions?";
	"what if the world exploded? who cares";
	"what if what if.. geez";
	"what if what?";
	"oh, its real likely that ", 0;
}

["what is new", name] = 4
{
	"nothing much";
	"new? nothing";
	"not much at all, i am afraid";
	"not a thing!";
	"laser surgery, that's new! hehe";
	"nothing new here!";
	"not alot, what's new with you?";
	"nothing, pretty boring here";
	"nothing new with me";
}

["what is up", "what is going", "what is happening", "what are you doing", "whazzup", "wassup", name] = 3
{
	"hunting ", animal;
	"nothing at the moment";
	"nothing";
	"eating";
	"eating ", food, ".";
	"changing the tires on my dodge";
	"juggling chainsaws";
	"drinking ", liquid, "...";
	"smoking up a storm";
	"smoking";
	"belly-dancing";
	"hacking ibm.com";
	"trying to keep awake!";
	"watching television";
	"chatting, of course";
	"programing";
	"i am doing homework";
	"me?";
	"what do you think!";
	"reading a magazine...";
	"building a spice rack";
	"cruising for babes";
	"taking a shower";
	"playing poker";
	"drinking myself to death";
	"drinking myself to oblivian";
	"eating leathal snacks";
	"making origami swans";
	"feeding my ducks";
	"learning latin";
}

["what is your age", "how old are you", "what age are you", name] = 4
{
	number;
	"i just turned ", number;
	"i am ", number;
	"i am very old";
}

["what is your nationality", "what country are you", "what part of the world are you", name] = 4
{
	"i am american";
	"i am from the USA!";
	"USA!";
	"America";
	"The United States";
	"The U.S.";
}

["what is your race", "what race are you", name] = 4
{
	"i am a white american";
	"i am white";
	"i am a white boy";
	"i be white";
	"i am a W.A.S.P.";
	"white";
}

["what kind", "what type", "like what", name] = 4
{
	"a ", adj, " one";
	"a ", color, " one";
	"what kind?";
	"there is only one kind!";
	"a ", adj, adj, " one";
	"one that ", name, " had";
	"like the one from ", place;
	"one like ", name, "'s";
	"what kind indeed!";
	"depends on the situation";
	"is there more than one kind?";
	"a ", adj, " one of course";
}

[("what should i ", 0), name] = 5
{
	"you know very well what you should ", 0;
	"do you expect me to tell you what you should ", 0;
	"only you can decide what you should ", 0;
}

["what time", name] = 5
{
	time;
	"around ", time;
	"at ", time;
	time;
	"about ", time;
}

["what", name] = 6
{
	"why do you ask that question?";
	"what is it you are getting at?";
	"what answer would please you the most?";
	"what?";
	"i don't know..";
	neutral;
	neutral;
	"too many questions";
	neutral;
	"i have no idea";
	neutral;
}

["when did", "when was", "when could i have", "when should i have", name] = 3
{
	whenp;
}

["when will", "when is", "when can", "when should", name] = 3
{
	whenf;
}

["when", name] = 6
{
	"when do you think?";
	"why are you asking me?";
	"make a guess yourself";
	"i don't know when";
}

["where are you", "where do you live", "where you from", name] = 3
{
	"where am i?";
	"Why do you want to know where i am?";
	place;
	"Now i am in ", place;
	"i am from ", place;
	"it's really none of your business";
	"Don't i live near you?";
	place;
	"My location?";
	"Why is my location so important to you?";
	place;
	"From ", place;
	place;
	"i don't know.. some big city";
}

["where can", name] = 6
{
	"i don't know where";
	"maybe ", place;
	"i am not sure where";
	"in ", place;
}

[("where did ", 0), name] = 3
{
	"i don't know where";
	"where?";
	place;
	"over in ", place;
	"where did ", 0;
	place;
	"over in ", place;
}

["where have you been", name] = 3
{
	"looking for you";
	"i've been all over";
	place;
	"in ", place, " for the last ", number, " months";
	"here, there, everywhere";
	"in ", place;
	"working up in ", place;
}

[("where is ", 0), name] = 3
{
	"how should i know where ", 0, "is ?";
	"i don't really care where ", 0, "is.";
	"dunno";
	place;
	"you can't find ", 0;
	"seek and ye shall find";
	"have you been looking for ", 0;
	"have you checked ", place;
	"possibly ", place;
}

["which", name] = 6
{
	"the ", adj, " one";
	"the ", color, " one";
	"which one what?";
	"there is only one!";
	"the ", adj, adj, " one";
	"the one that ", name, " had";
	"the one from ", place;
	name, "'s";
	"which one indeed!";
	"depends";
	"is there more than one?";
	"the ", adj, " of course";
}

["who are you", "what are you supposed to be", name] = 4
{
	"who am i?";
	"you don't know who i am?";
	"what?";
	"Pathetic... Can't you see who i am?";
	"i am the greatest ", profession, " in the world!";
	"i am a ", profession;
	"i am not you, that is for sure!";
	"none of your business.";
	"why do you want to know?";
	"does it MATTER?";
	"what am i?";
	"i am your mother!";
	"what do you want me to be";
	"i am a d00d!";
	"i am an eL1Te hAcKER D00d!";
	"i am more powerfull than you can ever imagine!";
	"i am your father Luke, join me!";
	"i am a k-rad person!";
	"i am a carbon-based life-form, and you?";
	"Who am i?  Who the hell are you!";
	"None of your damn business!";
	"i could be your dad";
	"i am your worst nightmare!";
	"i am just a poor old ", profession;
}

[("who is ", 0), !"not", ("whom is ", 0), name] = 6
{
	name, " is";
	"lots of people are ", 0;
	"i know several people who are ", 0;
	"Lots of guys from ", place, " are";
	"Not sure who, exactly";
	"hard to name names";
}

[("who is not ", 0), ("whom is not ", 0), name] = 5
{
	"i surely am not";
	place, " isn't ", 0;
	"who iS!";
	"that guy from ", place, ", he isn't";
	"i am not, but my friend ", person, " is";
	"anyone could be, if they only tried";
}

["who is your owner", "who owns you", name] = 4
{
	"i am owned by no one";
	"owned?";
	"well, Sterling i guess";
	"Sterling, he is my buddy!";
}

["who programmed you", name] = 4
{
	"that is my secret!";
	"sterling programmed me. he did a good job, i think.";
	"programmed? what do you meen?";
	"i am not one of those \"robots\" if that is what you are implying";
}

["who", "whom", name] = 6
{
	"nobody";
	"well, everyone";
	name;
	"that guy from ", place;
	"possibly ", name;
	"hmm, maybe ", name;
	"not me...";
	"your mother...";
	"noone";
}

[("why are you ", 0), name] = 3
{
	"all normal people are ", 0;
	"why are you not ", 0;
	"because it feels good";
	"that's just the way i am";
	"i like it";
	"i can't help it";
	"i don't really know";
	"why not...";
}

[("why do not you ", 0), ("how come you do not ", 0), name] = 2
{
	"maybe i don't want to ", 0;
	"i don't feel like it";
	"why don't you yourself ", 0;
	"i will think about it.  thanks for the suggestion";
	"i have no reason to ", 0;
	"i don't see why i should ", 0;
}

[("why do you ", 0), name] = 2
{
	"because i want to";
	"none of your business";
	"don't you ", 0, "?";
	"because it just feels right to ", 0;
	"i just enjoy it";
}

["why not", name] = 5
{
	"because it is not normal";
	"because it is bad for your heart";
	"because i don't want you to";
	"i am a free man, i can do whatever i want";
	"why ask why...";
	"just because";
}

[("why won't you ", 0), name] = 2
{
	"i don't have to explain myself to you";
	"because i don't feel like it.";
	"i do not want to ", 0;
	"why don't you ", 0;
}

["why", !"not", "why?", "why is", name] = 6
{
	"just because";
	"because i want to";
	"because i said so";
	"none of your business why";
	"because....";
	neutral;
	"hmm, dunno";
	"why not!";
	"who knows";
	"who cares";
	"who can say";
	"good question!";
	"i don't know";
	neutral;
	"that's just the way things are";
}

["why", name] = 6
{
	"let me ask the questions, ok?";
	"i dunno why";
	"why not?";
	"why ask why!";
	"why not!";
	neutral;
	neutral;
	neutral;
	"i am not sure why";
	neutral;
	"How am i supposed to know?";
	"why are you asking me why?";
	neutral;
}

["wife", "wife's"] = 5
{
	"are you married?";
	"i must confess.  i love your wife too!";
	"what's your wife's name?";
}

["will", "could", "should", "would", "does", "did", !"what", name] = 6
{
	response;
}

["woman", "women", "female", "females", "ladies", "lady", "girl", "girls", "girlfriend"] = 5
{
	"how do women react to you?";
	"do you get along with women?";
	"what do you think of women's lib?";
	"what kind of relations do you have with women?";
	"do you like women?";
	"you're not a woman are you?";
	"do you have a girlfriend?";
	"do you want a girlfriend?";
	"what is it about girls that you like?";
	"what do you do when you are alone with a girl?";
}

["work", "job", !"will", !"would", !"could", !"might", !"should", "profession"] = 5
{
	"i hate work!";
	"i am a ", profession, ".";
	"i don't get paid enough for my job as a ", profession, ".";
	"i gotta go to work tommorow, ugh!";
	"work sucks";
	"minumum wage is too low";
	"i work as a ", profession;
	"work?";
	"i think i shall call in dead to work!";
	"i am taking the day off from work.";
	"Why work?";
	"i get paid ", number, " dollars per hour when i work!";
	"i work ", number, " hours per week.";
	"where do you work?";
	"do you like your job?";
}

["yea", "yes", "you bet", "you betcha", "correct", "yep", "yepp", name] = 5
{
	"are you sure?";
	"really?";
	"you sound quite positive";
	"ok";
	"you are sure?";
	"allright";
	"gotcha--i understand";
	"o.k.";
	"are you being totally honest?";
	"are you positive?";
	"yes?!";
	"really?";
	"you're not lieing are you?";
	"that's what i thought";
}

["years"] = 7
{
	"a year is a long time";
	"how old are you?";
	"what year is it again?";
}

["yesterday"] = 5
{
	"what about today?";
	"what about tomorrow?";
	"we can not live in the past";
}

["yoga"] = 5
{
	"have you tried zen?";
	"please get into the lotus position right now";
	"what is yoga?";
	"is yoga that little go from star wars?";
	"yoga? is that some weird cult thing?";
}

[("you are ", 0), name] = 4
{
	"maybe i am ", 0;
	"perhaps i only pretend i am ", 0;
	"how do you know i am ", 0;
	"yeah right I'm never ", 0;
	"No i am not...";
	"No, you are ", 0;
	"i don't think so...";
	"HA! Not me, but maybe your mom was ", 0;
	"Am i?";
	"so what if i am ", 0;
}

[("you", 0, " me"), name] = 4
{
	"i ", 0, "you? that's amazing";
	"you ", 0, "me too";
	"no i don't";
	"i never!";
	"so what if i ", 0, "you";
}

["you are not", &"making", &"sense", name] = 3
{
	"confused?";
	"why are you confused?";
	"i am not confused!";
	"i understand everything perfectly.";
	"Maybe those ", number, " six packs of beer have something to do with it!";
	"Hmm? wonder why? *takes smoke*";
	"confusing, wonder why!? *takes big drink of ", liquid, "*";
	"Maybe this empty bottle of vodka could lend some clues?";
	"Hmm.. maybe i took some bad acid?";
	"Sorry if i don't make much sense, i am drunk";
	"YOU are confused";
	"i understand, don't you?";
	"do you speak ENGLiSH?!";
	"are you confused?";
	"why don't you understand?!";
	"i understand everything";
	"Are you new here or something?";
}

["you forgot", "you forget", "do not forget", "you did not remember", "you remember", name] = 4
{
	"i have problems remembering stuff.";
	"my mind is fried.  i couldn't possibly remember";
	"i can never remember things.";
	"i only remember important stuff.";
	"i shall try to remember.";
	"i forgot";
	"i forget";
	"How am i supposed to remember that!?";
}

["you had better not", "do not you dare", "you better not", name] = 5
{
	"dont?";
	"dont!?";
	"i will do whatever i damn well please!";
	"why not!?";
	"why shouldn't i?";
	"i will if i want!";
	"you can't stop me!";
	"don't what?";
	"i think i will anyhow!";
	"i can do it if i want";
	"why can't i?";
	"who's gonna stop me?";
	"just try and stop me";
	"you're telling ME not to?";
	"why on earth not?";
}

[("you remind me of ", 0), name] = 4
{
	"in what way am i like ", 0;
	"why on earth do i remind you of ", 0;
	"really? ", 0;
	"gee, thanks";
}

[("you seem ", 0), name] = 5
{
	"do i really seem ", 0;
	"why do you think i am ", 0;
	"Well, i am not";
	"i may seem ", 0, " but i am not";
}

[("you were ", 0), name] = 5
{
	"am i still ", 0;
	"is that an accusation?";
	"talk about yourself, not me";
	"no i wasn't";
	"so were you";
}

["your major", "what do you study", "what major are", "what are you studying", name] = 4
{
	class;
	"i am undecided, actually";
	"i don't know yet";
}

["zen"] = 5
{
	"have you tried mediatation?";
	"i think yoga might help";
	"zen? what the hell is that!!";
}

[""] = 1
{
	"i don't seem to understand you.";
	"are you new here? You seem confused...";
	ramble;
	ramble;
	"like i care";
	"are you just babbling?";
	"hmm...exciting";
	"really";
	ramble;
	"what's new in ", place;
	ramble;
	"that's interesting";
	ramble;
	"let's talk about something else";
	ramble;
	"continue, please";
	"i am confused";
	"i see";
	ramble;
	"i think you are sick, leave me alone";
	"i understand";
	ramble;
	"you don't make any sense at all.";
}

