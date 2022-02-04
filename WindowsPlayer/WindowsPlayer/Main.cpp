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
    - 


When D3D11CreateDevice gets called by the engine, it now calls our function instead and we grab some variables and hook CreateTexture2D 
and call the trampolined D3D11CreateDevice call.

Now that we have CreateTexture2D hooked we now have the ability to monitor textures the engine loads until we find the texture we want.


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

ID3D11Device* unityD3D11Device = nullptr;
IDXGIFactory* unityDXGIFactory = nullptr;
ID3D11DeviceContext* unityD3D11ImmediateContext = nullptr;

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
    return PresentActual(dxgiFactory, SyncInterval, Flags);
}

HRESULT CreateSwapChainForHwnd;

HRESULT(*CreateSwapChainForHwndActual)(IDXGIFactory* factory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
HRESULT STDMETHODCALLTYPE CreateSwapChainForHwndNew(IDXGIFactory *factory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    HRESULT hr = CreateSwapChainForHwndActual(factory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);

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

HRESULT  (*D3D11CreateTexture2DActual)(ID3D11Device* unityD3D11Device, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D);
HRESULT STDMETHODCALLTYPE D3D11CreateTexture2DNew(ID3D11Device* unityD3D11Device2, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    // The texture we are looking for is 
    if (pDesc->Width != 1024 && pDesc->Height != 1024 && pDesc->Format != DXGI_FORMAT_BC1_UNORM_SRGB)
    {
        return D3D11CreateTexture2DActual(unityD3D11Device2, pDesc, pInitialData, ppTexture2D);
    }

    return D3D11CreateTexture2DActual(unityD3D11Device2, pDesc, pInitialData, ppTexture2D);
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
       intptr_t** vtable = *reinterpret_cast<intptr_t***>(unityD3D11Device);// ->CreateTexture2D;
       intptr_t* function = vtable[5];
       MH_CreateHook(function, D3D11CreateTexture2DNew, (LPVOID*)&D3D11CreateTexture2DActual);
       MH_EnableHook(function);

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


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    MH_Initialize();

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
