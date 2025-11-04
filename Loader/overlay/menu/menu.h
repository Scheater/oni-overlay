#pragma once
#include <imgui.h>
#include <vector>
#include <Windows.h>
#include <atomic>

// UI Theme Colors
#define THEME_BACKGROUND     ImVec4(0.07f, 0.07f, 0.09f, 1.00f)
#define THEME_ACCENT        ImVec4(0.28f, 0.56f, 1.00f, 1.00f)
#define THEME_ACCENT_DIM    ImVec4(0.28f, 0.56f, 1.00f, 0.50f)
#define THEME_TEXT          ImVec4(0.86f, 0.86f, 0.86f, 1.00f)
#define THEME_TEXT_DIM      ImVec4(0.60f, 0.60f, 0.60f, 1.00f)

// Particle System
struct Particle {
    ImVec2 Position = ImVec2(0.0f, 0.0f);
    ImVec2 Velocity = ImVec2(0.0f, 0.0f);
    float Size = 0.0f;
    float Alpha = 0.0f;
    float Life = 0.0f;
};

// Configuration structure
struct Config {
    struct {
        bool vsync = true;
        bool streamproof = false;
        bool drawWatermark = true;
        int menuKey = VK_INSERT;

    } menu;

    struct {
        bool enabled = true;
        int size = 20;
        int thickness = 2;
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // White
        bool rotating = false;
        bool rainbow = false;
        float rotationSpeed = 1.0f;
        int type = 0; // 0=Cross, 1=Dot, 2=Plus, 3=Triangle, 4=Circle, 5=Windmill1954, 6=Pinwheel
    } crosshair;

    // Aimbot configuration (added to support menu controls)
    struct {
        bool enabled = false;
        int bone = 1; // default: Neck
        int x = 16;
        int y = 16;
        int key = VK_LBUTTON;
    } aimbot;

    struct {
        bool enabled = false;
        int minCps = 8;
        int maxCps = 12;
        int key = VK_XBUTTON1;
        int mode = 0; // 0 = hold (click while key held), 1 = toggle (press to start/stop)
        bool humanize = true; // enable humanization jitter
    } autoclicker;

    struct {
        bool enabled = true;
        float particleCount = 50.0f;
        float particleSpeed = 1.0f;
        float particleSize = 2.0f;
    } particles;
};

// Global variables
struct Globals {
    bool running = false;
    bool menuOpen = false;
    std::vector<Particle> particles;
};

inline Config* config = new Config();
inline Globals* globals = new Globals();

// Flag used when capturing a new menu key in the settings UI
inline std::atomic<bool> g_capturingMenuKey{ false };

// Runtime autoclicker toggle state (not persisted)
inline std::atomic<bool> g_autoclickerToggled{ false };

// Flag used when capturing a new autoclicker key in the UI
inline std::atomic<bool> g_capturingAutoKey{ false };

namespace menu {
    void InitStyle();
    void Draw();
    void DrawWatermark();
    void UpdateParticles();
    void DrawParticles();
    void UpdateAutoClicker();
}

namespace drawlist {
    void Test();
    void DrawCrosshair();
}
