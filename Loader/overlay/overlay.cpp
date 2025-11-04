#pragma once
#include <iostream>
#include <vector>
#include <string>
#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <chrono>

#include <dwmapi.h>

#include "menu/menu.h"

#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

// Fonts (wie gehabt)
inline ImFont* g_titleFont = nullptr;
inline ImFont* g_tabFont = nullptr;
inline ImFont* g_defaultFont = nullptr;
inline ImFont* g_drawFont = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace window
{
    inline HWND hwnd;
    inline HINSTANCE instance;
    inline uint32_t width, height;

    namespace directx
    {
        inline ID3D11Device* device = nullptr;
        inline ID3D11DeviceContext* context = nullptr;
        inline IDXGISwapChain* swap_chain = nullptr;
        inline ID3D11RenderTargetView* render_target_view = nullptr;
    }

    LRESULT WINAPI WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
    void cleanup();
    bool create_window();
    bool create_device();
    void new_frame();
    void draw();
}

namespace overlay
{
    inline HWND target;
    inline uint32_t width, height;

    bool initialize(HWND window);
    bool scale();
    void click_through(bool click);
    void draw_gui();
    void loop();

    // ----- Zustand (entspricht den C#-Eigenschaften) -----
    inline std::vector<std::string> ActiveFeatures;
    inline bool IsFeatureListVisible = true;
    inline int FeatureTextSize = 14;
    inline ImU32 FeatureTextColor = IM_COL32(255,255,255,255);

    inline bool IsWatermarkVisible = false;
    inline std::string WatermarkText = "OniV2";
    inline int WatermarkSize = 20;
    inline ImU32 WatermarkColor = IM_COL32(255,255,255,255);

    // Crosshair/display settings available to other units
    inline int CrosshairSize = 18;        // Radius / halbe Linienstrecke
    inline int LineThickness = 3;
    inline ImU32 CrosshairColor = IM_COL32(255,255,255,255);
    inline std::string CrosshairShape = "Dot"; // "Dot","Plus","Cross","Triangle","Pinwheel","Windmill1954"

    inline bool IsRotating = false;
    inline float RotationAngleDeg = 0.0f; // external code kann hochz?hlen
    inline bool RainbowCrosshair = false;

    // Constant for PI
    inline constexpr double PI_DOUBLE = 3.14159265358979323846;
}

// ----- Hilfsfunktionen -----
namespace {
    inline ImVec2 RotatePoint(const ImVec2& p, const ImVec2& center, float angleRad)
    {
        if (angleRad == 0.0f) return p;
        float dx = p.x - center.x;
        float dy = p.y - center.y;
        float ca = cosf(angleRad);
        float sa = sinf(angleRad);
        return ImVec2(center.x + dx * ca - dy * sa, center.y + dx * sa + dy * ca);
    }

    inline ImU32 HSVtoU32(float h, float s, float v, float a = 1.0f)
    {
        int i = (int)floorf(h * 6.0f);
        float f = h * 6.0f - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);
        float r = 1, g = 1, b = 1;
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        }
        return IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),(int)(a*255));
    }

    inline ImU32 CrosshairActualColor()
    {
        if (!overlay::RainbowCrosshair) return overlay::CrosshairColor;
        using namespace std::chrono;
        auto now = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
        float h = fmodf(static_cast<float>((ms / 1000.0) * 0.12), 1.0f);
        return HSVtoU32(h, 0.9f, 0.9f);
    }
}

// Add missing DrawFeatureList and DrawWatermark helper implementations
static void DrawFeatureList(ImDrawList* dl, const ImVec2& display_size)
{
    using namespace overlay;
    if (!IsFeatureListVisible || ActiveFeatures.empty()) return;
    const float padding = 10.0f;
    float y = 40.0f;
    float right = 40.0f;
    for (const auto& s : ActiveFeatures)
    {
        ImVec2 textSize = ImGui::CalcTextSize(s.c_str(), nullptr, false, 1000.0f);
        float x = display_size.x - textSize.x - right;
        dl->AddText(ImGui::GetFont(), (float)FeatureTextSize, ImVec2(x, y), FeatureTextColor, s.c_str());
        y += FeatureTextSize + 6.0f;
    }
}

