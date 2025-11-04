#define IMGUI_DEFINE_MATH_OPERATORS
#include "overlay.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <dwmapi.h>
#include <vector>
#include <d3d11.h>
#include <thread>
#include <atomic>
#include <chrono>

ID3D11ShaderResourceView* banner_texture = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
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

// Forward declarations
void StartTopmostMonitor();
void StopTopmostMonitor();
void UpdateWindowState();
void BringToForeground(HWND hWnd);
void RenderFrame();

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

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    menu::InitStyle();

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    StartTopmostMonitor();
    
    static bool menuKeyWasPressed = false;
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

        // Handle window resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Draw overlay GUI inside ImGui frame
        if (globals->menuOpen) {
            menu::Draw();
        }
        overlay::draw_gui();

        // Menu key handling
        if (!g_capturingMenuKey.load()) {
            bool keyDown = (config->menu.menuKey != 0) && (GetAsyncKeyState(config->menu.menuKey) & 0x8000);
            if (keyDown && !menuKeyWasPressed) {
                globals->menuOpen = !globals->menuOpen;
                UpdateWindowState(); // Update window transparency state
                menuKeyWasPressed = true;
            } else if (!keyDown) {
                menuKeyWasPressed = false;
            }
        }

        // Render
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    // Use BGRA format
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
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

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
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

// Topmost monitor thread control (re-using g_threadRunning and g_topmostThread)
static void StartTopmostMonitor() {
    if (g_threadRunning.load()) return;
    g_threadRunning.store(true);
    g_topmostThread = std::thread([]() {
        while (g_threadRunning.load()) {
            if (hwnd) {
                // keep window topmost without changing size/position
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
}

static void StopTopmostMonitor() {
    if (!g_threadRunning.load()) return;
    g_threadRunning.store(false);
    if (g_topmostThread.joinable()) g_topmostThread.join();
}

// Update window state each frame: enable/disable click-through based on menu visibility
static void UpdateWindowState() {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    bool isTransparent = (ex & WS_EX_TRANSPARENT) != 0;
    if (globals->menuOpen && isTransparent) {
        // remove transparent so we can interact with menu
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
        // ensure window is topmost and visible
        SetWindowPos(hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else if (!globals->menuOpen && !isTransparent) {
        // make overlay click-through
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    }
}

static void BringToForeground(HWND hWnd) {
    if (!hWnd) return;
    // try to bring the app window to foreground
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
}
