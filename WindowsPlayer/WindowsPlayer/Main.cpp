/*
=============================================================

3DxChat In-Game Video Playback Feature Mod thing

Ryan Verbeck

3DxChat doesn't offer a lot of things to do inside of the game besides sex and social which sucks; so I created another feature into the game.
I created a custom launcher for 3DxChat that adds the ability to play a movie in game. 

Some use cases for this:
    - Movie night with a special someone.
    - Club videos playing in game.
    - New types of game play mechanics for in game mini games.

In Unity the executable(3dxchat.exe) is basically just a stub for calling UnityMain inside of UnityPlayer.dll. Source code for the
laucher comes in all Unity distributions. 3DxChat as of this writing is on Unity version: 2018.4.36f1

This launcher sets up core hooks before calling UnityMain. This launcher hooks numerous things(see below) but one core hook is GetModuleFileNameW.
Hooking GetModuleFileNameW allows a cleaner user experience during the installation process and gets around the engine thinking the game needs to be
patched if the user just dropped in a different launcher executable. So we tell the user just to input the installation folder and return that value
fromm the hooked GetModuleFileNameW function and the game is none the wiser.

A dll in Windows only gets loaded into a process once.
The launcher takes advantage of that and 
    - Loads d3d11.dll before the engine wants to load it and hooks D3D11CreateDevice.
    - Loads dxgi.dll before the engine wants to load it and hooks CreateDXGIFactory1.
              UnityPlayer.dll at 0x0000000180554F20 is were the engine actually calls CreateDXGIFactory1.
    - Hook CreateSwapChainForHwnd so we can hook the present call.


When D3D11CreateDevice gets called by the engine, it now calls our function instead and we grab some variables and hook CreateTexture2D 
and call the trampolined D3D11CreateDevice call.

Now that we have CreateTexture2D hooked we now have the ability to monitor textures the engine loads until we find the texture we want.
This is just hacky fucking shit for the time being but it does work in my test map. Now that we have that we can upload our video 
via RunMovie that runs in the hooked Present call.


=============================================================
*/


#include "PrecompiledHeader.h"
#include "..\UnityPlayerStub\Exports.h"

// DX11 imports
#include <d3d11.h>
#include <D3Dcompiler.h>
#pragma comment(lib, "D3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "winmm.lib")
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }

#include "MinHook.h"
#include "d3d11_vmt.h"
#include <dxgi1_2.h>
#include "Cinematic.h"

#include <chrono>
#include <iostream>
#include <ctime>

#include <shlwapi.h>
#include <winsock2.h>

#include <string>
#include <random>

using std::string;

#pragma comment(lib, "ws2_32.lib")

ID3D11Device* unityD3D11Device = nullptr;
IDXGIFactory* unityDXGIFactory = nullptr;
ID3D11DeviceContext* unityD3D11ImmediateContext = nullptr;

Cinematic* testcinematic = nullptr;

ID3D11Texture2D* streamingMovieTexture = nullptr;

HWND gameWindow = nullptr;

using std::cout; using std::endl;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

const char *CamCards_t[] = {
    "Sports\n",
    "Football | Soccer Golf |  Golf        | Baseball \n",
    "Baskball | Ice Hockey  | Sailing      | Squash   \n",
    "Tennis   | Badminton   | Motor Racing | Wrstling \n",
    "Lacrosse | Vollyball   | Triathion    | Cycling  \n",

    "Jobs\n",
    "Fisherman      | Lumberjack     | Nurse        | Waiter    \n",
    "Janitor        | Secratary      | Accountant   | Teacher   \n",
    "Truck Driver   | Security Gaurd | Chef         | Architect \n",
    "Police Officer | Lawyer         | Carpantar    | Butcher   \n",

    "Fictional Characters\n",
    "Indiana Jones  | Mary Poppins   | Spiderman     | Catwoman           \n",
    "Janitor        | Wonder Woman   | Princess Leia | The Little Mermaid \n",
    "Dracula        | Lara Croft     | Robin Hood    | Herione GRanger    \n",
    "Super Mario    | Home Simpson   | Hercules      | Katniss Everdeen   \n",

    "Countries\n",
    "UK      | Spain    | Japan   | Brazil     \n",
    "France  | USA      | Italy   | Austrailia \n",
    "Germany | Mexico   | India   | Israel     \n",
    "Canada  | China    | Russia  | Egypt      \n",

    "Civilizations\n",
    "Romans | Eyptians  | Mayans    | Mongols    \n",
    "Aztecs | Japaneese | Persians  | Greeks     \n",
    "Turks  | Vikings   | Incas     | Spoanish   \n",
    "Zulu   | Chinese   | Spartans  | Aliens     \n",

    "States\n",
    "California | Texas       | Alabama  | Hawaii      \n",
    "Florida    | Montana     | Nevada   | Mississippi \n",
    "Michigan   | New York    | Kentucky | Tennessee   \n",
    "Colorado   | Washingtoon | Illnois  | Alaska      \n",

    "Phobias\n",
    "Ghosts   | Spiders | Monsters  | Rats          \n",
    "Toilets  | Snakes  | Germs     | Clowns        \n",
    "Needles  | Dogs    | Birds     | Insects       \n",
    "Children | Shadows | Roller Coasters  | Planes \n",
};

const char* truth2QuestionList[] = {
    "What is the most sensitive part of your body?",
    "What's a really good song to have crazy sexy too?",
    "In 3dx describe the perfect environment to have sex in.",
    "Who do you think in this room has the biggest penis?",
    "Where is the most inapproriate place you have masteurbated?",
    "Foreplay or roleplay? Explain",
    "If you could pick anyone who you aren't with to be your sex slave who would it be",
    "On a scale from one to ten rate your bedroom skills. Explain the reason for the score.",
    "What are three adjectives would you use to describe a perfect kiss?",
    "How much do you need to be paid to be in a sex magazine?",
    "When was the last time you watched porn?",
    "Have you ever fantasized about having sex with a teacher. If so which one.",
    "What sexual act turns you off",
    "What is the first thing to do if you were invisible",
    "Which too people in this room would have you a threesome with. Describe in detail.",
    "If you could invent a sex pleasing device what would it do.",
    "How many times a week do you masturbate.",
    "Were is your favorite place to be licked.",
    "Have you ever fantasized about anyone in this room.",
    "Who do you think has the biggest balls or boobs in this room.",
    "Would you ever go to a nude beach. Why or why not.",
    "Describe the sexiest underwear you like.",
    "What kind of underwear are you wearing now?",
    "Whats your favorite position",
    "Are you more of a top or bottom",
    "What was the most embarrassing thing that happened to you during sex",
    "What is the sluttiest thing you have ever done.",
    "Who in this room do you think would be the best in bed and why.",
    "Have you ever faked a orgasm. Why and with who.",
    "Would you ever have sex on a airplane with someone in this room. How would you do it.",
    "What are the top three things that turn you on.",
    "Have you ever peed in someones swimming pool.",
    "What do you like to do after sex in 3dx.",
    "Would you rather cuddle postsex or jump in a hot shower with your partner.",
    "Who here do you think gives the best blowjob in real life.",
    "What is your best idea of roleplay.",
    "What is the craziest thing you have ever done in public.",
    "Who here would you take to a public bathroom and what would you do.",
    "What food or smell puts you in the mood.",
    "If 3dx added kinkier clothes what would you want your partner to wear.",
    "Tell a secret that you have never told anyone.",
    "The group is going to tell you three names; you must rank them in order whod you have sex with.",
    "Describe your in-bed personality in three words.",
    "Someone gives you the name of a ugly celberaty. How far would you go for 100 1000 or 100000 dollars.",
    "Who would you want to go down on right now; describe the scene.",
    "If you were to make a sexy movie with someone here what would the title be.",
    "If you could get tied up by someone in the room who would it be and what would you want them to do.",
    "What item of clothing do you think you look the sexiest in.",
    "Pick a celebratity and describe your hottest sex fantasy with that person.",
    "Have you ever wanted to get your pussy or penis pierced.",
    "How many tattoos do you have and where.",
    "What kind of foreplay would you like to try on 3dx the next time your able too.",
    "If you could have one sex toy in this game what would it be and who would you use it on.",
    "What is your most embarrasing vomit story.",
    "If you had one of the players handcuffed and blindfolded what would you so and say to them and who is it.",
    "How did you discover masturbation.",
    "Pretend you were going to spend a entire day in bed with someone in this room. Describe in detail how it would start and end.",
    "What is sexy about the person closest to you.",
    "When did you lose your viriginity and were you a top or bottom.",
    "What do you like to be called on 3dx during sex.",
    "If you were taking off all your clothes who would get each article of your clothing and why.",
    "Are you a sub or dom in bed.",
    "Who do you regret having sex with.",
    "If you had to create a original sex position what would it be and what would you name it.",
    "Do you like handcuffs",
    "If you got a chance to make one of the players your slave for 24 hours who would that be and what would you make them do.",
    "If you had to stick your hand down a players pants in the room who would it be and what would you grab.",
    "Who here would you want to have sex with in a plane, in a car, and on a train.",
    "What is the strangest place you've had to pee in.",
    "Would you rather be lightly spanked in bed or have a feather run softly over your entire body.",
    "Where is your favorite place to be touched.",
    "Have you ever thought about a celebrity during sex. Who and why.",
    "Which is your most irriating feature.",
    "Where do you feel the most sexy. On the dance floor or getting cozy in the dark quiet lounge.",
    "If someone in this room made a sex video about you and someone else in this room what would they call it and who are those people.",
    "Does the idea of sex in public turn you on.",
    "Describe your idea of the best foreplay.",
    "Who would you want to preform a striptease on you in this room and what would you call it.",
    "Pretend you a critic and give a player here that you have had sex with or want to have sex with a review of their performance in 3dx.",
    "Which of the players do you think lost their virginity first and last.",
    "Choose a player who you haven't had sex with. What would you like to do with with them sexual. Describe in detail.",
    "Where do you think the sexiest place int he world is.",
    "Pick a article of clothing that belongs to the opposite gender that you would wear and why.",
    "What is your secret sexual talent.",
    "What is your most embarrasing moment in 3dx so far.",
    "Which two players in the game would you want to have double penetrating sex with.",
    "Would you do it with someone you didn't like for money.",
    "What is your sexiest feature.",
    "Do you like a mouth gag.",
    "Would you rather be tied up to a bed or a chair during sex.",
    "Have you ever had a crush on a teacher. Who and what did you want to them.",
    "What could happen and be a instant turn off during sex.",
    "Have you ever thought a friends parent was hot.",
    "What is the naughtest thing you have ever looked at online.",
    "Have you ever gone the entire day without wearing underwear.",
    "What is the worst rumor you have participated in.",
    "How do you think the body of the closest person to you would look better. Shaved or natural.",
    "If you cosplaying with the person closest to you what super hero would you both be and what would you be doing.",
    "Who in this room would you be willing to watch porn with.",
    "Move your hands along your lower body and describe your best feature.",
    "What are the most attractive features about the person closest to you.",
    "Would you ever pierce your penis or be turned on by someone who has a pierced penis.",
    "What nether-region landscaping turns you on the most. Tottaly bare, au naturel or creatively trimmed.",
    "Who in this room would turn you on by spanking you.",
    "Of the people playing the game, who do you think has had the most sex partners.",
    "Do you think size matters or performance matters.",
    "Have you ever tasted your own sperm.",
    "Would you rather try a threesome, experience with light sadomasocism, or make a video in the sack.",
    "Would you do a foursome or a orgy",
    "What authority figure do you find totally hot.",
    "If you have any costume at your disposal what would you want to dress up as during sex.",
    "Pick someone who is playing and describe what kind of food you would like to lick off of their body.",
    "Do you prefer to be the top or bottom.",
    "Which do you think is hotter sex standing against a wall or bent over a table.",
    "Describe how someone could please you with only using their tongue.",
    "What is the kinkiest thing you have done with yourself.",
    "How fiersty do you like it. Spanking. Scratching. Biting.",
};

