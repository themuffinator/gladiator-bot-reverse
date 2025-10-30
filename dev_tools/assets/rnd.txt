//===========================================================================
//
// name:				rnd.c
// function:		random strings
// programmer:		mr elusive (mrelusive@demigod.demon.nl)
// last update:	1999-02-10
// tab size:		3 (real tabs)
// notes:			-
//===========================================================================

#include "game.h"

#define NEGATIVE \
"no",\
"nope",\
"no way",\
"no!",\
"hell no",\
"of course not",\
"i do not think so..",\
"homey don't play that",\
"sure -not!-",\
"no",\
"negative",\
"heck no!",\
"i think not",\
"no way jose",\
"no"

#define AFFIRMATIVE \
"yes",\
"yep",\
"you bet",\
"uh-hu",\
"of course!",\
"you-betcha",\
"darn tootin",\
"affirmative",\
"10-4 good buddy!",\
"correct",\
"yeah",\
"that is right!",\
"you got it!",\
"sure",\
"precisely right",\
"that sounds right",\
"exactly!",\
"you know it!",\
"yeppers"

#define NEUTRAL \
"what are you talking about?",\
"geez",\
"ick!",\
"wait a sec... what?",\
"i don't understand",\
"dunno",\
"not sure",\
"let me think",\
"hmm",\
":-)",\
":(",\
"what do you mean?",\
"am i supposed to tell you?",\
"how should i know",\
"i could tell you, but i would have to kill you",\
"i will never tell!",\
"i have no idea!",\
"do not ask me",\
"i am not gonna tell you!",\
"i know nothing about that!",\
"that information is top secret, sorry",\
"can't say",\
"search me!",\
"what?!",\
"hugh?",\
"i don't know that!",\
"sorry, dunno!",\
"i am clueless!",\
"i am too sleepy to think straight",\
"i dunno man",\
"that's for me to know and you to find out",\
"*confused*",\
"i am not knowing",\
"i really don't know",\
"no idea",\
"haven't the foggiest"

#define RAMBLE \
":-(",\
":-)",\
"i am falling asleep",\
"i am getting sick.. ughh",\
"*vomit*",\
"i am bored",\
"i am really tired",\
"i hate life",\
"i just ask for belonging..",\
"what is everyone talking about?",\
"whoops",\
"yawn",\
"zzzzzzzzzzzz",\
"oh..",\
"no doubt!",\
"i am being repressed by de man!",\
"i see..",\
"i think i am getting a cold... ",\
"ahh",\
"anyone else smell smoke?",\
"anyone here actually have any intelligence?",\
"explain further",\
"facinating..",\
"go on..",\
"great..",\
"excellent",\
"hahaha",\
"hehehe",\
"shocking!",\
"grr...",\
"pfft! ",\
"*snif*",\
"*grin*",\
"hmm",\
"hmm..",\
"ho hum..",\
"la de dah..",\
"really",\
"school sucks",\
"so what shall we talk about?",\
"that's nice",\
"tsk tsk tsk",\
"*sigh*",\
"oh?",\
"aaaaaah"

#define ITEM \
"nui",\
"pad",\
"car",\
"computer",\
"hard drive",\
"floppy drive",\
"telephone",\
"new car",\
"bigger house",\
"bucket of money",\
"gold coin",\
"dog",\
"post-it-note",\
"2x3 flat sawed board",\
"timex sinclair",\
"pressure gauge",\
"vhs copy of \"ishtar\"",\
"joystick",\
"bongo set",\
"can opener"

#define PLACE \
"venus",\
"the 3rd planet from the sun",\
"the ghetto, man!",\
"the wetlands of florida",\
"endor",\
"the area behind fenway park\'s bleachers",\
"romulus",\
"vulcan",\
"alabama",\
"alaska",\
"birmingham",\
"newcastle",\
"nottingham",\
"deurne",\
"st.niklaas",\
"belgium",\
"france",\
"toulouse",\
"paris",\
"st.etienne",\
"benidorm",\
"blaness",\
"helmond",\
"twente",\
"spain",\
"barcelona",\
"madrid",\
"outer space",\
"eldorado",\
"caracas",\
"willemstad",\
"curacoa",\
"teteringen",\
"leiden",\
"in casa",\
"bonaire",\
"amsterdam",\
"arizona",\
"arkansas",\
"california",\
"hawaii",\
"greece",\
"the netherlands",\
"australia",\
"brisbane",\
"berlin",\
"42nd street",\
"zimbabwai",\
"lesotho",\
"the midwest",\
"new england",\
"london",\
"geneva",\
"rotterdam",\
"bonn",\
"dusseldorf",\
"maastricht",\
"oslo",\
"bergen",\
"sandness",\
"sweden",\
"norge",\
"jakarta",\
"jawa",\
"indonesia",\
"makassar",\
"tierra del fuego",\
"hazerswoude",\
"the high plains",\
"jellystone park",\
"the rocky mountains",\
"the pacific northwest",\
"the planet \"uranus\"",\
"zoetermeer"

#define CLASS \
"math",\
"science",\
"geology",\
"calculus",\
"computer science",\
"operating systems",\
"ai",\
"history",\
"political science",\
"physics",\
"economics",\
"chemistry",\
"biology",\
"anthropology",\
"elementary education",\
"nuke engineering",\
"pascal",\
"electrical engineering",\
"sociology",\
"material science",\
"management",\
"accounting",\
"marketing"

#define LIQUID \
"coke",\
"dr. pepper",\
"heineken",\
"amstel",\
"duvel",\
"palm beer",\
"brand",\
"hoegaarden",\
"wiekse witte",\
"bitter lemon",\
"oranjeboom",\
"grolsch",\
"water",\
"wild turkey",\
"johny walker",\
"rum",\
"malibu",\
"malibu-coke",\
"passoa",\
"passoa-oj",\
"kool-aid",\
"wine",\
"gasoline",\
"rain water",\
"iced tea",\
"guinness stout",\
"fosters",\
"sheaf stout",\
"pond water",\
"cognac",\
"vodka",\
"smirnoff",\
"mad dog",\
"thunderbird",\
"wine coolers"

#define NUMBER \
"0",\
"a billion",\
"a trillion",\
"a million",\
"billions and billions of",\
"1",\
"2",\
"3",\
"4",\
"5",\
"6",\
"7",\
"8",\
"9",\
"10",\
"100",\
"1500",\
"13",\
"234",\
"845",\
"23",\
"42",\
"around 11",\
"like 2 or 3",\
"like 8 or 9",\
"a shitload of",\
"a buttload of",\
"a bunch of",\
"fifty",\
"sixty",\
"seventy",\
"eighty",\
"a hundred",\
"a billion",\
"a bizillion",\
"a trillion"

#define PROFESSION \
"plumber",\
"brick layer",\
"drug dealer",\
"pre-school teacher",\
"mcdonald\'s manager",\
"hacker",\
"phreaker",\
"phone-man",\
"bank robber",\
"grease monkey",\
"programmer",\
"drinker",\
"engineer",\
"salesman",\
"sacker",\
"stocker",\
"prostitute",\
"preacher",\
"cook",\
"dancer",\
"dish washer",\
"accountant",\
"student",\
"system administrator",\
"pizza boy",\
"psychopath",\
"musician"

#define NAME \
"netw1z",\
"sterling",\
"zorgo",\
"phz",\
"kl",\
"lod",\
"the fbi",\
"the cia",\
"the kgb",\
"president clinton",\
"nia",\
"ninjam",\
"pc",\
"scorpio",\
"sirlance",\
"stone temple pilots",\
"the phrack crew",\
"the mexicans",\
"the aussies",\
"the dutch",\
"the limeys",\
"the telephone company",\
"the cubans",\
"the iraqies",\
"the libyans",\
"darth vader",\
"eek the cat",\
"daisy cutter",\
"doc tari",\
"sprint security",\
"bo diddley squatt",\
"sharky the shark dog",\
"dan quayle",\
"demigod",\
"barbara bush",\
"saddam hussein"

