#define IMGUI_DEFINE_MATH_OPERATORS
#include "overlay.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <dwmapi.h>
#include <vector>  
#include <d3d11.h>
#include <d3dx11.h>  
#include <thread>    
#include <atomic>  

ID3D11ShaderResourceView* banner_texture = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

extern const unsigned char banner[];

std::atomic<bool> g_threadRunning{ false };
std::thread g_topmostThread;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool g_DraggingImGuiWindow = false;
ImVec2 g_LastMousePos = ImVec2(0, 0);
HWND hwnd;

bool g_mouseOnMenu = false;
bool g_wasClickable = false;

void BringToForeground(HWND hWnd)
{
    if (!SetForegroundWindow(hWnd))
    {
        DWORD foregroundThreadID = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
        DWORD currentThreadID = GetCurrentThreadId();

        if (foregroundThreadID != currentThreadID)
        {
            AttachThreadInput(foregroundThreadID, currentThreadID, TRUE);
            SetForegroundWindow(hWnd);
            AttachThreadInput(foregroundThreadID, currentThreadID, FALSE);
        }
    }

    SetActiveWindow(hWnd);
}

void UpdateWindowClickability(HWND hWnd, bool makeClickable)
{
    LONG_PTR exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

    if (makeClickable && (exStyle & WS_EX_TRANSPARENT))
    {
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
    }
    else if (!makeClickable && !(exStyle & WS_EX_TRANSPARENT))
    {
        SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
    }
}

bool IsMouseOverMenu()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    ScreenToClient(hwnd, &cursorPos);

    ImVec2 mousePos = ImVec2((float)cursorPos.x, (float)cursorPos.y);

    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return false;

    for (int i = 0; i < ctx->Windows.Size; i++)
    {
        ImGuiWindow* window = ctx->Windows[i];
        if (window->WasActive && window->Rect().Contains(mousePos))
        {
            return true;
        }
    }

    return false;
}

void UpdateWindowState()
{
    g_mouseOnMenu = IsMouseOverMenu();

    bool shouldBeClickable = g_mouseOnMenu;

    if (shouldBeClickable != g_wasClickable) {
        UpdateWindowClickability(hwnd, shouldBeClickable);
        g_wasClickable = shouldBeClickable;
    }
}

bool IsWindowTopmost(HWND hWnd)
{
    HWND topmostWindow = GetTopWindow(NULL);

    while (topmostWindow != NULL)
    {
        if (IsWindowVisible(topmostWindow))
        {
            return (topmostWindow == hWnd);
        }
        topmostWindow = GetNextWindow(topmostWindow, GW_HWNDNEXT);
    }

    return false;
}

void TopmostMonitorThread()
{
    while (g_threadRunning)
    {
        if (hwnd != NULL && IsWindow(hwnd))
        {
            if (!IsWindowTopmost(hwnd))
            {

                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

                Sleep(100);

                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }

        Sleep(500);
    }
}

void StartTopmostMonitor()
{
    if (!g_threadRunning)
    {
        g_threadRunning = true;
        g_topmostThread = std::thread(TopmostMonitorThread);
    }
}

void StopTopmostMonitor()
{
    if (g_threadRunning)
    {
        g_threadRunning = false;
        if (g_topmostThread.joinable())
        {
            g_topmostThread.join();
        }
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

ID3D11ShaderResourceView* CreateTextureFromRGBA(ID3D11Device* device, const unsigned char* rgbaData, int width, int height)
{
    ID3D11Texture2D* pTexture = nullptr;
    ID3D11ShaderResourceView* textureView = nullptr;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));
    initData.pSysMem = rgbaData;
    initData.SysMemPitch = width * 4;
    initData.SysMemSlicePitch = width * height * 4;

    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, &pTexture)))
    {

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        if (SUCCEEDED(device->CreateShaderResourceView(pTexture, &srvDesc, &textureView)))
        {
            pTexture->Release();
            return textureView;
        }
        pTexture->Release();
    }

    return nullptr;
}

void CreateConsoleWindow(HWND mainWindow) {
    AllocConsole();

    FILE* fpOut;
    FILE* fpIn;
    FILE* fpErr;

    freopen_s(&fpOut, "CONOUT$", "w", stdout);
    freopen_s(&fpErr, "CONOUT$", "w", stderr);
    freopen_s(&fpIn, "CONIN$", "r", stdin);


    SetConsoleTitle(L"Debugging Console");

    SetForegroundWindow(mainWindow);
}

int load()
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Loader", nullptr };
    ::RegisterClassExW(&wc);
    hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName,
        L"Loader",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, wc.hInstance, nullptr);

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margin = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margin);

    //CreateConsoleWindow(hwnd);


    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = config.OversampleV = 1;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, &config);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    StartTopmostMonitor();

    D3DX11_IMAGE_LOAD_INFO info;
    ID3DX11ThreadPump* pump{ nullptr };

    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.00f, 0.00f);
    int currentAffinity = 0;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        overlay::draw_gui();

        UpdateWindowState();
        ImGui::Render();

        ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 0.f);
        const float clear_color_with_alpha[4] = {
            clear_color.x * clear_color.w,
            clear_color.y * clear_color.w,
            clear_color.z * clear_color.w,
            clear_color.w
        };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    StopTopmostMonitor();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    if ((msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN))
    {
        if (g_mouseOnMenu) {
            BringToForeground(hWnd);
        }
    }

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