const char* truthQuestionList[] = {
    "Coming to the West End this year, ______: The Musical.",
    "Crikey!I've never seen ______ like this before! Let's get a bit closer.",
    "CTV presents     ______: the Story of ______.    ",
    "Daddy, why is mommy crying ?",
    "Daddy, why is mummy crying ?",
    "Dear Abby, I'm having some trouble with ______ and would like your advice.",
    "Dear Agony Aunt, I'm having some trouble with ________ and I need your advice.",
    "Dear Agony Aunt, I'm having some trouble with ________ and would like your advice.",
    "Dear Sir or Madam, We regret to inform you that the Office of ______ has denied your request for ______.",
    "Doctor, you've gone too far! The human body wasn't meant to withstand that amount of ______!",
    "Dude, do not go in that bathroom.There's ______ in there.",
    "Dude, do not go in that washroom.There's ______ in there.",
    "Due to a PR fiasco, Walmart no longer offers ______.",
    "During his childhood, Salvador Dali produced hundreds of paintings of ______.",
    "During his midlife crisis, my dad got really into ______.",
    "During Picasso's often-overlooked Brown Period, he produced hundreds of paintings of ______.",
    "During sex, I like to think about ______.",
    "Finally!A service that delivers ______ right to your door.",
    "For my next trick, I will pull ______ out of ______.",
    "Fun tip!When your man asks you to go down on him, try surprising him with ______ instead.",
    "Future historians will agree that ______ marked the beginning of America's decline.",
    "Having problems with ______ ? Try ______!",
    "Here is the church Here is the steeple Open the doors And there is ______.    ",
    "Hey guys, welcome to Boston Pizza!Would you like to start the night off right with ______ ?",
    "Hey guys, welcome to Chili's! Would you like to start the night off right with ______?",
    "Hey guys, welcome to Sizzler!Would you like to start the night off right with ______ ?",
    "Hey guys, welcome to TGI Fridays!Would you like to start the night off right with ______ ?",
    "Hey guys, welcome to TGIF!Would you like to start the night off right with ______ ?",
    "Hey Reddit!I'm ______. Ask me anything.",
    "How am I maintaining my relationship status ?",
    "How did I lose my virginity ?",
    "Hulu's new reality show features twelve hot singles living with ______.",
    "I do not know with what weapons World War III will be fought, but World War IV will be fought with ______.",
    "I drink to forget ______.",
    "I get by with a little help from ______.",
    "I got 99 problems but ______ ain't one.",
    "I know when that hotline bling, that can only mean one thing : ______.",
    "I learned the hard way that you can't cheer up a grieving friend with ______.",
    "I never truly understood ______ until I encountered ______.",
    "I spent my whole life working toward ______, only to have it ruined by ______.",
    "I wish I hadn't lost the instruction manual for ______.",
    "I'm going on a cleanse this week. Nothing but kale juice and ______.",
    "I'm Lebron James, and when I'm not slamming dunks, I love ______.",
    "I'm no doctor, but I'm pretty sure what you're suffering from is called     ______.    ",
    "I'm sorry, Professor, but I couldn't complete my homework because of ______.",
    "I'm sorry, Sir, but I couldn't complete my homework because of ______.",
    "I’m not like the rest of you.I’m too rich and busy for ______.",
    "If you can't be with the one you love, love ______.",
    "IF you like ______, YOU MIGHT BE A REDNECK.",
    "In 1,000 years, when paper money is a distant memory, how will we pay for goodsand services ?",
    "In 1,000 years, when paper money is but a distant memory, ______ will be our currency.",
    "In a world ravaged by ______ our only solace is ______.",
    "In a world ravaged by ______, our only solace is ______.",
    "In an attempt to reach a wider audience, the National Museum of Australia has opened an interactive exhibit on ______.",
    "In an attempt to reach a wider audience, the Smithsonian Museum of Natural History has opened an interactive exhibit on ______.",
    "In Australia, ______ is twice as big and twice as deadly.",
    "In Belmarsh Prison, word is you can trade 200 cigarettes for ______.",
    "In her latest feature - length film, Tracy Beaker struggles with ______ for the first time.",
    "In his new self - produced album, Kanye West raps over the sounds of ______.",
    "In his new summer comedy, Rob Schneider is ______ trapped in the body of ______.",
    "In Jordan Peele's new thriller, a young family discovers that ______ had really been ______ all along.",
    "In L.A.County Jail, word is you can trade 200 cigarettes for ______.",
    "In M.Night Shyamalan's new movie, Bruce Willis discovers that ______ had really been ______ all along.",
    "In Michael Jackson's final moments, he thought about ______.",
    "In Rome, there are whisperings that the Vatican has a secret room devoted to ______.",
    "In the new Disney Channel Original Movie, Hannah Montana struggles with ______ for the first time.",
    "In the seventh circle of Hell, sinners must endure ______ for all eternity.",
    "In Wormwood Scrubs, word is you can trade 200 cigarettes for ______.",
    "Instead of coal, Father Christmas now gives the bad children ______.",
    "Instead of coal, Santa now gives the bad children ______.",
    "Introducing the amazing superhero / sidekick duo!It's ______ and ______!",
    "Introducing X - Treme Baseball!It's like baseball, but with ______!",
    "It's a trap!",
    "It’s a pity that kids these days are all getting involved with ______.",
    "Just once, I'd like to hear you say     Thanks, Mom. Thanks for ______.    ",
    "Just once, I'd like to hear you say     Thanks, Mum. Thanks for ______.    ",
    "Just saw this upsetting video!Please retweet!!#stop______",
    "Kids, I don't need drugs to get high. I'm high on ______.",
    "Life for Aboriginal people was forever changed when the white man introduced them to ______.",
    "Life for American Indians was forever changed when the White Man introduced them to ______.",
    "Life was difficult for cavemen before ______.",
    "Lifetime® presents ______ : the Story of ______.",
    "Lifetime® presents     ______: the Story of ______.    ",
    "Lovin’ you is easy ’cause you’re ______.",
    "Major League Baseball has banned ______ for giving players an unfair advantage.",
    "Make a haiku.",
    "Mate, do not go in that bathroom.There's ______ in there.",
    "Mate, do not go in that bathroom.There's ______ in there.",
    "Mate, do not go in that toilet.There's ______ in there.",
    "Maybe she's born with it. Maybe it's ______.",
    "Men's Wearhouse: You're gonna like ______.I guarantee it.",
    "Military historians remember Alexander the Great for his brilliant use of ______ against the Persians.",
    "Mitch McConnell can't cum without ______.",
    "Money can’t buy me love, but it can buy me ______.",
    "Mr.and Mrs.Diaz, we called you in because we're concerned about Cynthia. Are you aware that your daughter is ______?",
    "MTV's new reality show features eight washed-up celebrities living with ______.",
    "My fellow Americans : Before this decade is out, we will have ______ on the moon!",
    "My mom freaked out when she looked at my browser historyand found ______.com / ______.",
    "My plan for world domination begins with ______.",
    "Next from J.K.Rowling : Harry Potterand the Chamber of ______.",
    "Next from J.K.Rowling : Harry Potterand the Chamber of ______.",
    "Next on ESPN2 : The World Series of ______.",
    "Next on Eurosport : The World Championship of _______.",
    "Next on Nine's Wide World of Sports: The World Championship of ______.",
    "Next on Sky Sports : The World Championship of ________.",
    "Next on Sky Sports : The World Champsion of ________.",
    "Next on TSN : The World Series of ______.",
    "Next up on Channel 4 : Ramsay's ______ Nightmares.",
    "Nobody expects the Spanish Inquisition.Our chief weapons are fear, surprise,and ________.",
    "Now at the National Museum of Australia : an interactive exhibit on ______.",
    "Now at the Natural History Museum : an interactive exhibit on ______.",
    "Now at the Royal Ontario Museum : an interactive exhibit on ______.",
    "Now at the Smithsonian : an interactive exhibit on ______.",
    "O Canada, we stand on guard for ______.",
    "Oi!Show us ______!",
    "Old MacDonald had ______.E - I - E - I - O.",
    "Only two things in life are certain : death and ______.",
    "Penalty!______ : that's 5 minutes in the box!",
    "Qantas now prohibits ______ on airplanes.",
    "Rumor has it that Vladimir Putin's favorite dish is ______ stuffed with ______.",
    "Science will never explain ______.",
    "Skidamarink a dink a dink, skidamarink a doo, I love ______.",
    "Sorry everyone, I just ______.",
    "Step 1: ______. Step 2: ______. Step 3 : Profit.    ",
    "Studies show that lab rats navigate mazes 50 % faster after being exposed to ______.",
    "TFL apologizes for the delay in train service due to ______.",
    "That's right, I killed ______. How, you ask? ______.",
    "The blind date was going horribly until we discovered our shared interest in ______.",
    "The class field trip was completely ruined by ______.",
    "The Five Stages of Grief : denial, anger, bargaining, ______, acceptance.",
    "The healing process began when I joined a support group for victims of ______.",
    "The Natural History Museum has just opened an interactive exhibit on ________.",
    "The new Chevy Tahoe.With the powerand space to take ______ everywhere you go.",
    "The school excursion was completely ruined by ______.",
    "The school field trip was completely ruined by ______.",
    "The school trip was completely ruined by ______.",
    "The Smithsonian Museum of Natural History has just opened an interactive exhibit on ______.",
    "The TFL apologizes for the delay in train service due to ________.",
    "The theme for next year's Eurovision Song Contest is     We are ________.    ",
    "The U.S.has begun airdropping ______ to the children of Afghanistan.",
    "They said we were crazy.They said we couldn't put ______ inside of ______. They were wrong.",
    "    This is the way the world ends",
    "This is the way the world ends",
    "Not with a bang but with ______.    ",
    "This is your captain speaking.Fasten your seatbelts and prepare for ______.",
    "This season at Steppenwolf, Samuel Beckett's classic existential play: Waitng for ______.",
    "This season at the Old Vic, Samuel Beckett's classic existential play: Waiting for ______.",
    "This season at the old Vic, Samuel Beckett's classic existential play: Waitng for ______.",
    "This season at the Princess of Wales Theatre, Samuel Beckett's classic existential play: Waiting for ______.",
    "This season at the Sydney Opera House, Samuel Beckett's classic existential play: Waiting for ______.",
    "Today on Jerry Springer :     Help! My son is ______!    ",
    "Today on Maury :     Help! My son is ______!    ",
    "Today on The Jeremy Kyle Show :     Help! My son is ________!    ",
    "Tonight's top story: What you don't know about ________ could kill you.",
    "TSA guidelines now prohibit ______ on airplanes.",
    "Turns out that ______ - Man was neither the hero we needed nor wanted.",
    "Uh, hey guys, I know this was my idea, but I'm having serious doubts about ______.",
    "UKIP : Putting ______ First!",
    "War!What is it good for ?",
    "Well if you'll excuse me, gentlemen, I have a date with ______.",
    "What am I giving up for Lent ?",
    "What are my parents hiding from me ?",
    "What are school administrators using to curb rampant teenage pregnancy ?",
    "What broke up the original Wiggles ?",
    "What brought the orgy to a grinding halt ?",
    "What did I bring back from Amsterdam ?",
    "What did I bring back from Bali ?",
    "What did I bring back from Mexico ?",
    "What did the US airdrop to the children of Afghanistan ?",
    "What did Vin Diesel eat for dinner ?",
    "What do old people smell like ?",
    "What does Dick Cheney prefer ?",
    "What don't you want to find in your Chinese food?",
    "What don't you want to find in your doner kebab?",
    "What don't you want to find in your Kung Pao chicken?",
    "What don't you want to find in your Mongolian beef?",
    "What ended my last relationship ?",
    "What gets better with age ?",
    "What gives me uncontrollable gas ?",
    "What has been making life difficult at the nudist colony ?",
    "What helps Barack Obama unwind ?",
    "What helps Obama unwind ?",
    "What is Batman's guilty pleasure?",
    "What is George W.Bush thinking about right now ?",
    "What kept Margaret Thatcher busy in her waning years ?",
    "What left this stain on my couch ?",
    "What made my first kiss so awkward ?",
    "What makes life worth living ?",
    "What makes me a true blue Aussie ?",
    "What never fails to liven up the party ?",
    "What will always get you laid ?",
    "What will I bring back in time to convince people that I am a powerful wizard ?",
    "What would grandma find disturbing, yet oddly charming ?",
    "What's a girl's best friend ?",
    "What's my anti-drug?",
    "What's my secret power?",
    "What's Teach for America using to inspire inner city students to succeed?",
    "What's that smell?",
    "What's that sound?",
    "What's the Canadian government using to inspire rural students to succeed?",
    "What's the crustiest?",
    "What's the most emo?",
    "What's the new fad diet?",
    "What's the next Happy Meal® toy?",
    "What's the next superhero/sidekick duo?",
    "What's there a ton of in heaven?",
    "What's there a tonne of in heaven?",
    "When all else fails, I can always masturbate to ______.",
    "When I am a billionare, I shall erect a 20 - meter statue to commemorate ______.",
    "When I am a billionare, I shall erect a 20 - metre statue to commemorate ______.",
    "When I am a billionare, I shall erect a 50 - foot statue to commemorate ______.",
    "When I am President of the United States, I will create the Department of ______.",
    "When I am President, I will create the Department of ______.",
    "When I am Prime Minister of Canada, I will create the Department of ______.",
    "When I am Prime Minister of Canada, I will create the Ministry of ______.",
    "When I am Prime Minister of the United Kingdom, I will create the Ministry of ______.",
    "When I am Prime Minister, I will create the Department of ______.",
    "When I am Prime Minister, I will create the Ministry of ______.",
    "When I pooped, what came out of my butt ?",
    "When I was tripping on acid, ______ turned into ______.",
    "When I'm in prison, I'll have ______ smuggled in.",
    "When Pharaoh remained unmoved, Moses called down a Plague of ______.",
    "When you get right down to it, ______ is just ______.",
    "While the United States raced the Soviet Union to the moon, the Mexican government funneled millions of pesos into research on ______.",
    "White people like ______.",
    "Who stole the cookies from the cookie jar ?",
    "Why am I sticky ?",
    "Why can't I sleep at night?",
    "Why do I hurt all over ?",
    "Why is Brett so sweaty ?",
    "With enough time and pressure, ______ will turn into ______.",
    "Your persistence is admirable, my dear Prince.But you cannot win my heart with ______ alone",
    "An international tribunal has found ______ guilty of ______.",
    "And I would have gotten away with it, too, if it hadn't been for ______!",
    "Awww, sick! I just saw this skater do a 720 kickflip into ______!",
    "Dear Sir or Madam, We regret to inform you that the Office of ______ has denied your request for ______.",
    "Doctor, you've gone too far! The human body wasn't meant to withstand that amount of ______!",
    "Future historians will agree that ______ marked the beginning of America's decline.",
    "He who controls ______ controls the world.",
    "I learned the hard way that you can't cheer up a grieving friend with ______.",
    "In a pinch, ______ can be a suitable substitute for ______.",
    "In his new self-produced album, Kanye West raps over the sounds of ______.",
    "In its new tourism campaign, Detroit proudly proclaims that it has finally eliminated ______.",
    "In Rome, there are whisperings that the Vatican has a secret room devoted to ______.",
    "In the distant future, historians will agree that ______ marked the beginning of America's decline.",
    "Michael Bay's new three-hour action epic pits ______ against ______.",
    "My plan for world domination begins with ______.",
    "Next season on Man vs, Wild, Bear Grylls must survive the depths of the Amazon with only ______ and his wits.",
    "Next week on Discovery Channel, one man must survive in the depths of the Amazon with only _____ and his wits.",
    "Science will never explain ______.",
    "Science will never explain the origin of ______.",
    "The CIA now interrogates enemy agents by repeatedly subjecting them to ______.",
    "The socialist governments of Scandinavia have declared that access to ______ is a basic human right.",
    "This season on Man vs. Wild, Bear Grylls must survive in the depths of the Amazon with only ______ and his wits.",
    "What brought the orgy to a grinding halt?",
    "What has been making life difficult at the nudist colony?",
    "What's the gift that keeps on giving?",
    "When all else fails, I can always masturbate to ______.",
    "______ may pass, but ______ will last forever.",
    "______ will never be the same after ______.",
    "This is madness. No, THIS IS ______!",
    "2 AM in the city that never sleeps. The door swings open and she walks in, legs up to here. Something in her eyes tells me she's looking for ______.",
    "Adventure. Romance. ______. From Paramount Pictures, ______.",
    "And today's soup is Cream of ______.",
    "And would you like those buffalo wings mild, hot, or ______?",
    "Armani suit: $1,000. Dinner for two at that swanky restaurant: $300. The look on her face when you surprise her with ______: priceless.",
    "As king, how will I keep the peasants in line?",
    "Behind every powerful man is ______.",
    "Bitch, you're nasty. You're disgusting. You're _______. Go home, bitch.",
    "Come to Dubai, where you can relax in our world-famous spas, experience the nightlife, or simply enjoy ______ by the poolside.",
    "Dammit Gary. You can't just solve every problem with ______.",
    "Dear Leader Kim Jong-un, our village praises your infinite wisdom with a humble offering of ______.",
    "Do not fuck with me! I am literally ______ right now.",
    "Do the Dew with our most extreme flavor yet! Get ready for Mountain Dew ______!",
    "Do you lack energy? Does it sometimes feel like the whole world is ______ ? Ask your doctor about Zoloft.®",
    "Don't forget! Beginning this week, Casual Friday will officially become ______ Friday.",
    "Don't worry kid. It gets better. I've been living with ______ for 20 years.",
    "Every step towards ______ gets me a little bit closer to ______.",
    "Everybody join hands and close your eyes. Do you sense that? That's the presence of ______ in this room.",
    "Forget everything you know about ______, because now we've supercharged it with ______!",
    "Get ready for the movie of the summer! One cop plays by the book. The other's only interested in one thing: ______.",
    "Having the worst day EVER. #______",
    "Heed my voice, mortals! I am the god of ______ , and I will not tolerate ______!",
    "Help me doctor, I've got ______ in my butt!",
    "Here at the Academy for Gifted Children, we allow students to explore ______ at their own pace.",
    "Hi MTV! My name is Kendra, I live in Malibu, I'm into ______, and I love to have a good time.",
    "Hi, this is Jim from accounting. We noticed a $1,200 charge labeled ______. Can you explain?",
    "Honey, I have a new role-play I want to try tonight! You can be ______, and I'll be ______.",
    "How am I compensating for my tiny penis?",
    "I am become ______, destroyer of ______!",
    "I don't mean to brag, but they call me the Micheal Jordan of ______.",
    "I have a strict policy. First date, dinner. Second date, kiss. Third date, ______.",
    "I work my ass off all day for this family, and this is what I come home to? ______!?",
    "I'm Miss Tennessee, and if I could make the world better by changing one thing, I would get rid of ______.",
    "I'm pretty sure I'm high right now, because Im absolutely mesmerized by ______.",
    "I'm sorry, Mrs. Chen, but there was nothing we could do. At 4:15 this morning, your son succumbed to ______.",
    "I'm sorry, sir, but we don't allow ______ at the country club.",
    "If you can't handle ______, you'd better stay away from ______.",
    "If you had to describe the Card Czar, using only one of the cards in your hand, which one would it be?",
    "In his farewell address, George Washington famously warned Americans about the dangers of ______.",
    "In his new action comedy, Jackie Chan must fend off ninjas while also dealing with ______.",
    "In return for my soul, the Devil promised me ______, but all I got was ______.",
    "In the beginning, there was ______. And the Lord said, Let there be ______.",
    "It lurks in the night. It hungers for flesh. This summer, no one is safe from ______.",
    "James is a lonely boy. But when he discovers a secret door in his attic, he meets a magical new friend: ______.",
    "Life's pretty tough in the fast lane. That's why I never leave the house without ______.",
    "Listen Gary, I like you. But if you want that corner office, you're going to have to show me ______.",
    "Man, this is bullshit. Fuck ______.",
    "My grandfather worked his way up from nothing. When he came to this country, all he had was the shoes on his feet and ______.",
    "Now in bookstores: The Audacity of ______ by Barack Obama.",
    "Oprah's book of the month is ______ For ______: A Story of Hope.",
    "Patient presents with ______. Likely a result of ______.",
    "Puberty is a time of change. You might notice hair growing in new places. You might develop an intrest in ______. This is normal.",
    "She's up all night for good fun. I'm up all night for ______.",
    "The Japanese have developed a smaller, more efficient version of ______.",
    "The six things I could never do without: oxygen, Facebook, chocolate, Netflix, friends, and ______ LOL!",
    "This is America. If you don't work hard, you don't succeed. I don't care if you're black, white, purple, or ______.",
    "This is the prime of my life. I'm young, hot, and full of ______.",
    "This year's hottest album is ______ by ______.",
    "To become a true Yanomami warrior, you must prove that you can withstand ______ without crying out.",
    "To become a true Yanomamo warrior, you must prove that you can withstand ______ without crying out.",
    "Tonight we will have sex. And afterwards, If you'd like, a little bit of ______.",
    "We never did find ______, but along the way we sure learned a lot about ______.",
    "Well if ______ is good enough for ______, it's good enough for me.",
    "Well what do you have to say for yourself, Casey? This is the third time you've been sent to the principal's office for ______.",
    "Wes Anderson's new film tells the story of a precocious child coming to terms with ______.",
    "What killed my boner?",
    "What's fun until it gets weird?",
    "What's making things awkward in the sauna?",
    "When I was a kid, we used to play Cowboys and ______.",
    "WHOOO! God damn I love ______!",
    "Why am I broke?",
    "Why won't you make love to me anymore? Is it ______?",
    "Y'all ready to get this thing started? I'm Nick Cannon, and this is America's Got ______.",
    "Yo' mama's so fat she ______!",
    "You are not alone. Millions of Americans struggle with ______ every day.",
    "You guys, I saw this crazy movie last night. It opens on ______, and then there's some stuff about ______, and then it ends with ______.",
    "You know, once you get past ______, ______ ain't so bad.",
    "You Won't Believe These 15 Hilarious ______ Bloopers!",
    "You've seen the bearded lady! You've seen the ring of fire! Now, ladies and gentlemen, feast your eyes upon ______!",
    "A new adventure awaits at Walt Disney's Magical Kingdom of ______!",
    "After phasing out orca shows, SeaWorld is now attracting customers with ______.",
    "Alright, I'm done with ______. It's time for _____.",
    "And now, from WHYY in Philadelphia, it's ______ with Terry Gross.",
    "As a feminist, I cannot support ______.",
    "Every Tuesday, I purchase a box of donuts. I sit on the toilet. I eat the donuts. I remember ______, and I cry.",
    "Few remember Beethoven's lesser-known symphony, Ode to ______.",
    "FIRST LOOK: Nike drops hot new sneakers designed exclusively for ______.",
    "Hey everybody! It's ______ Guy.",
    "Hey, check out my podcast! It's just two regular guys talking about ______.",
    "Holy MOLY! Now THAT'S what I call ______!",
    "How am I, the Card Czar, going to die?",
    "I only saw my father cry twice: once after Mom died, and once after ______.",
    "I'm ______, you're ______, let's party!",
    "I'm Ted Cruz, and I'm proud to endorse ______.",
    "I'm that bad type",
    "Make your mama sad type",
    "Might seduce your dad type",
    "I'm ______.",
    "Incredible! Biologists have discovered an octopus capable of ______.",
    "It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in want of ______.",
    "It's very simple.",
    "You give me sex.",
    "I give you ______.",
    "Join our peaceful community. We eat grapes. We live in tents. We enjoy ______, together.",
    "Lord, please guide my baby girl back to you and away from ______. Amen.",
    "Lucky to be ALIVE! Kylie Jenner's TERRIFYING encounter with ______.",
    "Me: Hey, Randy! What's going on?",
    "Randy: Aw nothing, man. Just ______.",
    "Men like ______.",
    "Mom! Mom! Look at me, Mom! I'm ______!",
    "Momma always said, Life is like ______. You never know what you're gonna get.",
    "Mr. Musk, as your legal counsel, I must strongly advise against ______.",
    "My favorite rapper is Lil ______.",
    "My penis is ______.",
    "My vagina is ______.",
    "Now on Disney+! Pirates of the Caribbean: The Curse of ______.",
    "Pssst. You go around back. I'll distract the guards with ______.",
    "Ready or not, here comes ______!",
    "The first rule of Fight Club is: don't talk about Fight Club.",
    "The second rule of Fight Club is: don't talk about ______.",
    "The year is 2150. The president is ______.",
    "There is a video circulating the Internet that does not represent who I am. I made a vile mistake in my youth, and I deeply regret it. Please don't watch the ______ video.",
    "Today's technical challenge comes from Paul Hollywood. He would like six identical pastries in the shape of ______.",
    "Two roads diverged in a yellow wood--",
    "I chose ______,",
    "And that has made all the difference.",
    "Up next on Fox News: Are college students being radicalized by ______?",
    "WARNING: ______ may cause ______.",
    "Welcome the jungle! We've got ______.",
    "Welcome to Hidden Creek Water Park. Please remember that we do not allow ______ in the pool.",
    "What's better when clowns are involved?",
    "What's big, sweaty, and fantastic?",
    "What's kind of sad, kind of beautiful, but also kind of funny?",
    "Yo, is _____racist?",
    "You ______, bro!",
    "You're on a first date. What's an instant red flag?"
};