#define ONE_LINERS \
"\"breakfast sometime?\"  \"sure.\"  \"shall i call you, or just nudge you?\"",\
"\"define universe; give two examples.\" \"the perceived world; 1) mine, 2) yours.\"",\
"\"have you lived here all your life?\"  \"oh, twice that long.\"",\
"...all the modern inconveniences..",\
"28.35 grams of prevention are worth 0.45359 kilograms of cure",\
"355/113 -- not the famous irrational number pi, but an incredible simulation",\
"a backscratcher will always find new itches",\
"a billion here, a billion there; soon you\'re talking real money",\
"a bird in the bush usually has a friend in there with him",\
"a bird in the hand is safer than one overhead",\
"a bird in the hand makes it hard to blow your nose",\
"a boss with no humor is like a job that\'s no fun",\
"a cauliflower is nothing but cabbage with a college education",\
"a celebrity is a person who is known for his well-knownness",\
"a clean tie attracts the soup of the day",\
"a committee is an animal with at least six legs, and no brain",\
"a conclusion is the place where you got tired of thinking",\
"a couple of months in the lab can often save a couple of hours in the library",\
"a crisis is when you can\'t say, \"let\'s just forget the whole thing.\"",\
"a day without fusion is like a day without sunshine",\
"a day without orange juice is like a day without orange juice",\
"a day without sunshine is like night",\
"a dean is to a faculty as a hydrant is to a dog",\
"a fail-safe circuit will destroy others",\
"a fool and his money stabilize the economy",\
"a general leading the state department resembles a dragon commanding ducks",\
"a good scapegoat is hard to find",\
"a harp is a nude piano",\
"a helicopter is just a bunch of parts flying in close formation",\
"a home where the buffalo roam...  is messy",\
"a homeowner\'s reach should exceed her grasp, or what\'s a weekend for?",\
"a journey of a thousand miles begins with a cash advance from mom",\
"a kid\'ll eat the middle of an oreo, eventually",\
"a king\'s castle is his home",\
"a lack of leadership is no substitute for inaction",\
"a lot of people are afraid of heights.  not me, i\'m afraid of widths",\
"a man who fishes for marlin in ponds will put his money in etruscan bonds",\
"a man who turns green has eschewed protein",\
"a man without a woman is like a fish without gills",\
"a motion to adjourn is always in order",\
"a pat on the back is only a few centimeters from a kick in the pants",\
"a penny saved has not been spent",\
"a penny saved is an economic breakthrough",\
"a penny saved is ridiculous",\
"a pessimist is a married optimist",\
"a poet who reads his verse in public might have other nasty habits",\
"a quarter ounce of chocolate equals four pounds of fat",\
"a renaissance man diffuses to refine himself",\
"a rolling stone gathers momentum",\
"a sadist is a masochist who follows the golden rule",\
"a sentence is worth a thousand words",\
"a sine curve goes off to infinity, or at least the end of the blackboard",\
"a sinking ship gathers no moss",\
"a small town that cannot support one lawyer can always support two",\
"a smith & wesson beats four aces",\
"a soft drink turneth away company",\
"a student who changes the course of history is probably taking an exam",\
"a successful american spends more supporting the government than a family",\
"a theorist right once in ten is a hero; an observer wrong that often is a bum",\
"a theory is better than its explanation",\
"a truly wise person never plays leapfrog with a unicorn",\
"a university without students is like an ointment without a fly",\
"a verbal contract isn\'t worth the paper its printed on",\
"a virtuoso is a musician with real high morals",\
"a waist is a terrible thing to mind",\
"a watched clock never boils",\
"a wedding is a funeral where a man smells his own flowers",\
"a young child is a noise with dirt on it",\
"abandon the search for truth; settle for a good fantasy",\
"about all some men accomplish in life is to send a son to harvard",\
"about the only thing on a farm that has an easy time is the dog",\
"about when we think we can make ends meet, somebody moves the ends",\
"absence makes the heart go wander",\
"absence makes the heart grow fonder...  for someone else",\
"absolutum obsoletum.  (if it works, it is out of date.)",\
"academy:  a modern school where football is taught",\
"accident:  when presence of mind is good, but absence of body is better",\
"according to the official figures, 43% of all statistics are totally worthless",\
"acting:  an art that consists of keeping the audience from coughing",\
"actors will happen in the best-regulated families",\
"admiration:  our polite recognition of another\'s resemblance to ourselves",\
"adult:  a person that has stopped growing at both ends but not in the middle",\
"adult:  one old enough to know better",\
"adultery:  putting yourself in someone else\'s position",\
"advanced design:  upper management doesn\'t understand it",\
"adventure is a sign of incompetence",\
"after all is said and done, a hell of a lot more is said than done",\
"after painting the town red, take a rest before applying a second coat",\
"afterism:  a concise, clever statement you don\'t think of until too late",\
"afternoon very favorable for romance.  try a single person for a change",\
"age and treachery will always overcome youth and skill",\
"aiming for the least common denominator sometimes causes division by zero",\
"air is water with holes in it",\
"air travel:  breakfast in london, dinner in new york, luggage in brazil",\
"alcoholic:  someone you don\'t like who drinks as much as you do",\
"aleph-null bottles of beer on the wall, aleph-null bottles of beer..",\
"alimony and bribes will engage a large share of your wealth",\
"alimony is a splitting headache",\
"alimony is the high cost of leaving",\
"all generalizations are useless, including this one",\
"all i ask is the chance to prove that money cannot make me happy",\
"all i want is a warm bed and a kind word and unlimited power",\
"all my friends and i are crazy.  that\'s the only thing that keeps us sane",\
"all new:  parts not interchangeable with previous model",\
"all people are born alike -- except republicans and democrats",\
"all probabilities are really 50%.  either a thing will happen or it won\'t",\
"all signs in metric for the next 20 miles",\
"all that glitters has a high refractive index",\
"all the good ones are taken",\
"all the men on my staff can type",\
"all things are possible, except skiing through a revolving door",\
"all trails have more uphill sections than they have downhill sections",\
"all true wisdom is found on t-shirts",\
"always borrow money from a pessimist; he doesn\'t expect to be paid back",\
"always leave room to add an explanation if it doesn\'t work out",\
"always remember that you are unique...  just like everyone else",\
"always take both skis off before hanging them up",\
"am i in charge here?...  no, but i\'m full of ideas",\
"ambiguity:  telling the truth when you don\'t mean to",\
"ambition is a poor excuse for not having sense enough to be lazy",\
"amnesia used to be my favorite word, but then i forgot it",\
"amoebit:  amoeba/rabbit cross; it can multiply and divide at the same time",\
"among economists, the real world is often a special case",\
"an apple a day keeps the doctor away...  if it is aimed well",\
"an apple a day makes 365 apples a year",\
"an apple every eight hours keeps three doctors away",\
"an effective way to deal with predators is to taste terrible",\
"an example of hard water is ice",\
"an idle mind is worth two in the bush",\
"an informed citizen panics more intelligently",\
"an object at rest will always be in the wrong place",\
"an object in motion will always be headed in the wrong direction",\
"an unbreakable toy is useful for breaking other toys",\
"antonym:  the opposite of the word you are trying to think of",\
"any country with \"democratic\" in the title isn\'t",\
"any landing you can walk away from is a good one",\
"any simple idea will be worded in the most complicated way",\
"any smoothly functioning technology is indistinguishable from a \"rigged\" demo",\
"any technology distinguishable from magic is insufficiently advanced",\
"any two philosophers can tell each other all they know in two hours",\
"anybody can win, unless there happens to be a second entry",\
"anyone can admit they were wrong; the true test is admitting it to someone else",\
"anyone can make an omelet with eggs.  the trick is to make one with none",\
"anyone who goes to a psychiatrist ought to have his head examined",\
"anyone who makes an absolute statement is a fool",\
"anything good in life is either illegal, immoral, or fattening",\
"apart from the unknowns, everything is obvious",\
"appearances are not everything; it just looks like they are",\
"aquadextrous:  able to turn the bathtub faucet on and off with your toes",\
"archeology is the only profession where your future lies in ruins",\
"arguments with furniture are rarely productive",\
"arithmetic:  counting to twenty without taking off your shoes",\
"art is anything you can get away with",\
"artery:  study of paintings",\
"as god is my witness, andy, i thought that turkeys could fly",\
"as long as the answer is right, who cares if the question is wrong?",\
"ask not for whom the bell tolls, and pay only station-to-station rates",\
"at these prices, i lose money -- but i make it up in volume",\
"atheists are beyond belief",\
"atheists are people with no invisible means of support",\
"auditors always reject expense accounts with a bottom line divisible by five",\
"authority:  a person who can tell you more than you really care to know",\
"automobile:  a four-wheeled vehicle that runs up hills and down pedestrians",\
"babies can\'t walk because their legs aren\'t long enough to reach the ground",\
"bachelor:  a guy who is footloose and fiancee-free",\
"bachelor:  a man who never made the same mistake once",\
"bachelor:  a selfish guy who has cheated some woman out of a divorce",\
"ban the bomb.  save the world for conventional warfare",\
"banectomy:  the removal of bruises on a banana",\
"barium:  what doctors do when treatment fails",\
"baseball is to football as beethoven is to rap",\
"be a better psychiatrist and the world will beat a psychopath to your door",\
"be careful of reading health books; you might die of a misprint",\
"be content with what you\'ve got, but be sure you\'ve got plenty",\
"beam me up, scotty.  there\'s no intelligent life down here",\
"begathon:  a multi-day event on public television, used to raise money",\
"behaviorism is the art of pulling habits out of rats",\
"behold the warranty:  the bold print giveth, and the fine print taketh away",\
"being a good communicator means people find out what\'s really wrong with you",\
"being a woman is quite difficult since it consists mainly of dealing with men",\
"being popular is important.  otherwise people might not like you",\
"benjamin franklin produced electricity by rubbing cats backwards",\
"best gift for the person who has everything:  a burglar alarm",\
"between two evils, i always pick the one i never tried before",\
"beware of a dark-haired man with a loud tie",\
"beware of a tall dark man with a spoon up his nose",\
"bigamy is having one spouse too many.  monogamy is the same",\
"biology grows on you",\
"bisexuality immediately doubles your chances for a date on saturday night",\
"blessed are the meek for they shall inhibit the earth",\
"blessed are the young for they shall inherit the national debt",\
"blessed are they that run around in circles, for they shall be known as wheels",\
"bore:  a person who talks when you wish him to listen",\
"bore:  he who talks so much about himself that you can\'t talk about yourself",\
"bore:  wraps up a two-minute idea in a two-hour vocabulary",\
"brad, where tad had had \"had had\", had had \"had\".  \"had had\" had had me glad",\
"brain:  the apparatus with which we think that we think",\
"bride:  a woman with a fine prospect of happiness behind her",\
"brigands ask for your money or your life; spouses require them both",\
"broad-mindedness:  the result of flattening high-mindedness out",\
"budget:  a method of worrying before you spend money, as well as afterward",\
"bureaucracy:  a method of transforming energy into solid waste",\
"bureaucrat:  a person who cuts red tape sideways",\
"bureaucrat:  a politician with tenure",\
"business will be either better or worse",\
"but enough about me.  let\'s talk about you.  what do you think of me?",\
"but officer, i stopped for the last one, and it was green!",\
"by the time you have the right answers, no one is asking you questions",\
"calling a person a runner-up is a polite way of saying they lost",\
"can you think of another word for \"synonym\"?",\
"capitalism is based on the assumption that you can win",\
"cauterize:  made eye contact with a woman",\
"charity:  a thing that begins at home and usually stays there",\
"charm:  a way of getting a \"yes\" -- without having askwd any clear question",\
"chastity:  the most unnatural of the sexual perversions",\
"cheap:  much less expensive than ones selling for up to twice as much",\
"chemicals:  noxious substances from which modern foods are made",\
"children act like their parents despite every effort to teach them good manners",\
"cinemuck:  popcorn, soda, and candy that covers the floors of movie theaters",\
"circle:  a line that meets its other end without ending",\
"cloning is the sincerest form of flattery",\
"cogito ergo spud (i think, therefore i yam)",\
"college:  the fountains of knowledge, where everyone goes to drink",\
"colorless green ideas sleep furiously",\
"comedy is simply a funny way of being serious",\
"committee:  a group of men who keep minutes and waste hours",\
"committee:  the unwilling, selected from the unfit, to do the unnecessary",\
"common sense:  the collection of prejudices acquired by age 18",\
"concept:  any \"idea\" for which an outside consultant bills more than $25,000",\
"confidence:  the feeling you have before you understand the situation",\
"confound those who have said our remarks before us",\
"confucius say too much",\
"congress is not the sole suppository of wisdom",\
"conscience is a mother-in-law whose visit never ends",\
"conscience:  the inner voice that warns us somebody may be looking",\
"conscience:  the thing that hurts when everything else feels great",\
"conscious is being aware of something; conscience is wishing you weren\'t",\
"conservative:  a liberal who has just been mugged",\
"conservative:  a person who believes nothing should be done for the first time",\
"conservative:  one who is too cowardly to fight and too fat to run",\
"consider what might be fertilizing the greener grass across the fence",\
"consultant:  someone who knowns 101 ways to make love, but can\'t get a date",\
"consultation:  medical term meaning \"to share the wealth.\"",\
"continental life.  why do you ask?",\
"contraceptives should be used on every conceivable occasion",\
"could you be a poster child for retroactive birth control?",\
"courage:  two cannibals having oral sex",\
"coward:  one who in a perilous emergency thinks with his legs",\
"crazee edeee, his prices are insane!!!",\
"crime does not pay...  as well as politics",\
"cross country skiing is great if you live in a small country",\
"cynic:  a person searching for an honest man, with a stolen lantern",\
"cynic:  a person who tells you the truth about your own motives",\
"dare to be average",\
"dark dirt is attracted to light objects, and dark dirt to light objects",\
"death and taxes are inevitable; at least death doesn\'t get worse every year",\
"death has been proven to be 99% fatal to laboratory rats",\
"death is god\'s way of telling you not to be such a wise guy",\
"death is life\'s way of telling you you\'ve been fired",\
"death is the greatest kick of all.  that\'s why they save it for last",\
"death:  to stop sinning suddenly",\
"deliberation:  examining one\'s bread to determine which side it is buttered on",\
"democracy:  the worship of jackals by jackasses",\
"dentists are incapable of asking questions that need a simple yes or no answer",\
"design simplicity:  developed on a shoe-string budget",\
"dew is formed on leaves when the sun shines on them and makes them perspire",\
"dinner is ready when the smoke alarm goes off",\
"diplomacy:  the art of letting someone else have your way",\
"diplomacy:  the art of saying \"nice doggy\" until you can find a rock",\
"diplomat:  a man who can convince his wife she would look stout in a fur coat",\
"disco is to music what etch-a-sketch is to art",\
"distinctive:  a different color or shape than our competitors",\
"divorce:  having your genitals torn off through your wallet",\
"do married people live longer, or does it just seem that way?",\
"do not merely believe in miracles; rely on them",\
"do not underestimate the power of the force",\
"do you have redeeming social value?",\
"does the name \"pavlov\" ring a bell?",\
"don\'t be fooled by his twinkling eyes; it\'s the sun shining between his ears",\
"don\'t be humble...  you\'re not that great",\
"don\'t create a problem for which you do not have the answer",\
"don\'t eat the yellow snow",\
"don\'t force it, get a larger hammer",\
"don\'t get even -- get odd!",\
"don\'t get stuck in a closet; wear yourself out",\
"don\'t give someone a piece of your mind unless you can afford it",\
"don\'t lend people money...  it gives them amnesia",\
"don\'t marry for money; you can borrow it cheaper",\
"don\'t mind him; politicians always sound like that",\
"don\'t say yes until i finish talking",\
"don\'t steal.  the government hates competition",\
"don\'t sweat the petty things -- just pet the sweaty things",\
"don\'t take life too seriously; you won\'t get out of it alive",\
"don\'t tax you, don\'t tax me, tax that fellow behind the tree",\
"don\'t undertake vast projects with half-vast ideas",\
"don\'t use no double negatives, not never",\
"don\'t worry; the brontosaurus is slow, stupid, and placid",\
"don\'t you have anything more useful you could be doing?",\
"down with the categorical imperative!",\
"drive carefully.  we are overstocked.  -- sign in junkyard",\
"driving in the snow is a spectator sport",\
"drug:  a substance that, when injected into a rat, produces a scientific paper",\
"drugs are the scenic route to nowhere",\
"ducks?  what ducks??",\
"due to a mixup in urology, orange juice will not be served this morning",\
"dying is easy.  comedy is difficult",\
"early to rise and early to bed makes a man healthy and wealthy and dead",\
"earth destroyed by solar flare -- film at eleven",\
"earth is a great funhouse without the fun",\
"easiest way to figure the cost of living:  take your income and add ten percent",\
"eat a live toad in the morning and nothing worse will happen to you that day",\
"eat drink and be merry, for tomorrow it might be illegal",\
"education helps earning capacity.  ask any college professor",\
"eeny, meeny, jelly beanie, the spirits are about to speak..",\
"egotism:  doing a crossword puzzle with a pen",\
"eighty percent of all people consider themselves to be above average drivers",\
"either i\'m dead or my watch has stopped",\
"either that wallpaper goes, or i do",\
"elbonics:  two people maneuvering for one armrest in a movie theater",\
"elections come and go, but politics are always with us",\
"electricity comes from electrons; morality comes from morons",\
"eliminate government waste, no matter how much it costs!",\
"eloquence is logic on fire",\
"emulate your heros, but don\'t carry it too far.  especially if they are dead",\
"engineers...  they love to change things",\
"enjoy life; you could have been a barnacle",\
"eschew obfuscation",\
"eternal nothingness is fine if you happen to be dressed for it",\
"every cloud has a silver lining; you should have sold it, and bought titanium",\
"every journalist has a novel in him, which is an excellent place for it",\
"every silver lining has a cloud around it",\
"everybody lies, but it doesn\'t matter since nobody listens",\
"everyone else my age is an adult, whereas i am merely in disguise",\
"everyone is a genius.  it is just that some people are too stupid to realize it",\
"everyone needs belief in something.  i believe i\'ll have another beer",\
"everything in moderation, including moderation",\
"everything is actually everything else, just recycled",\
"everything is always done for the wrong reasons",\
"everything put together falls apart sooner or later",\
"everything worthwhile is mandatory, prohibited, or taxed",\
"everything you know is wrong, but you can be straightened out",\
"excellent day to have a rotten day",\
"exceptions always outnumber rules",\
"exceptions prove the rule, and wreck the budget",\
"exclusive:  we are the only ones who have the documentation",\
"executive ability:  deciding quickly and getting somebody else to do the work",\
"exercise extends your life ten years, but you spend 15 of them doing it",\
"experience is directly proportional to the amount of equipment ruined",\
"experience is something you don\'t get until just after you need it",\
"experience is what causes a person to make new mistakes instead of old ones",\
"experience is what you get when you were expecting something else",\
"experiments should be reproducible.  they should all fail the same way",\
"expert:  avoids the small errors while sweeping on to the grand fallacy",\
"f u cn rd ths, itn tyg h myxbl cd",\
"familiarity breeds attempt",\
"familiarity breeds children",\
"famous last words:  don\'t worry, i can handle it",\
"fanatic:  someone who, having lost sight of his goal, redoubles his efforts",\
"fashion:  a form of ugliness so intolerable that it changes every six months",\
"fast, cheap, good:  choose any two",\
"federal reserve:  a reserve where federal employees hunt wild game",\
"fenderberg:  deposit that forms on the inside of a car fender after a snowstorm",\
"fidelity:  a virtue peculiar to those who are about to be betrayed",\
"field tested:  manufacturing doesn\'t have a test system",\
"fill what\'s empty; empty what\'s full; scratch where it itches",\
"fine day for friends.  so-so day for you",\
"five is a sufficiently close approximation to infinity",\
"flying is the second greatest experience known to man.  landing is the first",\
"foolproof operation:  no provision for adjustment",\
"fools rush in -- and get the best seats in the house",\
"football, like religion, brings out the best in people",\
"for a good time, call 555-3100",\
"for adult education, nothing beats children",\
"for back-country preparedness, \"what if\" weighs about 20 pounds",\
"for every action, there is a corresponding over-reaction",\
"for every action, there is an equal and opposite criticism",\
"for every action, there is an equal and opposite government program",\
"for every knee, there is a jerk",\
"for some reason, this statement reminds everyone of marvin Zelkowitz",\
"for those who like this sort of thing, this is the sort of thing they like",\
"form follows function, and often obliterates it",\
"fortune favors the lucky",\
"fossil flowers come from the petrified florist",\
"four kinds of homicide:  felonious, excusable, justifiable, and praiseworthy..",\
"four wheel drive:  lets you get more stuck, further from help",\
"freedom is just chaos, with better lighting",\
"friends:  people who borrow my books and set wet glasses on them",\
"friends:  people who know you well, but like you anyway",\
"furbling:  walking a maze of ropes even when you are the only person in line",\
"genderplex:  trying to determine from the cutesy pictures which restroom to use",\
"generally you don\'t see that kind of behavior in a major appliance",\
"genetics:  why you look like your father, or if you don\'t, why you should",\
"genius is the infinite capacity for picking brains",\
"genius:  a chemist who discovers a laundry additive that rhymes with \"bright\"",\
"gentleman:  knows how to play the bagpipes, but doesn\'t",\
"give a skeptic an inch and he\'ll measure it",\
"give me a lever long enough, and a place to stand, and i\'ll break my lever",\
"give me a sleeping pill and tell me your troubles",\
"give me chastity and continence, but not just now",\
"give your very best today.  heaven knows it is little enough",\
"giving away baby clothes and furniture is the major cause of pregnancy",\
"gleemites:  petrified deposits of toothpaste found in sinks",\
"go away.  i\'m all right",\
"go directly to jail.  do not pass go, do not collect $200",\
"go to heaven for the climate but hell for the company",\
"god don\'t make mistakes.  that\'s how he got to be god",\
"god gives us relatives; thank goodness we can chose our friends",\
"god is a polythiest",\
"god is not dead.  he is alive and autographing bibles at cody\'s!",\
"god is not dead.  he is alive and working on a much less ambitious project",\
"god is not dead.  he just couldn\'t find a parking place",\
"god made everything out of nothing, but the nothingness shows through",\
"god made the world in six days, and was arrested on the seventh",\
"god, i ask for patience -- and i want it right now!",\
"going the speed of light is bad for your age",\
"good advice is something a man gives when he is too old to set a bad example",\
"good day for a change of scene.  repaper the bedroom wall",\
"good sopranos and tenors have resonance -- where others have brains",\
"good-bye.  i am leaving because i am bored",\
"government expands to absorb all available revenue and then some",\
"graft:  an illegal means of uniting trees to make money",\
"grasshoppotamus:  a creature that can leap to tremendous heights...  once",\
"gravity:  what you get when you eat too much and too fast",\
"great minds run in great circles",\
"group iq:  lowest iq of any member divided by the number of people in the group",\
"grub first, then ethics",\
"had there been an actual emergency, you would no longer be here",\
"hailing frequencies open, captain",\
"handel was half german, half italian, and half english.  he was rather large",\
"hangover:  the wrath of grapes",\
"happiness is having a scratch for every itch",\
"hard work never killed anybody, but why take a chance?",\
"have an adequate day",\
"having children is like having a bowling alley installed in your brain",\
"having children will turn you into your parents",\
"he has the heart of a little child...  it\'s in a jar on his desk",\
"he is considered a most graceful speaker who can say nothing in the most words",\
"he is no lawyer who cannot take two sides",\
"he was so narrow-minded he could see through a keyhole with both eyes",\
"he who dies with the most toys is nonetheless dead",\
"he who dies with the most toys, wins",\
"he who has a shady past knows that nice guys finish last",\
"he who hesitates is a damned fool",\
"he who hesitates is probably right",\
"he who invents adages to peruse takes along rowboat when going on cruise",\
"he who is content with his lot probably has a lot",\
"he who is still laughing hasn\'t yet heard the bad news",\
"he who laughs last didn\'t get the joke",\
"he who shouts the loudest has the floor",\
"he who speak with forked tongue, not need chopsticks",\
"he who spends a storm beneath a tree, takes life with a grain of tnt",\
"health is merely the slowest possible rate at which one can die",\
"heat expands:  in the summer the days are longer",\
"heating with wood, you get warm twice:  once chopping it, and once stacking it",\
"heineken uncertainty principle:  never sure how many beers you had last night",\
"heisenberg might have been here",\
"help stamp out and abolish redundancy",\
"help!  my typewriter is broken!",\
"history chronicles the small portion of the past that was suitable for print",\
"history does not repeat itself; historians merely repeat each other",\
"honesty is the best policy, but insanity is a better defense",\
"honeymoon:  a short period of doting between dating and debting",\
"honk if you love peace and quiet",\
"hospitality:  making your guests feel at home, even though you wish they were",\
"how can you govern a nation which has 246 kinds of cheese?",\
"how come wrong numbers are never busy?",\
"how do they get all those little metal bits on a zipper to line up so well?",\
"how do you make an elephant float?  two scoops of elephant and some rootbeer..",\
"how long is a minute depends on which side of the bathroom door you are on",\
"how long should a man\'s legs be?  long enough to reach the ground",\
"how many lawyers does it take to screw in a light bulb?  all you can afford",\
"how many weeks are there in a light year?",\
"how much sin can you get away with and still go to heaven?",\
"how sharper than a hound\'s tooth it is to have a thankless serpent",\
"how to regain your virginity:  reverse the process until it returns",\
"how wonderful opera would be if there were no singers",\
"human beings were created by water to transport it uphill",\
"humor is the best antidote to reality",\
"i am a creationist; i refuse to believe that i could have evolved from humans",\
"i am a great housekeeper.  i get divorced.  i keep the house",\
"i am a hollywood writer, so i put on a sports jacket and take off my brain",\
"i am a libra.  libras don\'t believe in astrology",\
"i am dying beyond my means",\
"i am going to live forever, or die trying!",\
"i am not a crook",\
"i am not a lovable man",\
"i am not as dumb as you look",\
"i am not cynical, just experienced",\
"i am prepared for all emergencies but totally unprepared for everyday life",\
"i am really enjoying not talking to you, so let\'s not talk again real soon, ok?",\
"i belong to no organized party.  i am a democrat",\
"i bet you have never seen a plumber bite his nails",\
"i came to mit to get an education for myself and a diploma for my mother",\
"i can relate to that",\
"i can\'t give you brains, but i can give you a diploma",\
"i could not possibly fail to disagree with you less",\
"i do desire we may be better strangers",\
"i don\'t have any solution, but i certainly admire the problem",\
"i doubt, therefore i might be",\
"i generally avoid temptation unless i can\'t resist it",\
"i hate quotations",\
"i have already told you more than i know",\
"i have been in more laps than a napkin",\
"i have found that the best direction for a hot tub to face is up",\
"i have had a perfectly wonderful evening.  but this wasn\'t it",\
"i have heard about people like me, but i never made the connection",\
"i have seen the future and it is just like the present, only longer",\
"i have the simplest tastes.  i am always satisfied with the best",\
"i have ways of making money that you know nothing of",\
"i just need enough to tide me over until i need more",\
"i know on which side my bread is buttered",\
"i like work; it fascinates me.  i can sit and look at it for hours",\
"i love mankind...  it\'s people i hate",\
"i love my job; it\'s the work i can\'t stand",\
"i may not be the world\'s greatest lover, but number seven\'s not bad",\
"i may not be totally perfect, but parts of me are excellent",\
"i must follow the people.  am i not their leader?",\
"i must get out of these wet clothes and into a dry martini",\
"i never forget a face, but in your case i\'ll make an exception",\
"i never made a mistake in my life.  i thought i did once, but i was wrong",\
"i often quote myself; it adds spice to my conversation",\
"i promise we would only loose ten to twenty million tops!",\
"i put instant coffee in a microwave, and almost went back in time",\
"i really had to act; \'cause i didn\'t have any lines",\
"i saw a subliminal advertising executive, but only for a second",\
"i shot an arrow into the air and it stuck",\
"i spilled spot remover on my dog.  now he\'s gone",\
"i suggest a new strategy, artoo:  let the wookee win",\
"i think sex is better than logic, but i can\'t prove it",\
"i think that i shall never see a billboard lovely as a tree",\
"i think we are all bozos on this bus",\
"i used to be lost in the shuffle.  now i just shuffle along with the lost",\
"i used to be snow white, but i drifted",\
"i used to get high on life, but lately i have built up a resistance",\
"i used to think i was indecisive, but now i am not so sure",\
"i want to achieve immortality through not dying",\
"i will always love the false image i had of you",\
"i will meet you at the corner of walk and don\'t walk",\
"i will never lie to you",\
"i worked myself up from nothing to a state of extreme poverty",\
"i would give my right arm to be ambidextrous",\
"i would have made a good pope",\
"i would like to help you out.  which way did you come in?",\
"i would like to lick apricot brandy out of your navel",\
"i would never join any club that would have the likes of me as a member",\
"i\'d like to meet the person who invented sex, and see what he\'s working on now",\
"i\'d rather have a free bottle in front of me than a prefrontal lobotomy",\
"i\'ll race you to china.  you can have a head start.  ready, set, go!",\
"i\'m in pittsburgh.  why am i here?",\
"i\'m not afraid to die.  i just don\'t want to be there when it happens",\
"i\'m not going deaf.  i\'m ignoring you",\
"i\'m not under the alkafluence of inkahol that some thinkle peep i am",\
"i\'m pretty good with bs but i love listening to an expert.  keep talking",\
"ice cream cures all ills.  temporarily",\
"idiot box:  part of an envelope that tells a person where to place the stamp",\
"if a straight line fit is required, obtain only two data points",\
"if a thing\'s worth doing, it is worth doing badly",\
"if all the world\'s a stage, i want to operate the trap door",\
"if all the world\'s managers were laid end to end, it would be an improvement",\
"if an item is advertised as \"under $50\", you can bet it\'s not $19.95",\
"if at first you do succeed, try to hide your astonishment",\
"if at first you don\'t succeed, destroy all evidence that you tried",\
"if at first you don\'t succeed, quit; don\'t be a nut about success",\
"if at first you don\'t succeed, redefine success",\
"if at first you don\'t succeed, you probably didn\'t really care anyway",\
"if at first you don\'t succeed, you\'re doing about average",\
"if at first you don\'t succeed, your successor will",\
"if at first you don\'t suck seed, suck harder",\
"if conditions are not favorable, bacteria go into a period of adolescence",\
"if enough data is collected, anything can be proven by statistical methods",\
"if entropy is increasing, where is it coming from?",\
"if flattery gets you nowhere, try bribery",\
"if god had meant for us to be naked, we would have been born that way",\
"if god had wanted you to go around nude, he would have given you bigger hands",\
"if god is perfect, why did he create discontinuous functions?",\
"if god is so great, how come everything he makes dies?",\
"if god lived on earth, people would knock out all his windows. - yiddish proverb",\
"if i could drop dead right now, i\'d be the happiest man alive",\
"if i had any humility i would be perfect",\
"if i owned texas and hell, i would rent out texas and live in hell",\
"if i told you you had a beautiful body, would you hold it against me?",\
"if ignorance is bliss, why aren\'t there more happy people?",\
"if in doubt, mumble",\
"if it ain\'t damp, it ain\'t camp",\
"if it is tuesday, this must be someone else\'s fortune",\
"if it is worth doing, it is worth doing for money",\
"if it jams, force it.  if it breaks, it needed replacing anyway",\
"if it pours before seven, it has rained by eleven",\
"if it wasn\'t for lawyers, we wouldn\'t need them",\
"if it wasn\'t for muscle spasms, i wouldn\'t get any exercise at all",\
"if it wasn\'t for newton, we wouldn\'t have to eat bruised apples",\
"if it were truly the thought that counted, more women would be pregnant",\
"if little else, the brain is an educational toy",\
"if murphy\'s law can go wrong, it will",\
"if one hundred people do a foolish thing, one will become injured",\
"if only i could be respected without having to be respectable",\
"if opportunity came disguised as temptation, one knock would be enough",\
"if parents would only realize how they bore their children",\
"if reproducibility might be a problem, conduct the test only once",\
"if some people didn\'t tell you, you\'d never know they\'d been away on vacation",\
"if sound can\'t travel in a vacuum, why are vacuum cleaners so noisy?",\
"if the probability of success is not almost one, it is damn near zero",\
"if the ship is not sinking, the rats must be the ones not leaving",\
"if the shoe fits, buy the other one too",\
"if the shoe fits, it\'s ugly",\
"if there is light at the end of the tunnel...  order more tunnel",\
"if there is no god, who pops up the next kleenex?",\
"if this saying did not exist, somebody would have invented it",\
"if time heals all wounds, how come bellybuttons don\'t fill in?",\
"if today is the first day of the rest of your life, what was yesterday?",\
"if we all work together we can totally disrupt the system",\
"if we knew what the hell we were doing, then it wouldn\'t be research",\
"if you are a fatalist, what can you do about it?",\
"if you are asked to join a parade, don\'t march behind the elephants",\
"if you are horny, it\'s lust, but if your partner\'s horny, it\'s affection",\
"if you are not very clever you should be conciliatory",\
"if you are seen fixing it, you will be blamed for breaking it",\
"if you can count your money, you don\'t have a billion dollars",\
"if you can lead it to water and force it to drink, it isn\'t a horse",\
"if you can survive death, you can probably survive anything",\
"if you can\'t be replaced, you can\'t be promoted",\
"if you can\'t dazzle \'em with brilliance, baffle \'em with bullshit",\
"if you can\'t find your glasses, it\'s probably because you don\'t have them on",\
"if you can\'t say anything nice, you probably don\'t have many friends",\
"if you cannot convince them, confuse them",\
"if you cannot hope for order, withdraw with style from the chaos",\
"if you do a job too well, you will get stuck with it",\
"if you do not change direction you are likely to end up where you are headed",\
"if you do something right once, someone will ask you to do it again",\
"if you don\'t care where you are, then you aren\'t lost",\
"if you don\'t go to other men\'s funerals they won\'t go to yours",\
"if you don\'t know what you\'re doing, do it neatly",\
"if you don\'t say anything, you won\'t be called on to repeat it",\
"if you explain so clearly that no one can possibly misunderstand, someone will",\
"if you have half a mind to watch tv, that is enough",\
"if you have kleptomania, you can always take something for it",\
"if you have to ask how much it is, you can\'t afford it",\
"if you have to travel on the titanic, why not go first class?",\
"if you liked earth, you will love heaven",\
"if you live in a country run by committee, be on the committee",\
"if you look like your passport photo, it\'s time to go home",\
"if you look like your passport photo, you aren\'t well enough to travel",\
"if you mess with a thing long enough, it will break",\
"if you put it off long enough, it might go away",\
"if you think before you speak, the other guy gets his joke in first",\
"if you think the problem is bad now, just wait until we\'ve solved it",\
"if you want to know how old a man is, ask his brother-in-law",\
"if you want to put yourself on the map, publish your own map",\
"if you were to ask me this question, what would my answer be?",\
"if you\'re not part of the solution, you\'re part of the precipitate",\
"if you\'ve seen one redwood, you\'ve seen them all",\
"if your parents didn\'t have any children, neither will you",\
"ignorance:  when you don\'t know anything, and someone else finds out",\
"ignore previous fortune",\
"ill-bred children always display their pest manners",\
"illiterate?  write for free help",\
"immigration is the sincerest form of flattery",\
"imports are ports very far inland",\
"in 1869 the waffle iron was invented for people who had wrinkled waffles",\
"in a ham and egg breakfast, the chicken was involved, but the pig was committed",\
"in a modern household, the only things we have to wash by hand are children",\
"in america, it is not how much an item costs, it is how much you save",\
"in an orderly world, there is always a place for the disorderly",\
"in english, every word can be verbed",\
"in lake wobegon, all the children are above average",\
"in marriage, as in war, it is permitted to take every advantage of the enemy",\
"in matrimony, to hesitate is sometimes to be saved",\
"in order to get a loan you must first prove you don\'t need it",\
"in process:  so wrapped up in red tape that the situation is almost hopeless",\
"in the first half of our life we learn habits that shorten the second half",\
"in this world, truth can wait; she is used to it",\
"ingrate:  bites the hand that feeds him, and then complains of indigestion",\
"insanity is inherited; you get it from your kids!",\
"instant sex will never be better than the kind you have to peel and cook",\
"institute:  an archaic school where football is not taught",\
"interchangeable parts won\'t",\
"irrationality is the square root of all evil",\
"irs:  income reduction service",\
"is it time for lunch yet?",\
"is there life before death?",\
"is this really happening?",\
"it ain\'t loafing unless they can prove it",\
"it does not do to leave a live dragon out of your calculations",\
"it doesn\'t matter whether you win or los",\
"it is bad luck to be superstitious",\
"it is better for civilization to go down the drain than to come up it",\
"it is better to be on penicillin than never to have loved at all",\
"it is better to be on the ground wishing you were flying, than vice versa",\
"it is better to burn out than to fade away",\
"it is better to give than to lend, and it costs about the same",\
"it is better to have a positive wasserman than never to have loved at all",\
"it is better to have loved and los",\
"it is better to have loved and lost than just to have lost",\
"it is better to light one candle than to torch a wax museum with a flamethrower",\
"it is better to remain childless than to father an orphan",\
"it is better to wear out than to rust out",\
"it is dangerous to name your children before you know how many you will have",\
"it is difficult to legislate morality in the absence of moral legislators",\
"it is difficult to soar with eagles when you work with turkeys",\
"it is easier to take it apart than to put it back together",\
"it is kind of fun to do the impossible",\
"it is later than you think",\
"it is more than magnificen",\
"it is much easier to suggest solutions when you know nothing about the problem",\
"it is not a good omen when goldfish commit suicide",\
"it is not an optical illusion, it just looks like one",\
"it is not camelot, but it\'s not cleveland, either",\
"it is not that you and i are so clever, but that the others are such fools",\
"it is so soon that i am done for, i wonder what i was begun for",\
"it now costs more to amuse a child than it once did to educate his father",\
"it seems to make an auto driver mad if she misses you",\
"it takes more than three weeks to prepare a good impromptu speech",\
"it was a book to kill time for those who liked it better dead",\
"it was a brave man that ate the first oyster",\
"it was such a beautiful day i decided to stay in bed",\
"it works better if you plug it in",\
"it would be nice to be sure of anything the way some people are of everything",\
"it would take a miracle to get you out of casablanca",\
"it\'s a damn poor mind that can only think of one way to spell a word",\
"it\'s a small world, but i wouldn\'t want to paint it",\
"it\'s hard to get ivory in africa, but in alabama the tuscaloosa",\
"it\'s hard to soar like an eagle when you are surrounded by turkeys",\
"it\'s hell to work for a nervous boss, especially if you are why he\'s nervous!",\
"it\'s not easy being green",\
"it\'s not hard to meet expenses; they are everywhere",\
"it\'s not whether you win or lose, it\'s how you look playing the game",\
"jesus saves; moses invests; but only buddha pays dividends",\
"job placement:  telling your boss what he can do with your job",\
"journalism is literature in a hurry",\
"journalism will kill you, but it will keep you alive while you are at it",\
"jury:  twelve men and women trying to decide which party has the best lawyer",\
"just because you are not paranoid doesn\'t mean they are not out to get you",\
"just give alice some pencils and she will stay busy for hours",\
"just when you get going, someone injects a dose of reality with a large needle",\
"justice:  a decision in your favor",\
"keep a very firm grasp on reality, so you can strangle it at any time",\
"keep america beautiful.  swallow your beer cans",\
"keep stress out of your life.  give it to others instead",\
"keep the pointy end forward and the dirty side down",\
"klatu barada nikto",\
"klein bottle for ren",\
"kleptomaniac:  a rich thief",\
"knocked; you weren\'t in",\
"know thyself-- but don\'t tell anyone",\
"know what i hate most?  rhetorical questions",\
"krogt:  the metallic silver coating found on fast-food game cards",\
"lactomangulation:  abusing the \"open here\" spout on a milk carton",\
"laugh at your problems; everybody else does",\
"laugh, and the world ignores you.  crying doesn\'t help either",\
"lead me not into temptation.  i can find it myself",\
"learning at some schools is like drinking from a firehose",\
"let him who takes the plunge remember to return it by tuesday",\
"let not the sands of time get in your lunch",\
"liberal:  a conservative who has just been arrested",\
"liberal:  someone too poor to be a capitalist and too rich to be a communist",\
"lie:  a very poor substitute for the truth, but the only one discovered to date",\
"life -- love it or leave it",\
"life begins at the centerfold and expands outward",\
"life is a game of bridg-- and you have just been finessed",\
"life is a sexually transmitted terminal disease",\
"life is complex.  it has real and imaginary parts",\
"life is difficult because it is non-linear",\
"life is fraught with opportunities to keep your mouth shut",\
"life is like a fountain...  i will tell you how when i figure it out",\
"life is like a maze in which you try to avoid the exit",\
"life is like a sewer...  what you get out of it depends on what you put into it",\
"life is like an analogy",\
"life is not for everyone",\
"life is uncertain, so eat dessert first",\
"life is wasted on the living",\
"life might have no meaning, or worse, it might have a meaning you don\'t like",\
"life without caffeine is stimulating enough",\
"life:  a brief interlude between nothingness and eternity",\
"little things come in small packages",\
"live fast, die young, and leave a good looking corpse",\
"living on earth includes an annual free trip around the sun",\
"living your life is so difficult, it has never been attempted before",\
"logic is a little bird, sitting in a tree, that smells awful",\
"logic is a means of confidently being wrong",\
"logic is an organized way of going wrong with confidence",\
"losing your driver\'s license is just god\'s way of saying \"booga, booga!\"",\
"love does not make the world go around, just up and down a bit",\
"love is being stupid together",\
"love is grand...  divorce is twenty grand",\
"love is the triumph of imagination over intelligence",\
"love means having to say you\'re sorry every five minutes",\
"love means nothing to a tennis player",\
"love thy neighbor as thyself, but choose thy neighborhood",\
"love thy neighbor:  tune thy piano",\
"love your enemies.  it will make them crazy",\
"love:  an obsessive delusion that is cured by marriage",\
"love:  the only game that is not called on account of darkness",\
"love:  the warm feeling you get towards someone who meets your neurotic needs",\
"lsd melts in your mind, not in your hand",\
"lsd soaks up 47 times its own weight in excess reality",\
"machines have less problems.  i\'d like to be a machine",\
"magnocartic:  an automobile that when left unattended attracts shopping carts",\
"maintain thy airspeed, lest the ground rise up and smite thee",\
"majority:  that quality that distinguishes a crime from a law",\
"make a firm decision now...  you can always change it later",\
"male zebras have white stripes, but female zebras have black stripes",\
"man has made his bedlam; let him lie in it",\
"man invented language to satisfy his deep need to complain",\
"man is the only animal that blushes -- or needs to",\
"man who arrives at party two hours late finds he has been beaten to the punch",\
"man who falls in blast furnace is certain to feel overwrought",\
"man who falls in vat of molten optical glass makes spectacle of self",\
"mankind has never reconciled itself to the ten commandments",\
"mankind...  infests the whole habitable earth and canada",\
"many a family tree needs trimming",\
"many are called, but few are at their desks",\
"many are cold, but few are frozen",\
"many quite distinguished people have bodies similar to yours",\
"marriage is a rest period between romances",\
"marriage is a three ring circus:  engagement ring, wedding ring, and suffering",\
"marriage is a trip between niagra falls and reno",\
"marriage is an institution -- but who wants to live in one?",\
"marriage is low down, but you spend the rest of your life paying for it",\
"marriage is not a word; it is a sentence",\
"marriage is the only adventure open to the cowardly",\
"marriage is the sole cause of divorce",\
"marriages are made in heaven and consummated on earth",\
"martinus hubertus bernardus kok is geen halfgod, maar een halfzot",\
"math is like love; a simple idea, but it can get complicated",\
"mathematicians are willing to assume anything -- except responsibility",\
"mathematicians take it to the limit",\
"matrimony is the root of all evil",\
"matter cannot be created or destroyed; nor can it be returned without a receipt",\
"matter will be damaged in direct proportion to its value",\
"maturity is a high price to pay for growing up",\
"may you die in bed at 95, shot by a jealous spouse",\
"may you have many friends and very few living enemies",\
"maybe you can\'t buy happiness, but these days you can certainly charge it",\
"measure with a micrometer; mark with chalk; cut with an axe",\
"meeting:  a gathering where the minutes are kept and the hours lost",\
"men seldom show dimples to girls who have pimples",\
"michelangelo would have made better time with a roller",\
"microwaves frizz your heir",\
"military intelligence is a contradiction in terms",\
"military justice is to justice what military music is to music",\
"millihelen:  the amount of beauty required to launch one ship",\
"miracles are great, but they are so damned unpredictable",\
"moderation is a fatal thing.  nothing succeeds like excess",\
"modern man is the missing link between apes and human beings",\
"modesty is a vastly overrated virtue",\
"modesty:  being comfortable that others will discover your greatness",\
"momentum:  what you give a person when they are going away",\
"money can\'t buy happiness, but it can certainly rent it for a couple of hours",\
"money can\'t buy happiness, but it lets you be miserable in comfort",\
"money does tal-- it says goodbye",\
"money is better than poverty, if only for financial reasons",\
"money is the root of all evil, and man needs roots",\
"monotony:  the practice of having only one spouse at a time",\
"most general statements are false, including this one",\
"most people get lost in thought because it is unfamiliar territory",\
"mother is far too clever to understand anything she does not like",\
"mother is the invention of necessity",\
"mother told me to be good, but she has been wrong before",\
"mountain climbers rope together to prevent the sensible ones from going home",\
"mountain range:  a cooking stove used at high altitudes",\
"mummy:  an egyptian who was pressed for time",\
"music sung by two people at the same time is called a duel",\
"my family history begins with me, but yours ends with you",\
"my life has a superb cast but i can\'t figure out the plot",\
"my opinions might have changed, but not the fact that i am right",\
"my own business always bores me to death; i prefer other people\'s",\
"my theology, briefly, is that the universe was dictated but not signed",\
"mysticism is based on the assumption that you can quit the game",\
"narcolepulacy:  the contagious action of yawning",\
"necessity is a mother",\
"neckties strangle clear thinking",\
"neutrinos have bad breadth",\
"never do today what you can put off until tomorrow",\
"never eat anything bigger than your head",\
"never eat more than you can lift",\
"never give an inch!",\
"never have any children, only grandchildren",\
"never hit a man with glasses.  hit him with a baseball bat",\
"never invest your money in anything that eats or needs repainting",\
"never laugh at live dragons",\
"never offend with style when you can offend with substance",\
"never put off till tomorrow what you can avoid all together",\
"never sleep with anyone crazier than yourself",\
"never verb your nouns",\
"new:  different color from previous model",\
"nice guys don\'t finish nice",\
"nine out of ten doctors agree that one out of ten doctors is an idiot",\
"nine out of ten people think they are above average.  the rest are in therapy",\
"no guts, no glory",\
"no maintenance:  impossible to fix",\
"no man is an island, but some of us are long peninsulas",\
"no man would listen to you talk if he didn\'t know it was his turn next",\
"no matter what goes wrong, there\'s always someone who knew it would",\
"no matter what results are expected, someone is always willing to fake it",\
"no one can feel as helpless as the owner of a sick goldfish",\
"no one gets too old to learn a new way of being stupid",\
"no prizes for predicting rain.  prizes only awarded for building arks",\
"no problem is so large it can\'t be fit in somewhere",\
"nobody can be as agreeable as an uninvited guest",\
"nobody ever has a reservation on a plane that leaves from gate 1",\
"nobody expects the spanish inquisition!",\
"nobody knows the trouble i have been",\
"nobody wants constructive criticism.  we can barely handle constructive praise",\
"nondeterminism means never having to say you are wrong",\
"nonsense.  space is blue and birds fly through it",\
"nostalgia just isn\'t what it used to be",\
"not all men who drink are poets.  some of us drink because we are not poets",\
"nothing can be done in one trip",\
"nothing cures insomnia like the realization that it is time to get up",\
"nothing increases your golf score like witnesses",\
"nothing is as inevitable as a mistake whose time has come",\
"nothing is ever a total loss; it can always serve as a bad example",\
"nothing is finished until the paperwork is done",\
"nothing is impossible or impassable if you have enough nails",\
"nothing recedes like success",\
"nothing so needs reforming as other people\'s habits",\
"nothing will dispel enthusiasm like a small admission fee",\
"now and then an innocent person is sent to the legislature",\
"now it\'s time to say goodbye, to all our company...  m-i-c, k-e-y, m-o-u-s-e",\
"nuclear war would really set back cable",\
"nudists are people who wear one-button suits",\
"nugloo:  single continuous eyebrow that covers the entire forehead",\
"of all the animals, the boy is the most unmanageable",\
"of course i am happily married -- she\'s happy, and i\'m married",\
"often it is fatal to live too long",\
"oh what a tangled web we weave, when first we practice to conceive",\
"oh, aunty em, it\'s so good to be home!",\
"old macdonald had an agricultural real estate tax abatement",\
"omniscience:  talking only about things you know about",\
"on the whole, i\'d rather be in philadelphia",\
"once a job is fouled up, anything done to improve it only makes it worse",\
"once is happenstance.  twice is coincidence.  thrice is enemy action",\
"once upon a time, charity was a virtue and not an organization",\
"one bell system -- it sometimes works",\
"one bit of advice:  don\'t give it",\
"one child is not enough, but two children are far too many",\
"one good thing about repeating your mistakes is that you know when to cringe",\
"one good turn usually gets most of the blanket",\
"one nice thing about egotists:  they don\'t talk about other people",\
"one size fits all:  doesn\'t fit anyone",\
"one thing leads to another, and usually does",\
"one way to make your old car run better is to look up the price of a new model",\
"only adults have difficulty with childproof caps",\
"only fools are quoted",\
"only through hard work and perseverance can one truly suffer",\
"only two groups of people fall for flattery:  men and women",\
"opportunity always knocks at the least opportune moment",\
"our parents were never our age",\
"our policy is, when in doubt, do the right thing",\
"our problems are mostly behind us.  now we have to fight the solutions",\
"our vision is to speed up time, eventually eliminating it",\
"out of the mouths of babes does often come cereal",\
"outpatient:  a person who has fainted",\
"oversteer is when the passenger is scared; understeer when the driver is scared",\
"packrat\'s credo:  \"i have no use for it, but i hate to see it go to waste.\"",\
"paper is always strongest at the perforations",\
"paradise is exactly like where you are, only much, much better",\
"paradox:  an assistant to phds",\
"parallel lines never meet unless you bend one or both of them",\
"paranoia:  a healthy understanding of the nature of the universe",\
"paranoid schizophrenics outnumber their enemies at least two to one",\
"people accept an idea more readily if you say benjamin franklin said it first",\
"people have one thing in common:  they are all different",\
"people usually get what\'s coming to them...  unless it was mailed",\
"people who live in glass houses shouldn\'t throw parties",\
"people who take cat naps usually don\'t sleep in a cat\'s cradle",\
"people who think they know everything greatly annoy those of us who do",\
"people will buy anything that is one to a customer",\
"perfect guest:  one who makes his host feel at home",\
"perfect paranoia is perfect awareness",\
"perhaps your whole purpose in life is simply to serve as a warning to others",\
"phasers locked on target, captain",\
"philosophy:  unintelligible answers to insoluble problems",\
"pity the meek, for they shall inherit the earth",\
"pity the poor egg; it only gets laid once",\
"politics consists of deals and ideals",\
"politics:  the art of turning influence into affluence",\
"positive:  being mistaken at the top of your voice",\
"possessions increase to fill the space available for their storage",\
"pound for pound, the amoeba is the most vicious animal on earth",\
"power means not having to respond",\
"predestination was doomed from the start",\
"preudhomme\'s law of window cleaning:  it\'s on the other side",\
"pro is to con as progress is to congress",\
"proctologist:  a doctor who puts in a hard day at the orifice",\
"professor:  one who talks in someone else\'s sleep",\
"progress means replacing a theory that is wrong with one more subtly wrong",\
"progress might have been all right once, but it\'s gone on too long",\
"proofreading is more effective after publication",\
"proximity isn\'t everything, but it comes close",\
"puritan:  someone who is deathly afraid that someone somewhere is having fun",\
"pushing 40 is exercise enough",\
"quack!",\
"quality assurance doesn\'t",\
"quantity is no substitute for quality, but it is the only one we have",\
"quark!  quark!  beware the quantum duck!",\
"question authority...  and the authorities will question you!",\
"quidquid latine dictum sit, altum viditur.  (anything in latin sounds profound.)",\
"quinine is the bark of a tree; canine is the bark of a dog",\
"quit working and play for once!",\
"quoting one is plagiarism.  quoting many is research",\
"radioactive cats have 18 half-lives",\
"rainy days and automatic weapons always get me down",\
"reality -- what a concept!",\
"reality is for people who can\'t deal with drugs",\
"reality is for people who lack imagination",\
"reality is just a convenient measure of complexity",\
"reality is that which, when you stop believing in it, doesn\'t go away",\
"refrain means don\'t do it. a refrain in music is the part you better not sing",\
"refuse to have a battle of wits with an unarmed person",\
"reputation:  what others are not thinking about you",\
"research is what i\'m doing when i don\'t know what i\'m doing",\
"right now i\'m having amnesia and deja vu at the same time",\
"rugged:  too heavy to lift",\
"rumper sticker on a horse:  \"get off my tail, because shit happens.\"",\
"russia has abolished god, but so far god has been more tolerant",\
"sacred cows make great hamburger",\
"saddam hussein is the father of the mother of all cliches",\
"sailing:  a form of mast transit",\
"satisfaction guaranteed, or twice your load back.-- sign on septic tank truck",\
"schroedinger\'s cat might have died for your sins",\
"science is material.  religion is immaterial",\
"scotty, beam me up a double!",\
"second marriage is the triumph of hope over experience",\
"seeing is deceiving.  it\'s eating that\'s believing",\
"seek simplicity -- and distrust it",\
"serendipity:  the process by which human knowledge is advanced",\
"serving coffee on aircraft causes turbulence",\
"sex is dirty only when it\'s done right",\
"sex is not the answer.  sex is the question.  \"yes\" is the answer",\
"sex is the most fun you can have without laughing",\
"she walks as if balancing the family tree on her nose",\
"showing up is 80% of life",\
"sign on bank:  \"free bottle of chivas with every million-dollar deposit.\"",\
"silly is a state of mind.  stupid is a way of life",\
"smile!  you\'re on candid camera",\
"smoking is one of the leading causes of statistics",\
"snow and adolescence are problems that disappear if you ignore them long enough",\
"socialism is based on the assumption that you can break even",\
"some is good, more is better, too much is just right",\
"some make things happen; some watch what happens; some wonder what happened",\
"some men are discovered; others are found out",\
"some people cause happiness wherever they go; others, whenever they go",\
"some people who can, should not",\
"some people would not recognize subtlety if it hit them on the head",\
"some prefer the happiness of pursuit to the pursuit of happiness",\
"someday you will get your big chance -- or have you already had it?",\
"someday you will look back on this moment and plow into a parked car",\
"sometimes a cigar is just a cigar",\
"sorry, but my karma just ran over your dogma",\
"spare no expense to save money on this one",\
"speed is n subsittute fo accurancy",\
"spelling is a lossed art",\
"spinster:  a bachelor\'s wife",\
"spirobits:  the frayed bits of left-behind paper in a spiral notebook",\
"spock:  we suffered 23 casualties in that attack, captain",\
"standing on head makes smile of frown, but rest of face also upside down",\
"statisticians do it with 95 percent confidence",\
"stealing a rhinoceros should not be attempted lightly",\
"stock item:  we shipped it once before and we can do it again",\
"stop committing useless mistakes.  make your next mistake count!",\
"strategy is when you keep firing so the enemy doesn\'t know you\'re out of ammo",\
"stupid:  losing $25 on the game, and $25 more on the instant replay",\
"success always occurs in private, and failure in full view",\
"success is something i will dress for when i get there, and not until",\
"success:  the ability to go from failure to failure without being discouraged",\
"suicide is the sincerest form of self-criticism",\
"sweater:  a garment worn by a child when his parent feels chilly",\
"system-independent:  works equally poorly on all systems",\
"tact:  the unsaid part of what you are thinking",\
"take everything in stride.  trample anyone who gets in your way",\
"talent does what it can; genius does what it must; i do what i am paid to do",\
"taxes are going up so fast, the government might price itself out of the market",\
"taxes:  the one of life\'s two certainties for which you can get an extension",\
"teamwork is essential; it allows you to blame someone else",\
"technique:  a trick that works",\
"teenagers are two year olds with hormones and wheels",\
"telepathy:  knowing what people think when really they don\'t think at all",\
"terrorists blow up celluloid factory...  no film at 11",\
"thank you for observing all safety precautions",\
"that must be wonderful; i don\'t understand it at all",\
"that that is is that that is not is not",\
"the adjective is the banana peel of the parts of speech",\
"the best cure for insomnia is a monday morning",\
"the best cure for insomnia is to get a lot of sleep",\
"the best laid plans of mice and men are usually about equal",\
"the best thing about growing older is that it takes such a long time",\
"the best way to inspire fresh thoughts is to seal the envelope",\
"the bigger they are, the harder they hit",\
"the bureaucracy expands to meet the needs of an expanding bureaucracy",\
"the chief cause of problems is solutions",\
"the climate of bombay is such that its inhabitants have to live elsewhere",\
"the coldest winter i ever spent was a summer in san francisco",\
"the cost of living hasn\'t affected its popularity",\
"the cost of living is going up, and the chance of living is going down",\
"the cow is a machine that makes grass fit for us people to eat",\
"the cow is of the bovine ilk; one end is moo, the other, milk",\
"the death rate on earth is:  one per person",\
"the decision does not have to be logical; it was unanimous",\
"the difference between a good haircut and a bad one is seven days",\
"the difficult we do today; the impossible takes a little longer",\
"the early worm gets the late bird",\
"the fact that it works is immaterial",\
"the famous politician was trying to save both his faces",\
"the fewer the data points, the smoother the curve",\
"the first myth of management is that it exists",\
"the first piece of luggage out of the chute does not belong to anyone, ever",\
"the first rule of gun fighting is -- bring a gun",\
"the first rule of intelligent tinkering is to save all the parts",\
"the first thing i do in the morning is brush my teeth and sharpen my tongue",\
"the first thing we do, let\'s kill all the lawyers",\
"the flush toilet is the basis of western civilization",\
"the following statement is not true..",\
"the four seasons are salt, pepper, mustard, and vinegar",\
"the future is a myth created by insurance salesmen and high school counselors",\
"the general direction of the alps is straight up",\
"the grass is always greener on the other side of your sunglasses",\
"the hardest thing in the world to understand is the income tax",\
"the highway of life is always under construction",\
"the idea is to die young as late as possible",\
"the knack of flying is learning how to throw yourself at the ground and miss",\
"the law of gravity was enacted by the british parliament",\
"the light at the end of the tunnel is the headlight of an approaching train",\
"the lion and the calf shall lie down together but the calf won\'t get much sleep",\
"the meek shall inherit the earth -- they are too weak to refuse",\
"the meek shall inherit the earth after we are done with it",\
"the more keys you have, the more likely to be you are locked out",\
"the more things change, the more they stay insane",\
"the more things change, the more they will never be the same again",\
"the mosquito is the state bird of new jersey",\
"the most dangerous part about playing cymbals is near the nose",\
"the most enjoyable form of sex education is the braille method",\
"the moving finger having writ...  gestures",\
"the next thing i say will be true.  the last thing i said was false",\
"the nice thing about standards is that there are so many of them to choose from",\
"the number watching you is proportional to the stupidity of your action",\
"the number you have dialed is imaginary.  please multiply by i and dial again",\
"the older a man gets, the farther he had to walk to school as a boy",\
"the one who says it can\'t be done should never interrupt the one doing it",\
"the only thing to do with good advice is pass it on",\
"the only tools some people are competent to use are a pen and a checkbook",\
"the only way to get rid of a temptation is to yield to it",\
"the optimum committee has no members",\
"the other line always moves faster",\
"the past is another country; they do things differently there",\
"the perversity of the universe tends toward a maximum",\
"the plural of \"musical instrument\" is \"orchestra\"",\
"the prairies are vast plains covered by treeless forests",\
"the problem with the gene pool is that there is no lifeguard",\
"the pyramids are a range of mountains between france and spain",\
"the race is not always to the swift...  but that\'s the way to bet",\
"the ranger isn\'t gonna like it, yogi",\
"the reason that sex is so popular is that it\'s centrally located",\
"the richer your friends, the more they will cost you",\
"the schizophrenic:  an unauthorized autobiography",\
"the second best policy is dishonesty",\
"the secret of life is to look good at a distance",\
"the secret of success is sincerity.  once you can fake that, you have it made",\
"the sex act is the funniest thing on the face of this earth",\
"the shortest distance between two points is through hell",\
"the shortest distance between two points is under construction",\
"the sixth shiek\'s sixth sheep\'s sick",\
"the society of independent people has no members",\
"the sooner you fall behind, the more time you have to catch up",\
"the stapler runs out of staples only while you are trying to staple something",\
"the supernova makes mt. st. helens and krakatoa look puny",\
"the theory of evolution was greatly objected to because it made men think",\
"the things that interest people most are usually none of their business",\
"the three stages of sex in marriage:  tri-weekly; try-weekly; try-weakly",\
"the total intelligence on the planet is a constant; the population is growing",\
"the tree of learning bears the noblest fruit, but noble fruit tastes bad",\
"the trouble with a kitten is that, when it grows up, it is always a cat",\
"the trouble with being punctual is that no one is there to appreciate it",\
"the trouble with political jokes is that they get elected",\
"the two kinds of egotists:  those who admit it, and the rest of us",\
"the two most common things in the universe are hydrogen and stupidity",\
"the universe is surrounded by whatever it is that surrounds universes",\
"the weather at home improves as soon as you go away",\
"the wind blows harder in the summer so the sun sets later",\
"the word today is legs...  spread the word",\
"the world is run by c students",\
"the world isn\'t any worse.  it\'s just that the news coverage is so much better",\
"the world\'s a stage and most of us are desperately unrehearsed",\
"the worst you can say about god is that he\'s an underachiever",\
"the zebra is chiefly used to illustrate the letter Z",\
"theft from a single author is plagiarism.  theft from three or more is research",\
"there are many excuses for being late, but there are none for being early",\
"there are many kinds of people in the world.  are you one of them?",\
"there are more old drunkards than old doctors",\
"there are more things in heaven and earth than anyplace else",\
"there cannot be a crisis next week.  my schedule is already full",\
"there is a 20% chance of tomorrow",\
"there is a fine line between courage and foolishness.  too bad it\'s not a fence",\
"there is a green, multi-legged creature crawling on your shoulder",\
"there is a vas deferens between men and women",\
"there is always more hell that needs raising",\
"there is an alarming increase in the number of things you know nothing about",\
"there is an old proverb that says just about whatever you want it to",\
"there is at least one fool in every married couple",\
"there is exactly one true categorical statement",\
"there is intelligent life on earth, but i am just visiting",\
"there is no future in time travel",\
"there is no problem a good miracle can\'t solve",\
"there is no room in the drug world for amateurs",\
"there is no substitute for good manners, except, perhaps, fast reflexes",\
"there is no time like the pleasant",\
"there is nothing more permanent than a temporary tax",\
"there is nothing wrong with abstinence, in moderation",\
"there is nothing wrong with teenagers that reasoning with them won\'t aggravate",\
"there is nothing you can do that can\'t be done",\
"there is only one difference between a madman and me.  i am not mad",\
"there is so much to say, but your eyes keep interrupting me",\
"there is very little future in being right when your boss is wrong",\
"there must be more to life than sitting wondering if there is more to life",\
"they also surf who only stand on waves",\
"they couldn\'t hit an elephant at this dist..",\
"they took some of the van goghs, most of the jewels, and all of the chivas!",\
"things are more like they are today then they ever were before",\
"things are more like they used to be than they are now",\
"things equal to nothing else are equal to each other",\
"things will get better despite our efforts to improve them",\
"think honk if you are telepathic",\
"think how much fun you could have with the doctor\'s wife and a bucket of apples",\
"this fortune is encrypted -- get your decoder rings ready!",\
"this fortune is inoperative.  please try another",\
"this fortune was brought to you by the people at hewlett-packard",\
"this is a crude version of a more advanced joke that has never been written",\
"this is a good time to punt work",\
"this is a recording",\
"this is national non-dairy creamer week",\
"this is the sort of english up with which i will not put",\
"this isn\'t right.  this isn\'t even wrong",\
"this may not be the best of all worlds, but it is certainly the most expensive",\
"this saying would be seven words long if it were six words shorter",\
"this sentence no verb",\
"this statement is in no way to be construed as a disclaimer",\
"this will be a memorable month -- no matter how hard you try to forget it",\
"those who can, do; those who can\'t, simulate",\
"those who flee temptation generally leave a forwarding address",\
"those who like sausages and the law had better not watch either one being made",\
"three may keep a secret, if two of them are dead",\
"time flies like an arrow.  fruit flies like a banana",\
"time flies when you don\'t know what you are doing",\
"time is an illusion perpetrated by the manufacturers of space",\
"time is an illusion; lunchtime doubly so",\
"time is nature\'s way of making sure that everything doesn\'t happen at once",\
"time is the best teacher; unfortunately, it kills all its students",\
"tip the world over on its side and everything loose will land in la",\
"to be safe, make a copy of everything before you destroy it",\
"to err is human.  to admit it is a blunder",\
"to err is human.  to blame someone else for your errors is even more human",\
"to err is human.  to forgive is unusual",\
"to err is human; to forgive is not company policy",\
"to generalize is to be an idiot",\
"to get it done:  do it yourself, hire someone, or forbid your kids to do it",\
"to keep milk from turning sour, you should keep it in the cow",\
"to make a small fortune in the commodities market, start with a large fortune",\
"to study a subject best, understand it thoroughly before you start",\
"to succeed in politics, it is often necessary to rise above your principles",\
"to vacillate or not to vacillate, that is the question...  or is it?",\
"to you i am an atheist; to god, i\'m the loyal opposition",\
"today is a good day to bribe a high ranking public official",\
"today is the first day of the rest of the mess",\
"today is the last day of the past of your life",\
"today is the tomorrow you worried about yesterday",\
"toe:  a part of the foot used to find furniture in the dark",\
"tomorrow looks like a good day to sleep in",\
"tomorrow will be cancelled due to lack of interest",\
"too much is not enough",\
"too much of a good thing is wonderful",\
"toto, i don\'t think we\'re in kansas anymore",\
"tragedy:  a busload of lawyers driving off a cliff with three empty seats",\
"traveling through hyperspace isn\'t like dusting crops, boy",\
"troglodytism does not necessarily imply a low cultural level",\
"truth is the most valuable thing we have -- so let us economize it",\
"truthful:  dumb and illiterate",\
"try the moo shu pork.  it is especially good today",\
"try to get all of your posthumous medals in advance",\
"tuesday after lunch is the cosmic time of the week",\
"tv is called a medium because it is neither rare nor well done",\
"twenty percent of zero is better than nothing",\
"two can live as cheaply as one for half as long",\
"two cars in every pot and a chicken in every garage",\
"two heads are more numerous than one",\
"two is not equal to 3, not even for large values of 2",\
"two wrongs are only the beginning",\
"unauthorized fornication with this equipment is disallowed",\
"under capitalism, man exploits man.  under communism, it is just the opposite",\
"under every stone lurks a politician",\
"unmatched:  almost as good as the competition",\
"very few profundities can be expressed in less than 80 characters",\
"vital papers demonstrate their vitality by spontaneously moving",\
"volcano:  a mountain with hiccups",\
"vote anarchist",\
"wagner\'s music is better than it sounds",\
"waste not, get your budget cut next year",\
"we are all politicians.  some of us are just honest enough to admit it",\
"we are all self-made, but only the rich will admit it",\
"we are living in a golden age.  all you need is gold",\
"we are not a loved organization, but we are a respected one",\
"we are so fond of each other because our ailments are the same",\
"we are sorry.  we cannot complete your call as dialed",\
"we are the people our parents warned us about",\
"we can loan you enough money to get you completely out of debt.-- sign in bank",\
"we could do that, but it would be wrong, that\'s for sure",\
"we don\'t have to protect the environment; the second coming is at hand",\
"we have them just where they want us",\
"we interrupt this fortune for an important announcement..",\
"we need either less corruption or more chance to participate in it",\
"we totally deny the allegations, and we are trying to identify the allegators",\
"we will cross out that bridge when we come back to it later",\
"we will get along fine as soon as you realize i am god",\
"we will have solar energy when the power companies develop a sunbeam meter",\
"wealth:  the ability to support debt",\
"wedding is destiny, and hanging likewise",\
"well adjusted:  makes the same mistake twice without getting nervous",\
"well-adjusted:  able to play bridge or golf as if they were games",\
"what can\'t be said, can\'t be said.  and it can\'t be whistled, either",\
"what did you bring the book i want to be read to out of about down under up for?",\
"what do you call 10,000 lawyers at the bottom of the ocean?  a good start",\
"what do you call a lawyer buried up to his neck in sand?  not enough sand",\
"what happens when you cut back the jungle?  it recedes",\
"what if there were no hypothetical situations?",\
"what is an atheist\'s favorite movie?  \"coincidence on 34th street\"",\
"what is mind?  no matter.  what is matter?  never mind",\
"what is orange and goes \"click, click\"?  a ball point carrot",\
"what is research but a blind date with knowledge?",\
"what is the difference between a duck?  one of its legs is both the same",\
"what is worth doing is worth the trouble of asking someone to do",\
"what orators lack in depth they make up in length",\
"what sane person could live in this world and not be crazy?",\
"what scoundrel stole the cork from my lunch?",\
"what this country needs is more leaders who know what this country needs",\
"what use is magic if it can\'t save a unicorn?",\
"what!  me worry?",\
"what, after all, is a halo?  it\'s only one more thing to keep clean",\
"whatever you want to do, you have to do something else first",\
"when angry, count four; when very angry, swear",\
"when evolution is outlawed, only outlaws will evolve",\
"when god created two sexes, he might have been overdoing it",\
"when i look at my children, i often wish i had remained a virgin",\
"when i\'m good, i\'m very good.  but when i\'m bad, i\'m better",\
"when in charge, ponder.  when in doubt, mumble.  when in trouble, delegate",\
"when in doubt, lead trump",\
"when in trouble or in doubt, run in circles, scream and shout",\
"when it comes to helping you, some people stop at nothing",\
"when it\'s you against the world, bet on the world",\
"when management wants your opinion, they will give it to you",\
"when marriage is outlawed, only outlaws will have inlaws",\
"when mozart was my age, he had been dead for two years",\
"when one burns one\'s bridges, what a very nice fire it makes",\
"when pigs back into an electric fence, there is a short circus",\
"when someone says, \"it ain\'t the money, but the principle,\" it\'s the money",\
"when the going gets tough, everyone leaves",\
"when the going gets weird, the weird turn pro",\
"when they ship styrofoam, what do they pack it in?",\
"when working hard, be sure to get up and retch every so often",\
"when you are in it up to your ears, keep your mouth shut",\
"when you breathe you inspire.  when you do not breathe you expire",\
"when you dial a wrong number you never get a busy signal",\
"when you make your mark in the world, watch out for guys with erasers",\
"when you\'ve got them by the balls, their hearts and minds will follow",\
"when you\'ve seen one non-sequitar, the price of tea in china",\
"when your memory goes, forget it!",\
"whenever anyone says, \"theoretically\", they really mean, \"not really\"",\
"whenever i feel like exercise, i lie down until the feeling passes",\
"wherever you go...there you are",\
"where there is a will, there is an inheritance tax",\
"where there\'s a whip there\'s a way",\
"where there\'s a will, there\'s a relative",\
"whether you can hear it or not, the universe is laughing behind your back",\
"which came first, the chicken or the egg?  neither, it was the rooster",\
"which is worse, ignorance or apathy?  who knows?  who cares?",\
"while money doesn\'t buy love, it puts you in a great bargaining position",\
"white dwarf seeks red giant for binary relationship",\
"who cares about procreation, as long as it tickles?",\
"who dat who say \"who dat\" when i say \"who dat\"?",\
"who was that masked man?",\
"who\'s on first?",\
"whoever said money can\'t buy happiness didn\'t know where to shop",\
"why be difficult when, with a bit of effort, you could be impossible?",\
"why bother building any more nuclear warheads until we use the ones we have?",\
"why did the chicken cross the road?  he was giving it last rites",\
"why did the politician cross the road?  to get to the middle",\
"why did the tachyon cross the road?  because it was on the other side",\
"why doesn\'t life come with subtitles?",\
"why don\'t \"minimalists\" find a shorter name for themselves?",\
"why is \"abbreviated\" such a long word?",\
"why is \"palindrome\" spelled \"palindrome\" and not \"palindromeemordnilap\"?",\
"why is it that there are so many more horses\' asses than horses?",\
"why isn\'t \"phonetic\" spelled the way it\'s said?",\
"why isn\'t there a special name for the tops of your feet?",\
"why was i born with such contemporaries?",\
"why would anyone want to be called later?",\
"winning isn\'t everything, but losing isn\'t anything",\
"with a rubber duck, one\'s never alone",\
"without life, biology itself would be impossible",\
"women want one man to meet every need; men want every woman to meet one need",\
"women who desire to be like men, lack ambition",\
"work is the curse of the drinking class",\
"writing free verse is like playing tennis with the net down",\
"yawning is an orgasm for your face",\
"years of development:  we finally got one to work",\
"yesterday was the deadline on all complaints",\
"yield to temptation; it might not pass your way again",\
"yo-yo:  something occasionally up but normally down (see also \"computer\")",\
"you ain't much if you ain't dutch",\
"you are here.  but you are not all there",\
"you are in a maze of little twisting passages, all alike",\
"you are in a maze of little twisting passages, all different",\
"you are not paranoid if they\'re really after you..",\
"you are ugly and your mother dresses you funny",\
"you are warm and giving toward others.  what are you after?",\
"you aren\'t a real engineer until you make one $50,000 mistake",\
"you can fool some of the people some of the time, and that is sufficient",\
"you can get more with a kind word and a gun than you can with a kind word",\
"you can learn many things from children...  like how much patience you have",\
"you can observe a lot just by watchin\'",\
"you can rent this profound space for only $5 a week",\
"you can\'t have everything.  where would you put it?",\
"you can\'t have kate and edith too!",\
"you cannot buy beer; you can only rent it",\
"you cannot determine beforehand which side of the bread to butter",\
"you could be playing a video game instead",\
"you fill a much-needed gap",\
"you have a right to your opinions.  i just don\'t want to hear them",\
"you have been selected for a secret mission",\
"you have the body of a 19 year old.  please return it before it gets wrinkled",\
"you have the capacity to learn from mistakes.  you will learn a lot today",\
"you know you are a little fat if you have stretch marks on your car",\
"you know you are over the hill when work is less fun and fun is more work",\
"you know you have landed gear-up when it takes full power to taxi",\
"you look like a million dollars...  all green and wrinkled",\
"you never know how many friends you have until you rent a house on the beach",\
"you never know who is right, but you always know who is in charge",\
"you now have asian flu",\
"you will be reincarnated as a toad; and you will be much happier",\
"you will be surprised by a loud noise",\
"you will feel hungry again in another hour",\
"you will live a long full life and gradually decay into a useless blob",\
"you will live a long, healthy, happy life and make bags of money",\
"you will never hit your finger if you hold the hammer with both hands",\
"you will pay for your sins.  if you have already paid, please disregard",\
"you will soon forget this",\
"you will step on the night soil of many countries",\
"you won\'t skid if you stay in a rut",\
"you would if you could but you can\'t so you won\'t (and probably shouldn\'t)",\
"you\'ll find it all at greeley mall",\
"you\'re not drunk if you can lie on the floor without holding on",\
"your chance of forgetting something is directly proportional to...  uh..",\
"your check is in the mail",\
"your fly might be open (but don\'t check it just now)",\
"your love life will be...  interesting",\
"your lucky number has been disconnected",\
"your lucky number is 364958674928.  watch for it everywhere",\
"your reasoning is silly and irrational but it is beginning to make sense",\
"your true value depends entirely on what you are compared with",\
"your weight is perfect for your height -- which varies",\
"youth is too good to be wasted on the young",\
"[he] has all the virtues i dislike and none of the vices i admire",\
"[nuclear war]...  may not be desirable"