static void DrawWatermark(ImDrawList* dl, const ImVec2& display_size)
{
    using namespace overlay;
    if (!IsWatermarkVisible || WatermarkText.empty()) return;
    ImVec2 textSize = ImGui::CalcTextSize(WatermarkText.c_str());
    float left = 40.0f;
    float y = display_size.y - (WatermarkSize * 2.2f);
    dl->AddText(ImGui::GetFont(), (float)WatermarkSize, ImVec2(left, y), WatermarkColor, WatermarkText.c_str());
}

// ----- Zeichnen (ImGui DrawList) -----
namespace overlay {
    void DrawCrosshair(ImDrawList* dl, const ImVec2& center)
    {
        if (CrosshairSize <= 0 || LineThickness <= 0) return;

        float angleRad = IsRotating ? (RotationAngleDeg * static_cast<float>(PI_DOUBLE) / 180.0f) : 0.0f;
        ImU32 col = CrosshairActualColor();
        float thickness = static_cast<float>((LineThickness > 1) ? LineThickness : 1);

        std::string shp = CrosshairShape;
        for (auto & c : shp) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        if (shp == "dot")
        {
            float d = static_cast<float>(CrosshairSize);
            dl->AddCircleFilled(center, d * 0.5f, col);
            return;
        }

        if (shp == "plus")
        {
            ImVec2 a = RotatePoint(ImVec2(center.x - CrosshairSize, center.y), center, angleRad);
            ImVec2 b = RotatePoint(ImVec2(center.x + CrosshairSize, center.y), center, angleRad);
            dl->AddLine(a, b, col, thickness);

            ImVec2 c = RotatePoint(ImVec2(center.x, center.y - CrosshairSize), center, angleRad);
            ImVec2 d = RotatePoint(ImVec2(center.x, center.y + CrosshairSize), center, angleRad);
            dl->AddLine(c, d, col, thickness);
            return;
        }

        if (shp == "cross")
        {
            int r = CrosshairSize;
            ImVec2 a = RotatePoint(ImVec2(center.x - r, center.y - r), center, angleRad);
            ImVec2 b = RotatePoint(ImVec2(center.x + r, center.y + r), center, angleRad);
            ImVec2 c = RotatePoint(ImVec2(center.x + r, center.y - r), center, angleRad);
            ImVec2 d = RotatePoint(ImVec2(center.x - r, center.y + r), center, angleRad);
            dl->AddLine(a, b, col, thickness);
            dl->AddLine(c, d, col, thickness);
            return;
        }

        if (shp == "triangle")
        {
            int r = CrosshairSize;
            ImVec2 p1 = RotatePoint(ImVec2(center.x, center.y - r), center, angleRad);
            ImVec2 p2 = RotatePoint(ImVec2(center.x - (0.866f * r), center.y + r / 2.0f), center, angleRad);
            ImVec2 p3 = RotatePoint(ImVec2(center.x + (0.866f * r), center.y + r / 2.0f), center, angleRad);
            dl->AddTriangle(p1, p2, p3, col, thickness);
            return;
        }

        if (shp == "pinwheel")
        {
            int r = CrosshairSize;
            double start = PI_DOUBLE / 4.0;
            for (int i = 0; i < 4; i++)
            {
                double ang = start + i * (PI_DOUBLE / 2.0);
                float x1 = center.x + static_cast<float>(cos(ang) * (r * 0.35));
                float y1 = center.y + static_cast<float>(sin(ang) * (r * 0.35));
                float x2 = center.x + static_cast<float>(cos(ang) * r);
                float y2 = center.y + static_cast<float>(sin(ang) * r);
                ImVec2 p1 = RotatePoint(ImVec2(x1, y1), center, angleRad);
                ImVec2 p2 = RotatePoint(ImVec2(x2, y2), center, angleRad);
                dl->AddLine(p1, p2, col, thickness);
            }
            dl->AddCircleFilled(center, r * 0.12f, col);
            return;
        }

        if (shp == "windmill1954")
        {
            int r = CrosshairSize;
            int vTopY = static_cast<int>(center.y - (2.0f * r));
            int vBotY = static_cast<int>(center.y + (2.0f * r));
            int hHalf = static_cast<int>(2.0f * r);

            dl->AddLine(RotatePoint(ImVec2(center.x, static_cast<float>(vTopY)), center, angleRad), RotatePoint(ImVec2(center.x, static_cast<float>(vBotY)), center, angleRad), col, thickness);
            dl->AddLine(RotatePoint(ImVec2(center.x - hHalf, center.y), center, angleRad), RotatePoint(ImVec2(center.x + hHalf, center.y), center, angleRad), col, thickness);

            int tip = static_cast<int>(2.0f * r);
            ImVec2 pTop = RotatePoint(ImVec2(center.x, static_cast<float>(vTopY)), center, angleRad);
            ImVec2 pTopEnd = RotatePoint(ImVec2(center.x + tip, static_cast<float>(vTopY)), center, angleRad);
            ImVec2 pBot = RotatePoint(ImVec2(center.x, static_cast<float>(vBotY)), center, angleRad);
            ImVec2 pBotEnd = RotatePoint(ImVec2(center.x - tip, static_cast<float>(vBotY)), center, angleRad);
            ImVec2 pRight = RotatePoint(ImVec2(center.x + hHalf, center.y), center, angleRad);
            ImVec2 pRightEnd = RotatePoint(ImVec2(center.x + hHalf, center.y + tip), center, angleRad);
            ImVec2 pLeft = RotatePoint(ImVec2(center.x - hHalf, center.y), center, angleRad);
            ImVec2 pLeftEnd = RotatePoint(ImVec2(center.x - hHalf, center.y - tip), center, angleRad);

            dl->AddLine(pTop, pTopEnd, col, thickness);
            dl->AddLine(pBot, pBotEnd, col, thickness);
            dl->AddLine(pRight, pRightEnd, col, thickness);
            dl->AddLine(pLeft, pLeftEnd, col, thickness);
            return;
        }
    }