const char* responseCard[] = {
    "2 Girls 1 Cup.",
"400 years of colonial atrocities.",
"50 mg of Zoloft daily.",
"50,000 volts straight to the nipples.",
"72 virgins.",
"8 oz. of sweet Mexican black-tar heroin.",
"A bag of magic beans.",
"A balanced breakfast.",
"A ball of earwax, semen, and toenail clippings.",
"A big black dick.",
"A big hoopla about nothing.",
"A bird that shits human turds.",
"A bit of slap and tickle.",
"A bitch slap.",
"A bleached arsehole.",
"A bleached asshole.",
"A Bop It.",
"A bowl of mayonnaise and human teeth.",
"A brain tumor.",
"A brain tumour.",
"A bucket of fish heads.",
"A can of whoop-ass.",
"A caress of the inner thigh.",
"A cartoon camel enjoying the smooth, refreshing taste of a cigarette.",
"A cat video so cute that your eyes roll back and your spine slides out of your anus.",
"A Chelsea smile. ",
"A Chinese tourist who wants something very badly but cannot communicate it.",
"A clandestine butt scratch.",
"A comprehensive understanding of the Irish backstop.",
"A cooler full of organs.",
"A cop who is also a dog.",
"A crucifixion.",
"A cute, fuzzy koala, but it has chlamydia.",
"A death ray.",
"A decent fucking Internet connection.",
"A deep-rooted fear of the working class.",
"A defective condom.",
"A despondent Maple Leafs fan sitting all alone.",
"A didgeridildo.",
"A disappointing birthday party.",
"A drive-by shooting.",
"A fair go.",
"A falcon with a cap on its head.",
"A fanny fart.",
"A fart so powerful that it wakes the giants from their thousand-year slumber.",
"A fat bald man from the internet.",
"A fetus.",
"A five-litre goon bag.",
"A Fleshlight.",
"A Fleshlight.",
"A foetus.",
"A foul mouth.",
"A fuck-ton of almonds.",
"A fuck-tonne of almonds.",
"A gassy antelope.",
"A general lack of purpose.",
"A gentle caress of the inner thigh.",
"A ginger's freckled ballsack.",
"A Ginsters pasty and three cans of Monsters Energy.",
"A good sniff.",
"A good, strong gorilla.",
"A gossamer stream of jizz that catches the light as it arcs through the morning air.",
"A grande sugar-free iced soy caramel macchiato.",
"A Gypsy curse.",
"A hairless little shitstain named Callou.",
"A Halal Snack Pack.",
"A hen night in Slough.",
"A homoerotic volleyball montage.",
"A horde of Vikings.",
"A hot mess.",
"A Japanese toaster you can fuck.",
"A Japanese whaling operation.",
"A LAN party.",
"A lifetime of sadness.",
"A literal tornado of fire.",
"A little boy who won't shut the fuck up about dinosaurs.",
"A live studio audience.",
"A look-see.",
"A low standard of living.",
"A mad cow.",
"A madman who lives in a police box and kidnaps women.",
"A man in yoga pants with a ponytail and feather earrings.",
"A man on the brink of orgasm.",
"A mating display.",
"A meat raffle!",
"A Mexican.",
"A micropenis.",
"A micropig wearing a tiny raincoat and booties.",
"A middle-aged man on roller skates.",
"A mime having a stroke.",
"A moment of silence.",
"A monkey smoking a cigar.",
"A mopey zoo lion.",
"A much younger woman.",
"A murder most foul.",
"A narc.",
"A neglected Tamagotchi.",
"A nice cup of tea.",
"A pangender octopus who roams the cosmos in search of love.",
"A piñata full of scorpions.",
"A posh wank.",
"A PowerPoint presentation.",
"A pyramid of severed heads.",
"A really cool hat.",
"A robust mongoloid.",
"A sad fat dragon with no friends.",
"A sad handjob.",
"A salad for men that's made of metal.",
"A salty surprise.",
"A sassy black woman.",
"A sausage festival.",
"A saxophone solo.",
"A sea of troubles.",
"A shark!",
"A sick burnout.",
"A sick wombat.",
"A sickly child-king.",
"A six-point plan to stop the boats.",
"A slab of VB and a pack of durries.",
"A slightly salty toad in the hole.",
"A slightly shittier parallel universe.",
"A snapping turtle biting the tip of your penis.",
"A sober Irishman who doesn't care for potatoes.",
"A soggy Sao.",
"A sorry excuse for a father.",
"A spastic nerd.",
"A stingray barb through the chest.",
"A stray pube.",
"A subscription to Men's Fitness.",
"A Super Soaker full of cat pee.",
"A supportive touch on the lower back.",
"A surprising amount of hair.",
"A sweet spaceship.",
"A thermonuclear detonation.",
"A thousand Scottish warriors lifting their kilts in unison.",
"A three-way with my wife and Shaquille O'Neal.",
"A Tim Hortons franchise.",
"A time travel paradox.",
"A tiny horse.",
"A tiny, gay guitar called a ukulele.",
"A tribe of warrior women.",
"A vagina that leads to another dimension.",
"A vajazzled vagina.",
"A vastly superior healthcare system.",
"A vindaloo poo.",
"A web of lies.",
"A wheelchair death race.",
"A white ethnostate.",
"A white van man.",
"A whole thing of butter.",
"A windmill full of corpses.",
"A wisecracking terrorist.",
"A zesty breakfast burrito.",
"Aaron Burr.",
"Abstinence.",
"Academy Award winner Meryl Streep.",
"Accepting the way things are.",
"Accidentally slipping yourself a roofie.",
"Active listening.",
"Actually getting shot, for real.",
"Actually taking candy from a baby.",
"Adderall.",
"Adult Friendfinder.",
"Advice from a wise, old black man.",
"African children.",
"Agriculture.",
"AIDS.",
"Ainsley Harriott.",
"Alcohol poisoning.",
"Alcoholism.",
"Alexandria Ocasio-Cortez.",
"All four prongs of an echidna's penis.",
"All my friends dying.",
"All my gentleman suitors.",
"All of this blood.",
"All the dudes I've fucked.",
"All-you-can-eat shrimp for $4.99.",
"All-you-can-eat shrimp for $8.99.",
"Altar boys.",
"America.",
"American Gladiators.",
"Americanization.",
"Amputees.",
"An AK-47 assault rifle.",
"An AK-47.",
"An AR-15 assault rifle.",
"An argument with Richard Dawkins.",
"An army of skeletons.",
"An ass disaster.",
"An asymmetric boob job.",
"An endless stream of diarrhea.",
"An endless stream of diarrhoea.",
"An entrenched class system.",
"An erection that lasts longer than four hours.",
"An Evening with Michael Buble.",
"An evil man in evil clothes.",
"An honest cop with nothing left to lose.",
"An icepick lobotomy.",
"An icy handjob from an Edmonton hooker.",
"An M. Night Shyamalan plot twist.",
"An M16 assault rifle.",
"An octopus giving seven handjobs and smoking a cigarette.",
"An Oedipus complex.",
"An old guy who's almost dead.",
"An older woman who knows her way around the penis.",
"An oversized lollipop.",
"An ugly face.",
"An unstoppable wave of fire ants.",
"An unwanted pregnancy.",
"An uppercut.",
"Anal beads.",
"Announcing that I am about to cum.",
"Another goddamn vampire movie.",
"Another shot of morphine.",
"Anything that comes out of Prince Philip's mouth.",
"Apologizing.",
"Arnold Schwarzenegger.",
"Asians who aren't good at math.",
"Assless chaps.",
"Attitude.",
"Auschwitz.",
"Australia.",
"Authentic Mexican cuisine.",
"Autocannibalism.",
"AXE Body Spray.",
"Backing over a kid with the Buick.",
"Badger culling.",
"Ball-by-ball commentary from Richie Benaud.",
"Balls.",
"Bananas in Pajamas.",
"Bananas.",
"Barack Obama.",
"Barely making $25,000 a year.",
"Barely making £15,000 a year.",
"Basic human decency.",
"BATMAN!",
"BATMAN!!!",
"Battlefield amputations.",
"Becoming a blueberry.",
"Bees?",
"Being a busy adult with many important things to do.",
"Being a dick to children.",
"Being a dinosaur.",
"Being a hideous beast that no one could love.",
"Being a motherfucking sorcerer.",
"Being a witch.",
"Being a woman.",
"Being able to talk to elephants.",
"Being awesome at sex.",
"Being black.",
"Being Canadian.",
"Being fabulous.",
"Being fat and stupid.",
"Being fucking stupid.",
"Being hunted like a fox.",
"Being marginalised.",
"Being marginalized.",
"Being on fire.",
"Being rich.",
"Being white.",
"Benedict Cumberbatch.",
"Bestiality.",
"Bill Nye the Science Guy.",
"Bingeing and purging.",
"Bio-engineered assault turtles with acid breath.",
"Bisexuality.",
"Bitches.",
"Black Jesus.",
"Black people.",
"Bling.",
"Blood farts.",
"Blood, sweat, and tears.",
"Blood, toil, tears, and sweat.",
"Blowing my boyfriend so hard he shits.",
"Blowing up Parliament.",
"Boat people; half boat, half human.",
"Bogies.",
"Bond, James Bond.",
"Bono.",
"Booby-trapping the house to foil burglars.",
"Boogers.",
"Boris Johnson.",
"Bosnian chicken farmers.",
"Braiding three penises into a Curly Wurly.",
"Braiding three penises into a licorice twist.",
"Braiding three penises into a Twizzler.",
"Breaking out into song and dance.",
"Brexit.",
"Britney Spears at 55.",
"Brown people.",
"Brutal austerity.",
"Bubble butt bottom boys.",
"Buckfast Tonic Wine.",
"Bullshit.",
"Burgers and pussy.",
"Burning down the White House.",
"Buying the right pants to be cool.",
"Canada: America's Hat.",
"Canadian Netflix.",
"Canned tuna with extra dolphin.",
"Capturing Newt Gingrich and forcing him to dance in a monkey suit.",
"Cardi B.",
"Cards Against Humanity.",
"Cashed-up bogans.",
"Casually suggesting a threesome.",
"Catapults.",
"Centaurs.",
"Chainsaws for hands.",
"Charisma.",
"Cheating in the Paralympics.",
"Cheating in the Special Olympics.",
"Cheeky bum sex.",
"Chemical weapons.",
"Child abuse.",
"Child beauty pageants.",
"Children on leashes.",
"Chivalry.",
"Christopher Walken.",
"Chundering into a kangaroo's pouch.",
"Chunks of dead backpacker.",
"Chunks of dead hitchhiker.",
"Chunks of dead prostitute.",
"Chutzpah.",
"Civilian casualties.",
"Classist undertones.",
"Climbing a telephone pole and becoming one with the T-Mobile network.",
"Clive Palmer's soft, shitty body.",
"Clubbing baby seals.",
"Coat hanger abortions.",
"Cock.",
"Cockfights.",
"College.",
"Committing suicide.",
"Completely unwarranted confidence.",
"Concealing a boner.",
"Concealing an erection.",
"Consensual sex.",
"Consultants.",
"Contagious face cancer.",
"Converting to Islam.",
"Cookie Monster devouring the Eucharist wafers.",
"Copping a feel.",
"Cottaging.",
"Count Chocula.",
"Court-ordered rehab.",
"Covering myself with Parmesan cheese and chili flakes because I am pizza.",
"COVID-19.",
"Crab.",
"Crazy hot cousin sex.",
"Creed.",
"Crippling debt.",
"Crucifixion.",
"Crumbs all over the bloody carpet.",
"Crumbs all over the god damn carpet.",
"Crumpets with the Queen.",
"Crystal meth.",
"Cuddling.",
"Cumming deep inside my best bro.",
"Cunnilingus.",
"Customer service representatives.",
"Cybernetic enhancements.",
"Dad's funny balls.",
"Daddies Brown Sauce",
"Daddies Brown Sauce.",
"Daddy issues.",
"Daddy's belt.",
"Daniel Radcliffe's delicious arsehole.",
"Daniel Radcliffe's delicious asshole.",
"Danny DeVito.",
"Danny Dyer.",
"Dark and mysterious forces beyond our control.",
"Darth Vader.",
"Date rape.",
"Dave Matthews Band.",
"David Bowie flying in on a tiger made of lightning.",
"David Cameron.",
"Dead babies.",
"Dead birds everywhere.",
"Dead parents.",
"Deflowering the princess.",
"Dental dams.",
"Denying climate change.",
"Destroying the evidence.",
"Dick Cheney.",
"Dick fingers.",
"Dick pics.",
"Dining with cardboard cutouts of the cast of Friends.",
"Dirty nappies.",
"Disco fever.",
"Discovering he's a Tory.",
"Diversity.",
"Dogging.",
"Doin' it in the butt.",
"Doin' it up the bum.",
"Doing a shit in Pudsey Bear's eyehole.",
"Doing crimes.",
"Doing it in the butt.",
"Doing the right thing.",
"Domino's Oreo Dessert Pizza.",
"Don Cherry's wardrobe.",
"Donald J. Trump.",
"Donald Trump.",
"Double penetration.",
"Douchebags on their iPhones.",
"Dr. Martin Luther King, Jr.",
"Drinking alone.",
"Drinking out of the toilet and eating garbage.",
"Drinking out of the toilet and eating rubbish.",
"Dropping a baby down the dunny.",
"Dropping a chandelier on your enemies and riding the rope up.",
"Drowning the kids in the bathtub.",
"Druids.",
"Drum circles.",
"Dry heaving.",
"Dwarf tossing.",
"Dwayne The Rock Johnson.",
"Dying alone and in pain.",
"Dying of dysentery.",
"Dying.",
"Eastern European Turbo-Folk music.",
"Eating a hard boiled egg out of my husband's arsehole.",
"Eating a hard boiled egg out of my husband's asshole.",
"Eating all of the cookies before the AIDS bake-sale.",
"Eating an albino.",
"Eating the last known bison.",
"Eating too much of a lamp.",
"Ecstasy.",
"Ed Balls.",
"Edible underpants.",
"Edible underwear.",
"Egging an MP.",
"Elderly Japanese men.",
"Electricity.",
"Embryonic stem cells.",
"Emerging from the sea and rampaging through Tokyo.",
"Emma Watson.",
"Emotions.",
"England.",
"Erectile dysfunction.",
"Establishing dominance.",
"Estrogen.",
"Ethnic cleansing.",
"Eugenics.",
"Euphoria by Calvin Klein.",
"Exactly what you'd expect.",
"Excalibur.",
"Exchanging pleasantries.",
"Executing a hostage every hour.",
"Existentialists.",
"Existing.",
"Expecting a burp and vomiting on the floor.",
"Explaining how vaginas work.",
"Explaining the difference between sex and gender.",
"Explosions.",
"Extremely tight jeans.",
"Extremely tight pants.",
"Extremely tight trousers.",
"Facebook.",
"Fading away into nothingness.",
"Faffing about.",
"Faith healing.",
"Fake tits.",
"FALCON PUNCH!!!",
"Famine.",
"Fancy Feast.",
"Farting and walking away.",
"Farting into your petticoats and wafting it at Lord Gregory.",
"Fear itself.",
"Feeding Rosie O'Donnell.",
"Fellowship in Christ.",
"Fiery poops.",
"Fiery poos.",
"Figgy pudding.",
"Filling my briefcase with business stuff.",
"Filling my son with spaghetti.",
"Filling Sean Hannity with helium and watching him float away.",
"Finger painting.",
"Fingering.",
"Firing a rifle into the air while balls deep in a squealing hog.",
"Fisting.",
"Five litres of Special Brew.",
"Five-Dollar Footlongs.",
"Flash flooding.",
"Flat out not giving a shit.",
"Flavored condoms.",
"Flesh-eating bacteria.",
"Flightless birds.",
"Flying robots that kill people.",
"Flying sex snakes.",
"Forced sterilisation.",
"Forced sterilization.",
"Foreskin.",
"Forgetting the Alamo.",
"Former President George W. Bush.",
"Four Loko.",
"Fox News.",
"Fragile masculinity.",
"Free samples.",
"Friction.",
"Friendly fire.",
"Friends who eat all the snacks.",
"Friends who eat all your snacks.",
"Friends with benefits.",
"Frolicking.",
"Front butt.",
"Fucking my sister.",
"Fucking the weatherman on live television.",
"Full frontal nudity.",
"Funky fresh rhymes.",
"Gandalf.",
"Gandhi.",
"Garth Brooks.",
"Gary Coleman.",
"Gary Glitter.",
"Gay aliens.",
"Gay conversion therapy.",
"Geese.",
"Genetically engineered super-soldiers.",
"Genghis Khan.",
"Genital piercings.",
"Gentleman's Relish.",
"Genuine human connection.",
"German Chancellor Angela Merkel.",
"German dungeon porn.",
"Germans on holiday.",
"Getting a DUI on a Zamboni.",
"Getting crushed by a vending machine.",
"Getting cummed on.",
"Getting drugs off the street and into my body.",
"Getting drunk on mouthwash.",
"Getting fingered.",
"Getting into a pretty bad car accident.",
"Getting naked and watching CBeebies.",
"Getting naked and watching Nickelodeon.",
"Getting naked and watching Play School.",
"Getting pregnant again.",
"Getting really high.",
"Getting so angry that you pop a boner.",
"Getting so angry that you pop a stiffy.",
"Getting the same Boots Meal Deal every day for six years.",
"Ghosts.",
"Girls who shouldn't go wild.",
"Girls.",
"Giving 110%.",
"Giving birth to the Antichrist.",
"Gladiatorial combat.",
"Glassing a wanker.",
"Glenn Beck being harried by a swarm of buzzards.",
"Glenn Beck catching his scrotum on a curtain hook.",
"Glenn Beck convulsively vomiting.",
"Glenn Beck.",
"Global warming.",
"Gloryholes.",
"Goats eating cans.",
"Goblins.",
"God.",
"GoGurt.",
"Going an entire day without masturbating.",
"Going around punching people.",
"Golden showers.",
"Good-natured, fun-loving Aussie racism.",
"Grabbing my man by his love handles and fucking his big ass.",
"Grandma.",
"Grandpa's ashes.",
"Graphic violence, adult language, and some sexual content.",
"Grave robbing.",
"Growing a pair.",
"Guys who don't call.",
"Haggis.",
"Half a kilo of pure China White heroin.",
"Half-assed foreplay.",
"Harry Potter erotica.",
"Have some more kugel.",
"Having a Golden Gaytime.",
"Having a penis.",
"Having a shag in the back of a ute.",
"Having a stroke.",
"Having anuses for eyes.",
"Having big dreams but no realistic way to achieve them.",
"Having sex for the first time.",
"Having sex on top of a pizza.",
"Having sex with every man in Winnipeg.",
"Having shotguns for legs.",
"Heartwarming orphans.",
"Heath Ledger.",
"Helplessly giggling at the mention of Hutus and Tutsis.",
"Her Majesty, Queen Elizabeth II.",
"Her Royal Highness, Queen Elizabeth II.",
"Heritage minutes.",
"Heritage minutes.",
"Heroin.",
"Heteronormativity.",
"Hillary Clinton's emails.",
"Hip hop jewels.",
"Hipsters.",
"Historically black colleges.",
"Hobos.",
"Holding down a child and farting all over him.",
"Home video of Oprah sobbing into a Lean Cuisine.",
"Homeless people.",
"Homo milk.",
"Hooning.",
"Hope.",
"Hormone injections.",
"Horrifying laser hair removal accidents.",
"Horse meat.",
"Hospice care.",
"Hot cheese.",
"Hot people.",
"Hot Pockets.",
"How amazing it is to be on mushrooms.",
"How awesome it is to be white.",
"How bad my daughter fucked up her dance recital.",
"How far I can get my own penis up my butt.",
"How wet my pussy is.",
"However much weed $20 can buy.",
"However much weed £20 can buy.",
"Huffing spray paint.",
"Huge biceps.",
"Hulk Hogan.",
"Hunting accidents.",
"Hunting accidents.",
"Hurling one's body down a hill in pursuit of a wheel of cheese.",
"Hurricane Katrina.",
"Hurting those closest to me.",
"I'm friends with your dad on Facebook.",
"Ice.",
"Illegal immigrants.",
"Impotence.",
"Improvised explosive devices.",
"Inappropriate yodeling.",
"Incest.",
"Indescribable loneliness.",
"Inserting a jam jar into my anus.",
"Inserting a Mason jar into my anus.",
"Intelligent design.",
"Intimacy problems.",
"Invading Poland.",
"Irritable Bowel Syndrome.",
"Italians.",
"Itchy pussy.",
"J.D. Power and his associates.",
"Jade Goody's cancerous remains.",
"James fucking Cordon.",
"Jedward.",
"Jehovah's Witnesses.",
"Jennifer Lawrence.",
"Jerking off into a pool of children's tears.",
"Jesus.",
"Jew-fros.",
"Jewish fraternities.",
"Jews, gypsies, and homosexuals.",
"Jibber-jabber.",
"Jimmy Savile.",
"Jobs.",
"Joe Biden.",
"John Howard's eyebrows.",
"John Wilkes Booth.",
"Judge Judy.",
"Judging everyone.",
"Just touching David Beckham's hair.",
"Justin Bieber.",
"Justin Trudeau.",
"Juuling.",
"Kamikaze pilots.",
"Kanye West.",
"Kayaking with my sluts.",
"Keanu Reeves.",
"Keeping Christ in Christmas.",
"Keg stands.",
"Kibbles 'n Bits.",
"Kibbles n' Bits.",
"Kids with ass cancer.",
"Kids with bum cancer.",
"Kim Jong-il.",
"Kim Jong-un.",
"Kissing grandma on the forehead and turning off her life support.",
"Kissing nan on the forehead and turning off her life support.",
"Knife crime.",
"Kourtney, Kim, Khloe, Kendall, and Kylie.",
"Lactation.",
"Lads.",
"Lady Gaga.",
"Lance Armstrong's missing testicle.",
"Land mines.",
"Laying an egg.",
"Leaked footage of Kate Middleton's colonoscopy.",
"Leaving an awkward voicemail.",
"Lena Dunham.",
"Leprosy.",
"Letting everyone down.",
"Letting yourself go.",
"Leveling up.",
"Liberals.",
"Licking the Queen.",
"Licking things to claim them as your own.",
"Listening to her problems without trying to solve them.",
"Literally eating shit.",
"Little boy penises.",
"Living in a trash can.",
"Living in Yellowknife.",
"Lockjaw.",
"Loki, the trickster god.",
"Looking in the mirror, applying lipstick, and whispering tonight, you will have sex with Tom Cruise.",
"Loose lips.",
"Lumberjack fantasies.",
"Lunchables.",
"LYNX Body Spray.",
"Mad cow disease.",
"Madeleine McCann.",
"Magnets.",
"Making a friend.",
"Making a pouty face.",
"Making sex at her.",
"Making the penises kiss.",
"Making up for centuries of oppression with one day of apologising.",
"Man meat.",
"Mansplaining.",
"Many bats.",
"Marky Mark and the Funky Bunch.",
"Martha Stewart.",
"Martin Lewis, Money Saving Expert.",
"Massive, widespread drought.",
"Masturbating.",
"Masturbation.",
"Mathletes.",
"Maureen of Blackpool, Reader's Wife of the Year 1988.",
"Me time.",
"Me.",
"MechaHitler.",
"Memes.",
"Men discussing their feelings in an emotionally healthy way.",
"Men.",
"Menstrual rage.",
"Menstruation.",
"Meth.",
"Michael Jackson.",
"Michelle Obama's arms.",
"Mike Pence.",
"Mild autism.",
"Miley Cyrus at 55.",
"Miley Cyrus.",
"Millions of cane toads.",
"Millwall fans.",
"Mining accidents.",
"Mistaking a retarded person for someone who's merely deaf.",
"Mom.",
"Mooing.",
"Moral ambiguity.",
"More elephant cock than I bargained for.",
"Morgan Freeman's voice.",
"Mountain Dew Code Red.",
"Mouth herpes.",
"Mr. Clean, right behind you.",
"Mr. Dressup.",
"Mr. Dressup.",
"Mr. Snuffleupagus.",
"Mr. Squiggle, the Man from the Moon.",
"Muhammad (Peace Be Upon Him).",
"Muhammad (Praise Be Unto Him).",
"Multiple orgasms.",
"Multiple stab wounds.",
"Murder.",
"Mutually assured destruction.",
"Mutually-assured destruction.",
"Muzzy.",
"My abusive boyfriend who really isn't so bad once you get to know him.",
"My balls on your face.",
"My black ass.",
"My Black ass.",
"My boss.",
"My bright pink fuckhole.",
"My cheating prick of a husband.",
"My cheating son-of-a-bitch husband.",
"My collection of high-tech sex toys.",
"My collection of Japanese sex toys.",
"My ex-wife.",
"My fat daughter.",
"My father, who died when I was seven.",
"My first kill.",
"My genitals.",
"My good bra.",
"My humps.",
"My inner demons.",
"My little boner.",
"My machete.",
"My mate Dave.",
"My neck, my back, my pussy, and my crack.",
"My relationship status.",
"My sex life.",
"My soul.",
"My Uber driver, Ajay.",
"My Uber driver, Pavel.",
"My ugly face and bad personality.",
"My vagina.",
"Nachos for the table.",
"Naked News.",
"Natalie Portman.",
"Natural male enhancement.",
"Natural selection.",
"Nazis.",
"NBA superstar LeBron James.",
"Necrophilia.",
"New Age music.",
"Newfies.",
"Newfies.",
"Nickelback.",
"Nicki Minaj.",
"Nicolas Cage.",
"Nip slips.",
"Nippers.",
"Nipple blades.",
"Nocturnal emissions.",
"Not contributing to society in any meaningful way.",
"Not giving a shit about the Third World.",
"Not having sex.",
"Not reciprocating oral sex.",
"Not vaccinating my children because I am stupid.",
"Not wearing pants.",
"Not wearing trousers.",
"Nothing but sand.",
"Nothing.",
"Nunchuck moves.",
"Obesity.",
"Object permanence.",
"Oestrogen.",
"Old-people smell.",
"Ominous background music.",
"One Direction's supple, hairless bodies.",
"One Ring to rule them all.",
"One titty hanging out.",
"One trillion dollars.",
"Only dating Asian women.",
"Oompa-Loompas.",
"Opposable thumbs.",
"Oprah.",
"Our dildo.",
"Our first chimpanzee President.",
"Our first chimpanzee Prime Minister.",
"Overcompensation.",
"Overpowering your father.",
"Oversized lollipops.",
"Owning and operating a Chili's franchise.",
"Pabst Blue Ribbon.",
"Pac-Man uncontrollably guzzling cum.",
"Paedophiles.",
"Panda sex.",
"Paris Hilton.",
"Parting the Red Sea.",
"Party poopers.",
"Passable transvestites.",
"Passing a kidney stone.",
"Passive-aggression.",
"Passive-aggressive Post-it notes.",
"Pauline Hanson.",
"PCP.",
"Peanut Butter Jelly Time.",
"Pedophiles.",
"Peeing a little bit.",
"Penis breath.",
"Penis envy.",
"People who smell their own socks.",
"Perfunctory foreplay.",
"Permanent Orgasm-Face Disorder.",
"Picking up girls at the abortion clinic.",
"Pictures of boobs.",
"Pikies.",
"Pingers.",
"Pissing in my thirsty mouth.",
"Pixelated bukkake.",
"Playing silly buggers.",
"Police brutality.",
"Polish People.",
"Pooping back and forth. Forever.",
"Pooping in a laptop and closing it.",
"Pooping in the soup.",
"Poopy diapers.",
"Poor life choices.",
"Poor people.",
"Poorly-timed Holocaust jokes.",
"Popped collars.",
"Porn stars.",
"Poutine.",
"Poverty.",
"Power.",
"Powerful thighs.",
"Prancing.",
"Praying the gay away.",
"Prescription pain killers.",
"Preteens.",
"Pretending to care.",
"Profound respect and appreciation for indigenous culture.",
"Pronouncing the names of northern Welsh towns.",
"Prosti-tots.",
"Pterodactyl eggs.",
"PTSD.",
"Puberty.",
"Public ridicule.",
"Pulling out.",
"Pumping out a baby every nine months.",
"Punching a congressman in the face.",
"Punching an MP in the face.",
"Puppies!",
"Pussy Galore.",
"Putting things where they go.",
"Queefing.",
"Queen Elizabeth's immaculate anus.",
"Queuing.",
"Racially-biased SAT questions.",
"Racism.",
"Radical Islamic terrorism.",
"Rap music.",
"Raping and pillaging.",
"Raptor attacks.",
"Re-gifting.",
"Rectangles.",
"Rehab.",
"Repression.",
"Republicans.",
"Reverse cowgirl.",
"Rich people.",
"Riding off into the sunset.",
"Ring Pops.",
"Ring Pops.",
"Rip Torn dropkicking anti-Semitic lesbians.",
"Ripping off the Beatles.",
"Ripping open a man’s chest and pulling out his still-beating heart.",
"Rising from the grave.",
"Road head.",
"Rob Ford.",
"Robbing a sperm bank.",
"Robert Downey Jr.",
"Robert Downey, Jr.",
"RoboCop.",
"Rohypnol.",
"Ronald Reagan.",
"Roofies.",
"Rubbing Boris Johnson's belly until he falls asleep.",
"Running naked through a mall, pissing and shitting everywhere.",
"Running out of semen.",
"Rupert Murdoch.",
"Rush Limbaugh's soft, shitty body.",
"Ruth Bader Ginsburg brutally gaveling your penis.",
"Ryan Gosling riding in on a white horse.",
"Ryanair.",
"Same-sex ice dancing.",
"Samuel L. Jackson.",
"Santa Claus.",
"Sarah Palin.",
"Saxophone solos.",
"Saying I love you.",
"Scalping the Milkybar Kid.",
"Scalping.",
"Schmirler the Curler.",
"Science.",
"Scientology.",
"Scottish independence.",
"Scousers.",
"Screaming like a maniac.",
"Scrotum tickling.",
"Scrubbing under the folds.",
"Sean Connery.",
"Sean Penn.",
"Seduction.",
"Seeing Grandma naked.",
"Seeing Granny naked.",
"Seeing my father cry.",
"Seeing what happens when you lock people in a room with hungry seagulls.",
"Seething with quiet resentment.",
"Self-flagellation.",
"Self-loathing.",
"Selling crack to children.",
"Selling ice to children.",
"Seppuku.",
"Serfdom.",
"Seven dead and three in critical condition.",
"Sex with animals.",
"Sex with Patrick Stewart.",
"Sexting.",
"Sexual humiliation.",
"Sexual peeing.",
"Sexual tension.",
"Sexy pillow fights.",
"Shaking a baby until it stops crying.",
"Shapeshifters.",
"Shaquille O'Neal's acting career.",
"Sharing needles.",
"Shiny objects.",
"Shipping convicts to Australia.",
"Shitting out a perfect Cumberland sausage.",
"Shitting out a screaming face.",
"Shorties and blunts.",
"Showing up to an orgy for the food.",
"Shutting the fuck up.",
"Shutting up so I can watch the game.",
"Shutting up so I can watch the match.",
"Sideboob.",
"Silence.",
"Sipping artificial popcorn butter.",
"Sitting in a jar of vinegar all night because I am gherkin.",
"Sitting on my face and telling me I'm garbage.",
"Sitting on my face.",
"Sitting on my face.",
"Skeletor.",
"Skippy the Bush Kangaroo.",
"Slapping a biscuit out of an orphan's mouth.",
"Slapping a racist old lady.",
"Slapping Nigel Farage over and over.",
"Slapping your knees to signal your imminent departure.",
"Slaughtering innocent civilians.",
"Slavs.",
"Smallpox blankets.",
"Smegma.",
"Sneezing, farting, and cumming at the same time.",
"Sniffing and kissing my feet.",
"Sniffing glue.",
"Snotsicles.",
"Sobbing into a Hungry-Man Frozen Dinner.",
"Soft, kissy missionary sex.",
"Soiling oneself.",
"Solving problems with violence.",
"Somali pirates.",
"Some bloody peace and quiet.",
"Some douche with an acoustic guitar.",
"Some foundation, mascara, and a touch of blush.",
"Some god damn peace and quiet.",
"Some guy.",
"Some kind of bird man.",
"Some of the best rappers in the game.",
"Some punk kid who stole my turkey sandwich.",
"Some really fucked-up shit.",
"Sorry, this content cannot be viewed in your region.",
"Soup that is too hot.",
"Spaghetti? Again?",
"Spaniards.",
"Spectacular abs.",
"Spending lots of money.",
"Sperm whales.",
"Spirit Airlines.",
"Spontaneous human combustion.",
"Stalin.",
"Staring at a painting and going hmmmmmmm...",
"Stephen Harper.",
"Stephen Harper.",
"Stephen Hawking talking dirty.",
"Steve Bannon.",
"Steve Irwin.",
"Stifling a giggle at the mention of Hutus and Tutsis.",
"Still being a virgin.",
"Stockholm Syndrome.",
"Stormtroopers.",
"Stranger danger.",
"Strong female characters.",
"Stunt doubles.",
"Substitute teachers.",
"Sucking some dicks to not get drafted.",
"Sudden Poop Explosion Disease.",
"Suicidal thoughts.",
"Summoning Harold Holt from the sea in a time of great need.",
"Sunshine and rainbows.",
"Surprise sex!",
"Swag.",
"Sweet, sweet vengeance.",
"Switching to Geico.",
"Swooping.",
"Synergistic management solutions.",
"Syrupy sex with a maple tree.",
"Take-backsies.",
"Taking a man's eyes and balls out and putting his eyes where his balls go and then his balls in the eye holes.",
"Taking a sheep-wife.",
"Taking off your shirt.",
"Tangled Slinkys.",
"Tap dancing like there's no tomorrow.",
"Tasteful sideboob.",
"Teaching a robot to love.",
"Team building exercises.",
"Team-building exercises.",
"Tearing that ass up like wrapping paper on Christmas morning.",
"Teenage pregnancy.",
"Telling a shitty story that goes nowhere.",
"Tentacle porn.",
"Terrorists.",
"Terry Fox's prosthetic leg.",
"Testicular torsion.",
"That ass.",
"That one gay Teletubby.",
"That thing that electrocutes your abs.",
"The American Dream.",
"The Amish.",
"The arrival of the pizza.",
"The art of seduction.",
"The baby that ruined my pussy.",
"The Bachelorette season finale.",
"The bastard seagull who stole my chips.",
"The Big Bang.",
"The big fucking hole in the ozone layer.",
"The Black Death.",
"The Blood of Christ.",
"The bloody Welsh.",
"The BNP.",
"The bombing of Nagasaki.",
"The boners of the elderly.",
"The Boy Scouts of America.",
"The bush.",
"The Care Bear Stare.",
"The Chinese gymnastics team.",
"The chronic.",
"The clitoris.",
"The cool, refreshing taste of Coca-Cola.",
"The cool, refreshing taste of Pepsi.",
"The cool, refreshing taste of Pepsi.",
"The crazy, ball-slapping sex your parents are having right now.",
"The Daily Mail.",
"The Dalai Lama.",
"The Dance of the Sugar Plum Fairy.",
"The deformed.",
"The Devil Himself.",
"The Donald Trump Seal of Approval.",
"The drama club.",
"The economy.",
"The EDL.",
"The end of days.",
"The entire cast of Downton Abbey.",
"The entire Internet.",
"The entire Mormon Tabernacle Choir.",
"The female orgasm.",
"The FLQ.",
"The flute.",
"The folly of man.",
"The forbidden fruit.",
"The Force.",
"The French.",
"The gays.",
"The glass ceiling.",
"The Great Depression.",
"The Great Emu War.",
"The Gulags.",
"The guys from Queer Eye.",
"The guys from Queer Eye.",
"The Hamburglar.",
"The hardworking Mexican.",
"The heart of a child.",
"The Hemsworth brothers.",
"The hiccups.",
"The Hillsborough Disaster.",
"The Holy Bible.",
"The homosexual agenda.",
"The homosexual lifestyle.",
"The Honey Monster.",
"The human body.",
"The Hustle.",
"The illusion of choice in a late-stage capitalist society.",
"The inevitable heat death of the universe.",
"The invisible hand.",
"The Jews.",
"The KKK.",
"The Kool-Aid Man.",
"The land of chocolate.",
"The light of a billion suns.",
"The Little Engine That Could.",
"The magic of live theatre.",
"The Make-A-Wish Foundation.",
"The Make-A-Wish Foundation.",
"The milk man.",
"The milkman.",
"The miracle of childbirth.",
"The morbidly obese.",
"The North.",
"The Official Languages Act. La Loi sur les langues officielles.",
"The only gay person in a hundred kilometers.",
"The only gay person in a hundred miles.",
"The opioid epidemic.",
"The past.",
"The Patriarchy.",
"The penny whistle solo from My Heart Will Go On.",
"The People's Elbow.",
"The petty troubles of the aristrocracy.",
"The petty troubles of the landed gentry.",
"The pirate's life.",
"The placenta.",
"The plot of a Michael Bay movie.",
"The Pope.",
"The profoundly handicapped.",
"The prostate.",
"The prunes I've been saving for you in my armpits.",
"The Rapture.",
"The Red Hot Chili Peppers.",
"The Red Menace.",
"The Rev. Dr. Martin Luther King, Jr.",
"The rhythms of Africa.",
"The Royal Canadian Mounted Police.",
"The Russians.",
"The Scouts.",
"The screams...the terrible screams.",
"The Smell of a Primark.",
"The Smell of Primark.",
"The South.",
"The Stig.",
"The Strictly Come Dancing final.",
"The Strictly Come Dancing season finale.",
"The sudden appearance of the Go Compare man.",
"The Superdome.",
"The taint; the grundle; the fleshy fun-bridge.",
"The Tempur-Pedic Swedish Sleep System.",
"The terrorists.",
"The Thong Song.",
"The Three-Fifths compromise.",
"The Three-Fifths Compromise.",
"The token minority.",
"The total collapse of the global financial system.",
"The Trail of Tears.",
"The true meaning of Christmas.",
"The Übermensch.",
"The Underground Railroad.",
"The unstoppable tide of Islam.",
"The violation of our most basic human rights.",
"The Virginia Tech Massacre.",
"The way James Bond treats women.",
"The Welsh.",
"The White Australia Policy.",
"The wifi password.",
"The wonders of the Orient.",
"The World of Warcraft.",
"The wrath of Vladimir Putin.",
"Therapy.",
"Theresa May.",
"These hoes.",
"Third base.",
"This answer is postmodern.",
"This month's mass shooting.",
"This year's mass shooting.",
"Those times when you get sand in your vagina.",
"Three dicks at the same time.",
"Throwing a virgin into a volcano.",
"Throwing grapes at a man until he loses touch with reality.",
"Tickle Me Elmo.",
"Tickle Me Elmo.",
"Tickling Sean Hannity, even after he tells you to stop.",
"Tiger Woods.",
"Tiny nipples.",
"Tom Cruise.",
"Tongue.",
"Toni Morrison's vagina.",
"Tony Abbott in budgie smugglers.",
"Too much hair gel.",
"Tories.",
"Total control of the media.",
"Touching a pug right on his penis.",
"Trench foot.",
"Tripping balls.",
"Tweeting.",
"Twenty tonnes of bat shit.",
"Twinkies.",
"Two midgets shitting into a bucket.",
"Unfathomable stupidity.",
"Uppercuts.",
"Used knickers.",
"Used panties.",
"Vegemite.",
"Vehicular homicide.",
"Vehicular manslaughter.",
"Viagra.",
"Vigilante justice.",
"Vigorous jazz hands.",
"Vikings.",
"Vladimir Putin.",
"Vomiting into a kangaroo's pouch.",
"Vomiting mid-blowjob.",
"Vomiting seafood and bleeding anally.",
"Waking up half-naked in a Denny's parking lot.",
"Waking up half-naked in a Little Chef car park.",
"Waking up half-naked in a Macca's car park.",
"Waking up half-naked in a Wetherspoons car park.",
"Waking up in Idris Elba's arms.",
"Walking in on Dad peeing into Mom's mouth.",
"Wanking into a pool of children's tears.",
"Waterboarding.",
"Weapons-grade plutonium.",
"Wearing an octopus for a hat.",
"Wearing underwear inside out to avoid doing laundry.",
"Wearing underwear inside-out to avoid doing laundry.",
"Wet dreams.",
"What that mouth do.",
"What's left of the Great Barrier Reef.",
"When you fart and a little bit comes out.",
"Whining like a little bitch.",
"Whipping it out.",
"Whiskas Catmilk.",
"White people.",
"White power.",
"White power.",
"White privilege.",
"White-man scalps.",
"Whoever the Prime Minister is these days.",
"Wifely duties.",
"Will Smith.",
"William Shatner.",
"Winking at old people.",
"Wiping her bum.",
"Wiping her butt.",
"Wizard music.",
"Women in yoghurt adverts.",
"Women in yoghurt commercials.",
"Women in yogurt commercials.",
"Women of color.",
"Women of colour.",
"Women voting.",
"Women's suffrage.",
"Women's undies.",
"Wondering if it's possible to get some of that salsa to go.",
"Words.",
"World peace.",
"Worshipping that pussy.",
"Xenophobia.",
"Yeast.",
"YOU MUST CONSTRUCT ADDITIONAL PYLONS.",
"Your mom.",
"Your mum.",
"Your weird brother.",

"70s bush.",
"9 11 but with monkeys.",
"A Big Mouth Billy Bass that just screams.",
"A bird funeral.",
"A bit of a tarantula problem.",
"A chode.",
"A dangerous new type of onion.",
"A dentist named Funboy.",
"A fuck swing.",
"A geisha who is clearly Ronald McDonald.",
"A huge swan that wants to fuck.",
"A little boy in a sailor suit.",
"A little heroin here and there.",
"A meeting that could've been an email.",
"A missed period.",
"A mule that can say Walmart.",
"A national conversation about race.",
"A penis with its own smaller penis.",
"A perfectly ordinary object, like a pen.",
"a red wheel barrow",
"glazed with rain water",
"beside the white chickens",
"A restraining order.",
"A sexually charged game of chess.",
"A sexy little bald guy.",
"A shirtless Uber driver.",
"A skinny, submissive husband.",
"A viable third party candidate.",
"A VR horse-fucking simulator.",
"A ziploc bag of soup.",
"Affirmative action.",
"Aging terribly.",
"All y'all motherfuckers.",
"An $80,000 bill from the hospital.",
"An arranged marriage.",
"An avalanche of ground beef.",
"Anxiety-baking a hundred muffins.",
"Any kind of sex with anybody.",
"Apatheid.",
"Arguing.",
"Asking for money.",
"Asking the dentist for more teeth.",
"Asking to see the manager.",
"Bad music or stupid people.",
"Bad stand-up comedy.",
"Bangin' bimbos and boppin' bongos!",
"Being 1/16th Native American.",
"Being a bad, racist policeman.",
"Being a pervert.",
"Being acquired by Amazon.",
"Being mindful of ccylists.",
"Being nervous around white people.",
"Being the product of incest.",
"Big snake!",
"Bitcoin.",
"Black Republicans.",
"Bleeding through another jumbo tampon.",
"Brian, the man who works at the Guitar Center.",
"Building systems and coordinating processes.",
"Buildings.",
"Bush's Baked Beans.",
"Buying things to fill the emptiness inside.",
"Cashews.",
"Chinese pResident Xi Jinping.",
"Choking to death on a Werther's Original.",
"Couples therapy.",
"Crandolph, my sweet egg boy.",
"Crankin' my hog in the middle of a Zoom call.",
"Crying Yes, Father! while getting spanked.",
"Deteriorating mental health.",
"Diabetes.",
"DiGiorno.",
"Dissolving my husband's body in acid.",
"Divorced billionaire energy.",
"Doing nothing about climate change.",
"Domestic terrorism.",
"Drag queens.",
"Eating a huge bowl of pasta like a fat piece of shit.",
"Edging.",
"Escaping from North Korea.",
"Expanding British commercial interests in Nigeria.",
"Faking a pregnancy.",
"Falling down the stairs.",
"Falling in love with a turkey.",
"Farting out a little boy's birthday candles.",
"Feeding a three-course Italian dinner to a mailbox.",
"Figuring out how to stop Black people from voting.",
"Fleeing the country.",
"Fucking a pregnant clown.",
"Fucking me.",
"Gathering all the world leaders in one room and showing them my pussy.",
"Gay.",
"Getting bukkaked by twenty Spider-Men.",
"Getting crunched into a ball and slammed through a hoop by LeBron James.",
"Getting dick tonight.",
"Getting drafted and having to be in the army.",
"Getting involved in local politics.",
"Getting spayed.",
"Getting swole.",
"Getting tazed in the balls.",
"Getting waterboarded.",
"Getting white-girl wasted.",
"Giving $5 to Black Lives Matter.",
"Glittering my pubes.",
"Going to a Maroon 5 concert in 2007.",
"Golf.",
"Googling lenny kravitz penis.",
"Grandpa jerking it to Betty Boop.",
"Hands.",
"Having a bad life.",
"Having a really cool nickname, like The Sledgehammer.",
"Having a thousand best friends but they're all pigeons.",
"Having an affair.",
"Having divorced parents.",
"Having six weeks left to live.",
"Hentai.",
"Holding a lantern and saying, Who goes there ?",
"Hootin' and hollerin'.",
"Hooves.",
"Horny.",
"How sad it is that Tom Hanks died of COVID.",
"HPV.",
"Huge American children.",
"I am a goat.",
"idk lol",
"Impostor Syndrome.",
"Inhaling a bird mid-yawn.",
"Janky tits that go clankety-clank.",
"Killing God.",
"Knowing the legal age of consent in all 50 states.",
"Laying a Nintendo at the Tomb of the Unknown Soldier.",
"Leonardo DiCaprio having sex with a jar of mango salsa.",
"Literally fucking the police.",
"Livin' la vida loca.",
"Looking at an infographic and saying wow.",
"Losing custody of the kids.",
"Lowering your standards.",
"Lying face-down in wet cement.",
"Making friends as an adult.",
"Making love atop a windswept mesa as the sun blooms on the horizon.",
"Mitch McConnell.",
"More cowbell.",
"Muh truck.",
"My beautiful granddaughter.",
"My landlord.",
"My man, Horace.",
"Namazu, the giant mythical catfish.",
"Nelson Mandela.",
"Neoliberalism.",
"Norwegian mansluts.",
"OnlyFans.com",
"Organized religion.",
"Passing as white.",
"Peeeeeeee!",
"Permanent brain damage from football.",
"Personal growth.",
"Playing the cello like a gorilla having a stroke.",
"Profiting off the exploited labot of the working class.",
"Promoting democracy in the Middle East.",
"Pulling Grandpa's pants down and smacking his old butt.",
"Punching my dad in the face and stealing his car.",
"Putting a bookmark in your pussy to remind where I left off.",
"Putting the Pop-Tarts in the toaster like a self-respecting person.",
"Quality custom upholstery at unbeatable prices.",
"Quitting.",
"Receiving anal pleasure at Burger King.",
"Referring to a mozzarella stick as a bad boy.",
"Refusing to pay federal or state income taxes.",
"Remembering that Donald Trump was the president.",
"Renouncing Christ.",
"Resonating with the Earth's vibrational energy.",
"Returning all money to its rightful owner, Jeff Bezos.",
"Ribs, the food that's got BONES!",
"Rudy Giuliani.",
"Running a marathon because oooooh look at me!",
"Running my tongue all the way from your forehead to your asshole in one long, creepy stroke.",
"RuPaul.",
"Russian propaganda.",
"Saudi Crown Prince Mohammed bin Salman.",
"Saying fuck it and stepping off a cliff.",
"Saying grace before sex.",
"Sexy stinky werewolf boys.",
"Shattered hopes and dreams.",
"Shittin' me britches.",
"Shitting into a Coinstar machine.",
"Shitting out part of a turd, sucking it back in, and shitting it back out over and over so you're basically fucking yourself with your own shit.",
"Showing deference to the King of Thailand.",
"Slappin' dat pussaaayyyyyy!",
"Slut-shaming.",
"Smearing feces on the walls.",
"Smokin' a joint, eatin' some cat food, and watchin' Law & Order SVU.",
"Socialism.",
"Some piece-of-shit birdhouse my kid made.",
"Some sort of alien lobster handjob contest.",
"Spending way too long picking a restaurant.",
"Sponsored content.",
"Squeezing an egg out of my bird pussy.",
"Stabbing a motherfucker in the face.",
"Staging a coup.",
"Stompin' around, humpin' and dumpin'.",
"Sturdy Bulgarian women.",
"Sucking period clumps through a boba straw.",
"Swallowing an unopend can of beer.",
"Taking a nap, and then taking another nap.",
"Taking forever to cum.",
"Tentacles.",
"Testing a picture of my asshole to Ted Cruz.",
"The American healthcare system.",
"The Annual San Diego Conference of Blowjobs.",
"The breakup of Yugoslavia.",
"The duck who eats bread out of my ass.",
"The Electoral College.",
"The Great British Baking Show.",
"The guillotine.",
"The horrible things that giving birth dos to your vagina.",
"The joys of motherhood.",
"The kind of epic diarrhea that the ancient bards wrote songs about.",
"The lagacy of slavery.",
"The long fingernail guy from The Guiness Book of World Records.",
"The official company motto of Starbucks: To inspire and nurture the human spirit -- one person, one cup, and one neighborhood at a time.",
"The pile of triceratops shit from Jurassic Park.",
"The profound humanism of Iranian cinema.",
"The Satisfyer Pro 2 Air Pulse Clitoris Stimulator.",
"The song Uptown Funk coming on.",
"The Swamp of Unlimited Pleasure.",
"The ten largest men from my village.",
"The terrible computer virus that put all this porn on my laptop.",
"The weird noises mom makes when she's swallowing big veggies.",
"The world's ugliest baby.",
"Thong crust.",
"Threatening legal action.",
"Throwing your dad into a dumpster and yelling, Now I am dad!",
"Thunderous lovemaking.",
"Thy anus.",
"Titty.",
"Toddlers.",
"Tongue-fucking them toes.",
"Transphobia.",
"Two dudes just bumpin' their balls together.",
"Unresolved childhood trauma.",
"Watching someone fuck my wife.",
"Watching you poop.",
"Wearing a condom the proper way, around the balls and penis.",
"Wearing a MAGA hat.",
"Wearing the bicycle helmet during sex.",
"What I assume to be semen.",
"What the State of New Jersey describes as public defecation.",
"What?",
"Whatever Master desires.",
"White trash in a kiddie pool.",
"Wife-swapping.",
"Your father's throbbin' rockin' penis."
};