#define ADJ \
"old",\
"new",\
"greasy",\
"slimy",\
"big",\
"small",\
"little",\
"soft",\
"hard",\
"strange",\
"weird",\
"tiny",\
"smelly",\
"fat",\
"skinny",\
"dark",\
"light",\
"heavy"

#define COLOR \
"blue",\
"red",\
"white",\
"green",\
"yellow",\
"clear",\
"tangerine",\
"black",\
"purple",\
"pink",\
"brown",\
"tan",\
"grey",\
"olive"

#define WHENF \
"never",\
"when?",\
"next week",\
"next month",\
"a long time from now",\
"2 million ad",\
"not now",\
"tommorow",\
"tonight",\
"in 1 year",\
"in 5 years",\
"next year",\
"in 2010",\
"in the year 2052",\
"in 2001",\
"who can tell"

#define WHENP \
"when?",\
"1 million bc",\
"a long time ago",\
"this morning",\
"yesterday",\
"last week",\
"last month",\
"4 years ago",\
"2 weeks ago",\
"last week",\
"the year 1776",\
"the year 1492",\
"the year 1066",\
"before breakfest",\
"good question"

#define TIME \
"0:00",\
"1:00",\
"2:00",\
"3:00",\
"4:00",\
"5:00",\
"6:00",\
"7:00",\
"8:00",\
"9:00",\
"10:00",\
"11:00",\
"12:00",\
"1:30",\
"2:30",\
"3:30",\
"4:30",\
"5:30",\
"6:30",\
"7:30",\
"8:30",\
"9:30",\
"10:30",\
"11:30",\
"12:30"

