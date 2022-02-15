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

ID3D11Device* unityD3D11Device = nullptr;
IDXGIFactory* unityDXGIFactory = nullptr;
ID3D11DeviceContext* unityD3D11ImmediateContext = nullptr;

Cinematic* testcinematic = nullptr;

ID3D11Texture2D* streamingMovieTexture = nullptr;

using std::cout; using std::endl;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

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
        testcinematic->InitFromFile("D:\\Projects\\3dxmod2\\Binary\\testvideo.mp4", true);

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
    HRESULT hr = D3D11CreateDeviceActual(pAdapter, DriverType, Software, Flags | D3D11_CREATE_DEVICE_DEBUG, pFeatureLevels, FeatureLevels,  SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

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