enum BotCommandTypes_t {
    BOT_COMMAND_NONE,
    BOT_COMMAND_TRUTH
};

BotCommandTypes_t currentBotCommand = BOT_COMMAND_NONE;
int currentProcessingNum = 0;
int currentProcessChar = 0;

const int numTruthEntries = sizeof(truthQuestionList) / sizeof(intptr_t);
const int numTruth2Entries = sizeof(truth2QuestionList) / sizeof(intptr_t);
const int numResponseCards = sizeof(responseCard) / sizeof(intptr_t);

const int numCamCards = (sizeof(CamCards_t) / 5) / sizeof(intptr_t);

//bool truthCardsInPlay[numTruthEntries] = { };
//bool responseCardsInPlay[numResponseCards] = { };

std::vector<int> truthCardList;
std::vector<int> truthCard2List;
std::vector<int> responseCardList;
std::vector<int> camCardList;

int currentTruthCard = 0;
int currentTruth2Card = 0;
int currentResponseCard = 0;
int currentCamCard = 0;

void ResetCards(void)
{
    static std::random_device rd;

    //memset(&truthCardsInPlay[0], 0, sizeof(truthCardsInPlay));
    //memset(&responseCardsInPlay[0], 0, sizeof(responseCardsInPlay));

    truthCardList.clear();
    truthCard2List.clear();
    responseCardList.clear();

    for (int i = 0; i < numTruthEntries; i++)
        truthCardList.push_back(i);

    for (int i = 0; i < numTruth2Entries; i++)
        truthCard2List.push_back(i);

    for (int i = 0; i < numResponseCards; i++)
        responseCardList.push_back(i);

    for (int i = 0; i < numCamCards; i++)
        camCardList.push_back(i);

    {        
        std::mt19937 g(rd());
        std::shuffle(truthCardList.begin(), truthCardList.end(), g);
    }

    {
        std::mt19937 g(rd());
        std::shuffle(truthCard2List.begin(), truthCard2List.end(), g);
    }

    {
        std::mt19937 g(rd());
        std::shuffle(responseCardList.begin(), responseCardList.end(), g);
    }

    {
        std::mt19937 g(rd());
        std::shuffle(camCardList.begin(), camCardList.end(), g);
    }

    currentTruthCard = 0;
    currentTruth2Card = 0;
    currentResponseCard = 0;
    currentCamCard = 0;
}