#define FOOD \
"pizza",\
"shrimp",\
"bannannas",\
"potted meat",\
"chips and salsa",\
"sushi",\
"beans and weiners",\
"cabbage",\
"sateh",\
"salami",\
"ham",\
"cheese",\
"oatmeal",\
"turkey",\
"cow flesh",\
"t-bone steak",\
"spam",\
"green eggs and ham",\
"sardines",\
"anchovies",\
"spaghetti-o\'s",\
"eggplant",\
"meatloaf",\
"chicken wings",\
"liver",\
"porkchops and applesauce",\
"popcorn",\
"jolly ranchers",\
"texas toast",\
"frech fries",\
"hamburger",\
"beef jerky",\
"cottage cheese",\
"cheese-food",\
"black jellybeans",\
"twinkys",\
"olive loaf"

#define ANIMAL \
"monkey",\
"tiger",\
"horse",\
"hippo",\
"sheep",\
"lion",\
"cow",\
"wombat",\
"kangaroo",\
"zebra",\
"giant eel",\
"racoon",\
"aardvark",\
"duck",\
"dog",\
"cat",\
"wombat",\
"goose",\
"gorilla",\
"squirrel",\
"chipmunk",\
"platypus",\
"blue-footed booby",\
"caribou",\
"head louse",\
"mosquito"