    // ----- Implementation von draw_gui, benutzt die obigen Helfer -----
    void draw_gui()
    {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 display_size = ImGui::GetIO().DisplaySize;

        // Feature list (oben rechts)
        DrawFeatureList(dl, display_size);

        // Watermark (unten links)
        DrawWatermark(dl, display_size);

        // Sync crosshair settings from config so changes in menu apply immediately
        {
            static const char* _types[] = { "Cross", "Dot", "Plus", "Triangle", "Circle", "Windmill1954", "Pinwheel" };
            CrosshairSize = config->crosshair.size;
            LineThickness = config->crosshair.thickness;
            int r = (int)(config->crosshair.color[0] * 255.0f);
            int g = (int)(config->crosshair.color[1] * 255.0f);
            int b = (int)(config->crosshair.color[2] * 255.0f);
            int a = (int)(config->crosshair.color[3] * 255.0f);
            CrosshairColor = IM_COL32(r, g, b, a);
            int t = config->crosshair.type;
            if (t < 0) t = 0; if (t >= (int)(sizeof(_types)/sizeof(_types[0]))) t = 0;
            CrosshairShape = _types[t];
            RainbowCrosshair = config->crosshair.rainbow;
            IsRotating = config->crosshair.rotating;
            if (IsRotating) {
                float delta = ImGui::GetIO().DeltaTime;
                RotationAngleDeg += config->crosshair.rotationSpeed * 60.0f * delta;
                if (RotationAngleDeg > 360.0f) RotationAngleDeg = fmodf(RotationAngleDeg, 360.0f);
            }
        }

        // Draw crosshair only if enabled in config
        if (config->crosshair.enabled) {
            ImVec2 center = ImVec2(display_size.x * 0.5f, display_size.y * 0.5f);
            DrawCrosshair(dl, center);
        }

        // Hinweis: RotationAngleDeg kann auﬂerhalb hochgez‰hlt werden (z.B. in loop())
    }
}

// Minimal implementations for functions declared in overlay.h to satisfy linker
namespace window {
    LRESULT WINAPI WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, Msg, wParam, lParam))
            return true;
        return ::DefWindowProcW(hWnd, Msg, wParam, lParam);
    }

    void cleanup() {
        // nothing to cleanup here in this module
    }

    bool create_window() {
        // This module uses external window creation (load.h), so just return true
        return true;
    }

    bool create_device() {
        // Device created elsewhere (load.h); return true
        return true;
    }

    void new_frame() {
        // Forward to ImGui new frame helpers if needed
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void draw() {
        // call the GUI draw implementation
        overlay::draw_gui();
    }
}

namespace overlay {
    bool initialize(HWND window) {
        target = window;
        return true;
    }

    bool scale() {
        // scaling handled elsewhere; return true
        return true;
    }

    void click_through(bool click) {
        if (!target) return;
        LONG_PTR ex = GetWindowLongPtr(target, GWL_EXSTYLE);
        if (click) {
            SetWindowLongPtr(target, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
        } else {
            SetWindowLongPtr(target, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        }
    }

    void loop() {
        // This project uses a custom loop in load.h; leave empty.
    }
}