const char* truthMessage = nullptr;

void SetClipboardText(const char *msg)
{
    std::string msg_fixed = "\n";

    msg_fixed += msg;

    if (!OpenClipboard(NULL))
        return;

    EmptyClipboard();

    size_t bufferSize = msg_fixed.size();
    HGLOBAL bufferHandle = GlobalAlloc(GMEM_MOVEABLE, bufferSize * sizeof(wchar_t));
    if (bufferHandle == NULL)
    {
        CloseClipboard();
        return;
    }

    wchar_t* buffer = reinterpret_cast<wchar_t*>(GlobalLock(bufferHandle));
    MultiByteToWideChar(CP_UTF8, 0, msg_fixed.c_str(), -1, buffer, bufferSize);
    GlobalUnlock(bufferHandle);

    SetClipboardData(CF_UNICODETEXT, bufferHandle);
    CloseClipboard();
}

void RunBotCommand(void)
{
    static bool skipFrame = false;

    if (currentBotCommand == BOT_COMMAND_NONE)
        return;

    if (currentBotCommand == BOT_COMMAND_TRUTH && !skipFrame) {
        if (currentProcessChar == 0) {
            SetClipboardText(truthMessage);
        }

        currentProcessChar++;
        if (currentProcessChar == 2)
        {
          //  PostMessage(gameWindow, WM_KEYDOWN, VK_CONTROL, 0);
          //  PostMessage(gameWindow, WM_KEYDOWN, VkKeyScanExA('v', GetKeyboardLayout(0)), 0);
        }
        if (currentProcessChar >= 3)
        {
            //PostMessage(gameWindow, WM_KEYDOWN, VK_RETURN, 0);
            currentBotCommand = BOT_COMMAND_NONE;
            currentProcessChar = 0; // so this doesn't fuck up elsewhere.
            skipFrame = false;
            return;
        }

        skipFrame = !skipFrame;
        
        return;
    }

    skipFrame = !skipFrame;
}

