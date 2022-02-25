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

//
// returns random integer from 1 to lim (Bill's generator)
//
int rand3(int lim)
{
    static long a = time(0);

    a = (((a * 214013L + 2531011L) >> 16) & 32767);

    return ((a % lim) + 1);
}

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
    "In Australia, ______ is twice as bigand twice as deadly.",
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
    "The secret to a lasting marriage is communication, communication,and ______.",
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
    "Your persistence is admirable, my dear Prince.But you cannot win my heart with ______ alone"
};

enum BotCommandTypes_t {
    BOT_COMMAND_NONE,
    BOT_COMMAND_TRUTH
};

BotCommandTypes_t currentBotCommand = BOT_COMMAND_NONE;
int currentProcessingNum = 0;
int currentProcessChar = 0;

const int numTruthEntries = sizeof(truthQuestionList) / sizeof(intptr_t);

void RunBotCommand(void)
{
    if (currentBotCommand == BOT_COMMAND_NONE)
        return;

    if (currentBotCommand == BOT_COMMAND_TRUTH) {
        int maxLen = strlen(truthQuestionList[currentProcessingNum]) + 1;
        //int vk_scan_code = tolower(truthQuestionList[currentProcessingNum][currentProcessChar]) - 'a';
        //
        //if (truthQuestionList[currentProcessingNum][currentProcessChar] == ' ')
        //{
        //    PostMessage(gameWindow, WM_KEYDOWN, VK_SPACE, 0);
        //}
        //else if (truthQuestionList[currentProcessingNum][currentProcessChar] == ',')
        //{
        //    PostMessage(gameWindow, WM_KEYDOWN, VK_COMMA, 0);
        //}
        //else
        //{
        //    PostMessage(gameWindow, WM_KEYDOWN, 0x41 + vk_scan_code, 0);
        //}

        PostMessage(gameWindow, WM_KEYDOWN, VkKeyScanExA(truthQuestionList[currentProcessingNum][currentProcessChar], GetKeyboardLayout(0)), 0);        

        currentProcessChar++;
        if (currentProcessChar >= maxLen)
        {
            PostMessage(gameWindow, WM_KEYDOWN, VK_RETURN, 0);
            currentBotCommand = BOT_COMMAND_NONE;
            currentProcessChar = 0; // so this doesn't fuck up elsewhere.
        }
        return;
    }
}

void ProcessBotCommand(const char* command)
{
    if (currentBotCommand != BOT_COMMAND_NONE)
    {
        OutputDebugStringA("WARNING: Bot is current processing work! Command Ignored!");
        return;
    }

    if (strstr(command, "#card")) {
        currentProcessingNum = rand3(numTruthEntries);
        currentBotCommand = BOT_COMMAND_TRUTH;
        currentProcessChar = 0;
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