#define WEATHER \
"hot",\
"cold",\
"windy",\
"so so..",\
"hot as hell ",\
"cold as hell!",\
"sunny",\
"cloudy",\
"rainy",\
"pretty cool",\
"a bit warm",\
"overcast",\
"storming",\
"raining",\
"flooding",\
"snowing",\
"hailing",\
"sleeting"

#define MONTH \
"january",\
"february",\
"march",\
"april",\
"may",\
"june",\
"july",\
"august",\
"september",\
"october",\
"november",\
"december"

exclamation = {"!", "!!", "!", "!!!", "!!!!!!!", "!!!"}
good = {"good", "gooood", "so good", "sooo goood"}
looser = {"sucker", "loser", "sckr", "loosr"}
nerd = {"pizza", "nerdy", "nerd"}
boyz = {"boyz", "boyz & grllz", "girlies"}
ramble = {RAMBLE, ONE_LINERS}
response = {AFFIRMATIVE, NEGATIVE, NEUTRAL}
neutral = {NEUTRAL}
negative = {NEGATIVE}
item = {ITEM}
place = {PLACE}
class = {CLASS}
liquid = {LIQUID}
number = {NUMBER}
profession = {PROFESSION}
name = {NAME}
adj = {ADJ}
color = {COLOR}
whenf = {WHENF}
whenp = {WHENP}
time = {TIME}
food = {FOOD}
animal = {ANIMAL}
weather = {WEATHER}
month = {MONTH}
positive = {AFFIRMATIVE}
person = {"mrelusive", "squatt", "mrfreeze", "the pope", "thor"}