void ProcessBotCommand(const char* command)
{
    if (currentBotCommand != BOT_COMMAND_NONE)
    {
        OutputDebugStringA("WARNING: Bot is current processing work! Command Ignored!");
        return;
    }

    if (strstr(command, "#camcard")) {
        if (currentCamCard >= numCamCards)
            ResetCards();

        currentProcessingNum = camCardList[currentCamCard++];
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;

        static std::string camStringTest;

        camStringTest = "";

        for (int i = 0; i < 5; i++)
        {
            camStringTest += CamCards_t[(currentProcessingNum * 5) + i];
        }

        truthMessage = camStringTest.c_str();
        return;
    }

    if (strstr(command, "#card")) {
        if (currentTruthCard >= numTruthEntries)
            ResetCards();

        currentProcessingNum = truthCardList[currentTruthCard++];
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;
        truthMessage = truthQuestionList[currentProcessingNum];
        return;
    }

    if (strstr(command, "#truth")) {
        if (currentTruth2Card >= numTruth2Entries)
            ResetCards();

        currentProcessingNum = truthCard2List[currentTruth2Card++];
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;
        truthMessage = truth2QuestionList[currentProcessingNum];
        return;
    }

    if (strstr(command, "#rcard")) {
        static std::string responseCards;

        responseCards = "";

        if (currentResponseCard >= numResponseCards - 10)
            ResetCards();

        for (int i = 0; i < 5; i++)
        {
            responseCards += responseCard[responseCardList[currentResponseCard++]];

            if(i < 4)
                responseCards += " | ";
        }

        currentProcessingNum = -1;
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;
        truthMessage = responseCards.c_str();
        return;
    }

    if (strstr(command, "#shuffle")) {
        ResetCards();
        currentProcessingNum = -1;
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;
        truthMessage = "cards shuffled";
        return;
    }
    
    OutputDebugStringA("WARNING: Unknown command!");

    //PostMessage(gameWindow, WM_KEYDOWN, 0x41, 0);
    //
    //OutputDebugStringA(command);


}

void RunMovie(void)
{
    static float time_test = 0;

    if (streamingMovieTexture == nullptr)
        return;

    cinData_t data = testcinematic->ImageForTime(time_test);

    UINT const DataSize = sizeof(FLOAT);
    UINT const RowPitch = DataSize * data.imageWidth;
    UINT const DepthPitch = DataSize * data.imageWidth * data.imageHeight;

    D3D11_BOX Box;
    Box.left = 0;
    Box.right = data.imageWidth;
    Box.top = 0;
    Box.bottom = data.imageHeight;
    Box.front = 0;
    Box.back = 1;

   D3D11_MAPPED_SUBRESOURCE map;
   unityD3D11ImmediateContext->Map(streamingMovieTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);

   // I have to copy the texture upside down; stupid slow copy!
   // memcpy(map.pData, data.image, DepthPitch);
   unsigned char* gpu_data = (unsigned char *)map.pData;
   for (int i = 0; i < data.imageWidth * data.imageHeight; i++)
   {
       int dest = (data.imageWidth * data.imageHeight) - i - 1;
       gpu_data[(dest * 4) + 0] = data.image[(i * 4) + 0];
       gpu_data[(dest * 4) + 1] = data.image[(i * 4) + 1];
       gpu_data[(dest * 4) + 2] = data.image[(i * 4) + 2];
       gpu_data[(dest * 4) + 3] = data.image[(i * 4) + 3];
   }

   unityD3D11ImmediateContext->Unmap(streamingMovieTexture, 0);

       time_test += 16.0f;
}


// ==========================================================================================================
//
// All Code below is setup and hooks for getting custom code loaded into 3dxchat that we need.
//
// ==========================================================================================================

// Hint that the discrete gpu should be enabled on optimus/enduro systems
// NVIDIA docs: http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// AMD forum post: http://devgurus.amd.com/thread/169965
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
};

HRESULT(*PresentActual)(IDXGIFactory* dxgiFactory, UINT SyncInterval, UINT Flags);
HRESULT STDMETHODCALLTYPE PresentNew(IDXGIFactory* dxgiFactory, UINT SyncInterval, UINT Flags) {
    RunMovie();

    RunBotCommand();

    return PresentActual(dxgiFactory, SyncInterval, Flags);
}

HRESULT CreateSwapChainForHwnd;

HRESULT(*CreateSwapChainForHwndActual)(IDXGIFactory* factory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
HRESULT STDMETHODCALLTYPE CreateSwapChainForHwndNew(IDXGIFactory *factory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    HRESULT hr = CreateSwapChainForHwndActual(factory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);

    gameWindow = hWnd;

    // Hook the present function.
    static bool setHook = false;
    if (!setHook)
    {
        intptr_t** vtable = *reinterpret_cast<intptr_t***>(*ppSwapChain);// ->CreateTexture2D;
        intptr_t* function = vtable[(int)IDXGISwapChainVMT::Present];
        MH_CreateHook(function, PresentNew, (LPVOID*)&PresentActual);
        MH_EnableHook(function);
    
        setHook = true;
    }

    return hr;
}

// Tell Unity we are in the right folder with the right executable name.
DWORD (*GetModuleFileNameWActual)(HMODULE hModule, LPWSTR  lpFilename, DWORD   nSize);
DWORD GetModuleFileNameWNew(HMODULE hModule, LPWSTR  lpFilename, DWORD   nSize) {
    // TODO: Move to external file don't hard code this shit!
    wcscpy(lpFilename, L"D:\\Games\\3DXChat2.8\\3DXChat.exe");
    return 1;
}

HRESULT (*CreateDXGIFactory1Actual)(REFIID riid, void** ppFactory);
HRESULT CreateDXGIFactoryNew(REFIID riid, void** ppFactory) {
    HRESULT hr = CreateDXGIFactory1Actual(riid, ppFactory);

    unityDXGIFactory = (IDXGIFactory*)*ppFactory;

    // Hook the CreateSwapChain function.
    static bool setHook = false;
    if (!setHook)
    {
        intptr_t** vtable = *reinterpret_cast<intptr_t***>(unityDXGIFactory);// ->CreateTexture2D;
        intptr_t* function = vtable[15];
        MH_CreateHook(function, CreateSwapChainForHwndNew, (LPVOID*)&CreateSwapChainForHwndActual);
        MH_EnableHook(function);

        setHook = true;
    }

    

    return hr;
}

bool createMovieResourceView = false;

HRESULT  (*D3D11CreateTexture2DActual)(ID3D11Device* unityD3D11Device, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);
HRESULT STDMETHODCALLTYPE D3D11CreateTexture2DNew(ID3D11Device* unityD3D11Device2, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    static bool runMovieTest = false;

    if (!runMovieTest)
    {
        return D3D11CreateTexture2DActual(unityD3D11Device2, pDesc, pInitialData, ppTexture2D);
    }

    // The texture we are looking for is 
    if (pDesc->Width != 256 && pDesc->Height != 256 || pDesc->Format != DXGI_FORMAT_BC3_UNORM_SRGB)
    {
        return D3D11CreateTexture2DActual(unityD3D11Device2, pDesc, pInitialData, ppTexture2D);
    }

    static int stupidFuckingHack = 0;

    if (stupidFuckingHack == 0)
    {
        // Load in the movie.
        Cinematic::InitCinematic();
        testcinematic = Cinematic::Alloc();
        testcinematic->InitFromFile("D:\\Projects\\3dxmod2\\Binary\\testvideo3.mp4", true);

        D3D11_TEXTURE2D_DESC newDesc = { };
        newDesc.Width = testcinematic->CIN_WIDTH;
        newDesc.Height = testcinematic->CIN_HEIGHT;
        newDesc.MipLevels = newDesc.ArraySize = 1;
        newDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        newDesc.SampleDesc.Count = 1;
        newDesc.Usage = D3D11_USAGE_DYNAMIC;
        newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        newDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        newDesc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA data = { };

        unsigned char* tempData = new unsigned char[testcinematic->CIN_WIDTH * testcinematic->CIN_HEIGHT * 4];
        memset(tempData, 255, testcinematic->CIN_WIDTH * testcinematic->CIN_HEIGHT * 4);

        data.pSysMem = tempData;
        data.SysMemPitch = testcinematic->CIN_WIDTH * 4;
        data.SysMemSlicePitch = 0;

        stupidFuckingHack++;

        HRESULT hr = D3D11CreateTexture2DActual(unityD3D11Device2, &newDesc, &data, ppTexture2D);

        streamingMovieTexture = *ppTexture2D;

        createMovieResourceView = true;

        return hr;
    }

    stupidFuckingHack++;

    return D3D11CreateTexture2DActual(unityD3D11Device2, pDesc, pInitialData, ppTexture2D);
}

HRESULT (*CreateShaderResourceViewActual)(ID3D11Device* unityD3D11Device2, ID3D11Resource* pResource,const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,ID3D11ShaderResourceView** ppSRView);
HRESULT CreateShaderResourceViewNew(ID3D11Device* unityD3D11Device2, ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView) {

    if (createMovieResourceView)
    {
        createMovieResourceView = false;
        D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
        shaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
        shaderResourceViewDesc.Texture2D.MipLevels = 1;

        return CreateShaderResourceViewActual(unityD3D11Device, pResource, &shaderResourceViewDesc, ppSRView);
    }

    return CreateShaderResourceViewActual(unityD3D11Device, pResource, pDesc, ppSRView);
}

// We detour D3D11CreateDevice which Unity calls to create the D3D11 rendering device. For us we just need to grab the resulting device handle.
// We also need to hook CreateTexture2D for the movie updates.
// D3D11CreateDeviceActual is the real pointer to the function and D3D11CreateDeviceNew is our detour function.
HRESULT(*D3D11CreateDeviceActual)(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT  SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);
HRESULT D3D11CreateDeviceNew(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT  SDKVersion, ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) {
    HRESULT hr = D3D11CreateDeviceActual(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,  SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

    // Grab the D3D11 device handle.
    unityD3D11Device = *ppDevice;
    unityD3D11ImmediateContext = *ppImmediateContext;

    // Hook our Texture2D create function. D3D11CreateDevice can get called again if the device gets lost it makes sense to guard this.
    static bool setHook = false;
    if(!setHook)
    {
        {
            intptr_t** vtable = *reinterpret_cast<intptr_t***>(unityD3D11Device);// ->CreateTexture2D;
            intptr_t* function = vtable[5];
            MH_CreateHook(function, D3D11CreateTexture2DNew, (LPVOID*)&D3D11CreateTexture2DActual);
            MH_EnableHook(function);
        }

        {
            intptr_t** vtable = *reinterpret_cast<intptr_t***>(unityD3D11Device);// ->CreateTexture2D;
            intptr_t* function = vtable[7];
            MH_CreateHook(function, CreateShaderResourceViewNew, (LPVOID*)&CreateShaderResourceViewActual);
            MH_EnableHook(function);
        }
       

       setHook = true;
    }

    return hr;
}

void DetourD3D11CreateDevice(void)
{
    HMODULE d3d11dll = LoadLibraryA("d3d11.dll");
    void* function = (LPVOID)GetProcAddress(d3d11dll, "D3D11CreateDevice");
    MH_CreateHook(function, D3D11CreateDeviceNew, (LPVOID*)&D3D11CreateDeviceActual);
    MH_EnableHook(function);
}

void DetourDXGICreateFactory(void)
{
    HMODULE dxgidll = LoadLibraryA("dxgi.dll");
    void* function = (LPVOID)GetProcAddress(dxgidll, "CreateDXGIFactory1");
    MH_CreateHook(function, CreateDXGIFactoryNew, (LPVOID*)&CreateDXGIFactory1Actual);
    MH_EnableHook(function);
}


HANDLE (*CreateFileWActual)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE CreateFileWNew(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    // Stupid asset bundles don't load overrides.
    //if (StrStrW(lpFileName, TEXT("man@wall_1_single_m"))) {
    //    HANDLE hr = CreateFileWActual(L"D:\\Projects\\3dxmod2\\Binary\\AssetBundles\\animation\\ingame\\man@dance_1", dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    //
    //    DWORD error = GetLastError();
    //
    //    return hr;
    //}

    return CreateFileWActual(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

SOCKET chatSocket = 0;


int (*connectactual)(SOCKET s,const sockaddr* name,int namelen);
int connectnew(SOCKET s, const sockaddr* name, int namelen) {
    sockaddr_in *_in = (sockaddr_in *)name;

    if (_in->sin_port == htons(8125)) {
        chatSocket = s;
        return connectactual(s, name, namelen);
    }

    return connectactual(s, name, namelen);
}

int (*recvfromactual)(SOCKET s,char* buf, int len, int flags, sockaddr* from, int* fromlen);
int recvfromnew(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen) {
    if (s == chatSocket) {
        int bufferSize = recvfromactual(s, buf, len, flags, from, fromlen);

        for (int d = bufferSize - 10; d < bufferSize; d++)
        {
            if (buf[d] == '#')
            {
                ProcessBotCommand(&buf[d]);
                return bufferSize;
            }
        }
        

        return bufferSize;
    }

    return recvfromactual(s, buf, len, flags, from, fromlen);
}

void HookConnectForGameBot(void) 
{
    {
        HMODULE dxgidll = LoadLibraryA("Ws2_32.dll");
        void* function = (LPVOID)GetProcAddress(dxgidll, "connect");
        MH_CreateHook(function, connectnew, (LPVOID*)&connectactual);
        MH_EnableHook(function);
    }
    
    {
        HMODULE dxgidll = LoadLibraryA("Ws2_32.dll");
        void* function = (LPVOID)GetProcAddress(dxgidll, "recvfrom");
        MH_CreateHook(function, recvfromnew, (LPVOID*)&recvfromactual);
        MH_EnableHook(function);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    ResetCards();

    MH_Initialize();

    HookConnectForGameBot();

    {
        void* function = (LPVOID)&CreateFileW;
        MH_CreateHook(function, CreateFileWNew, (LPVOID*)&CreateFileWActual);
        MH_EnableHook(function);
    }

    {
        void* function = (LPVOID)GetModuleFileNameW;
        MH_CreateHook(function, GetModuleFileNameWNew, (LPVOID*)&GetModuleFileNameWActual);
        MH_EnableHook(function);
    }

    DetourD3D11CreateDevice();

    DetourDXGICreateFactory();

    // Make the call into UnityMain will launch 3dx chat.
    return UnityMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}
