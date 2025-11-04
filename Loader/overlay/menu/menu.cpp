#include "menu.h"
#include <random>
#include <imgui_internal.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <atomic>

namespace menu {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> xDist(0.0f, 1.0f);
    static std::uniform_real_distribution<float> speedDist(0.1f, 0.35f);
    static std::uniform_real_distribution<float> sizeDist(1.0f, 4.0f);

    // Accent colors
    static const ImVec4 accentPurple = ImVec4(0.41f, 0.25f, 0.78f, 1.00f);
    static const ImVec4 accentPurpleDim = ImVec4(0.41f, 0.25f, 0.78f, 0.55f);

    // Config files directory
    static const std::string kConfigsDir = "configs";

    // Autoclicker timing state
    struct AutoClickState {
        std::chrono::high_resolution_clock::time_point nextClickTime;
        bool active = false; // whether autoclicker is currently active (for toggle mode)
    } autoclickState;

    // clamp CPS to allowed range
    static void ClampAutoclickerRange() {
        if (config->autoclicker.minCps < 1) config->autoclicker.minCps = 1;
        if (config->autoclicker.maxCps < 1) config->autoclicker.maxCps = 1;
        if (config->autoclicker.minCps > 30) config->autoclicker.minCps = 30;
        if (config->autoclicker.maxCps > 30) config->autoclicker.maxCps = 30;
        if (config->autoclicker.maxCps < config->autoclicker.minCps) config->autoclicker.maxCps = config->autoclicker.minCps;
    }

    // Generate next interval (seconds) based on min/max CPS with humanization
    static double GenerateNextIntervalSec() {
        ClampAutoclickerRange();
        int minC = config->autoclicker.minCps;
        int maxC = config->autoclicker.maxCps;
        std::uniform_int_distribution<int> cpsDist(minC, maxC);
        int cps = cpsDist(gen);

        double base = 1.0 / static_cast<double>(cps);

        if (!config->autoclicker.humanize) return base;

        // humanization: add jitter up to +/- 20% and occasional micro-pauses
        std::uniform_real_distribution<double> jitter(-0.18, 0.18);
        double j = jitter(gen);
        double interval = base * (1.0 + j);

        // occasional micro pause (1% chance add 30-120 ms)
        std::uniform_real_distribution<double> rare(0.0, 1.0);
        if (rare(gen) < 0.01) {
            std::uniform_real_distribution<double> extra(0.03, 0.12);
            interval += extra(gen);
        }

        if (interval < 0.008) interval = 0.008;
        return interval;
    }

    static void PerformClickEvent() {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, inputs, sizeof(INPUT));
    }

    static void UpdateAutoClicker() {
        if (!config->autoclicker.enabled) return;
        // if key is 0, disabled
        if (config->autoclicker.key == 0) return;

        // handle toggle mode key press (edge detect)
        bool keyDown = (GetAsyncKeyState(config->autoclicker.key) & 0x8000) != 0;
        static bool prevKeyState = false;
        if (config->autoclicker.mode == 1) { // toggle
            if (keyDown && !prevKeyState) {
                // on press, flip toggled state
                bool cur = g_autoclickerToggled.load();
                g_autoclickerToggled.store(!cur);
                // reset scheduling
                autoclickState.nextClickTime = std::chrono::high_resolution_clock::time_point();
            }
            prevKeyState = keyDown;
        } else { // hold
            // ensure toggled state is off
            g_autoclickerToggled.store(false);
        }

        bool shouldClick = (config->autoclicker.mode == 1) ? g_autoclickerToggled.load() : keyDown;

        if (shouldClick) {
            auto now = std::chrono::high_resolution_clock::now();
            if (autoclickState.nextClickTime.time_since_epoch().count() == 0) {
                double interval = GenerateNextIntervalSec();
                autoclickState.nextClickTime = now + std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::duration<double>(interval));
            }
            if (now >= autoclickState.nextClickTime) {
                PerformClickEvent();
                double interval = GenerateNextIntervalSec();
                autoclickState.nextClickTime = now + std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::duration<double>(interval));
            }
        } else {
            autoclickState.nextClickTime = std::chrono::high_resolution_clock::time_point();
        }
    }

    static bool EnsureConfigsDir() {
        std::error_code ec;
        std::filesystem::create_directories(kConfigsDir, ec);
        return !ec;
    }

    static bool SaveConfigToFile(const std::string& name) {
        if (name.empty()) return false;
        if (!EnsureConfigsDir()) return false;
        std::string path = kConfigsDir + "/" + name + ".cfg";
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs.write(reinterpret_cast<const char*>(config), sizeof(*config));
        return ofs.good();
    }

    static bool LoadConfigFromFile(const std::string& name) {
        if (name.empty()) return false;
        std::string path = kConfigsDir + "/" + name + ".cfg";
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        Config tmp;
        ifs.read(reinterpret_cast<char*>(&tmp), sizeof(tmp));
        if (!ifs) return false;
        *config = tmp;
        return true;
    }

    static std::vector<std::string> ListConfigs() {
        std::vector<std::string> out;
        std::error_code ec;
        if (!std::filesystem::exists(kConfigsDir, ec)) return out;
        for (auto it = std::filesystem::directory_iterator(kConfigsDir, ec); it != std::filesystem::directory_iterator(); ++it) {
            if (ec) break;
            const auto& ent = *it;
            if (!ent.is_regular_file()) continue;
            auto p = ent.path();
            if (p.extension() == ".cfg") {
                out.push_back(p.stem().string());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    static bool DeleteConfigFile(const std::string& name) {
        std::string path = kConfigsDir + "/" + name + ".cfg";
        std::error_code ec;
        return std::filesystem::remove(path, ec);
    }

    // Helper: get readable key name for virtual-key code
    static std::string GetKeyName(int vk) {
        if (vk == 0) return std::string("None");
        UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
        LONG lParam = (scan << 16);
        char name[64] = {0};
        if (GetKeyNameTextA(lParam, name, (int)sizeof(name))) return std::string(name);
        switch (vk) {
            case VK_INSERT: return "INSERT";
            case VK_DELETE: return "DELETE";
            case VK_HOME: return "HOME";
            case VK_END: return "END";
            case VK_PRIOR: return "PGUP";
            case VK_NEXT: return "PGDN";
            case VK_UP: return "UP";
            case VK_DOWN: return "DOWN";
            case VK_LEFT: return "LEFT";
            case VK_RIGHT: return "RIGHT";
            case VK_LBUTTON: return "LBUTTON";
            case VK_RBUTTON: return "RBUTTON";
            default: {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", vk);
                return std::string(buf);
            }
        }
    }

    void InitStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(16, 16);
        style.WindowRounding = 12.0f;
        style.FramePadding = ImVec2(6, 6);
        style.FrameRounding = 10.0f;
        style.ItemSpacing = ImVec2(12, 8);
        style.ItemInnerSpacing = ImVec2(8, 6);
        style.IndentSpacing = 25.0f;
        style.ScrollbarSize = 12.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabMinSize = 8.0f;
        style.GrabRounding = 6.0f;
        style.PopupRounding = 8.0f;
        style.Alpha = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.43f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.05f, 0.07f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.12f, 0.13f, 0.16f, 0.6f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.02f, 0.03f, 0.04f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.03f, 0.04f, 0.06f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
        colors[ImGuiCol_ScrollbarGrab] = accentPurpleDim;
        colors[ImGuiCol_ScrollbarGrabHovered] = accentPurple;
        colors[ImGuiCol_ScrollbarGrabActive] = accentPurple;
        colors[ImGuiCol_CheckMark] = accentPurple;
        colors[ImGuiCol_SliderGrab] = accentPurpleDim;
        colors[ImGuiCol_SliderGrabActive] = accentPurple;
        colors[ImGuiCol_Button] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.10f, 0.10f, 0.12f, 0.4f);
        colors[ImGuiCol_Tab] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    }

    // Custom toggle switch (rounded)
    static bool ToggleSwitch(const char* id, bool* v) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float height = 22.0f;
        float width = 44.0f;
        ImRect bb(p, ImVec2(p.x + width, p.y + height));
        ImGui::ItemSize(bb.GetSize());
        if (!ImGui::ItemAdd(bb, ImGui::GetID(id))) return false;

        bool pressed = ImGui::InvisibleButton(id, bb.GetSize());
        if (pressed) *v = !*v;

        ImU32 colBg = (*v) ? ImGui::ColorConvertFloat4ToU32(accentPurple) : ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f,0.20f,0.22f,1.0f));
        ImU32 colHandle = ImGui::ColorConvertFloat4ToU32(ImVec4(0.94f,0.94f,0.96f,1.0f));

        draw->AddRectFilled(bb.Min, bb.Max, colBg, height * 0.5f);
        float hx = bb.Min.x + ( *v ? (width - height + 2.0f) : 2.0f );
        draw->AddCircleFilled(ImVec2(hx + height*0.5f - 2.0f, bb.Min.y + height*0.5f), (height*0.5f) - 3.0f, colHandle);

        ImGui::Dummy(bb.GetSize());
        return pressed;
    }

    static void DrawPillLabel(const char* text) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 pad(12, 6);
        ImVec2 size(textSize.x + pad.x * 2, textSize.y + pad.y * 2);
        ImRect bb(p, ImVec2(p.x + size.x, p.y + size.y));
        ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f,0.11f,0.13f,1.0f));
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.86f,0.86f,0.88f,1.0f));
        draw->AddRectFilled(bb.Min, bb.Max, bg, 12.0f);
        draw->AddText(ImVec2(bb.Min.x + pad.x, bb.Min.y + pad.y/2), textCol, text);
        ImGui::Dummy(size);
    }

    // Continuous menu particles that loop from top to bottom and stay anchored to the menu content
    void UpdateMenuParticles(const ImVec2& areaSize) {
        if (!config->particles.enabled) return;
        const float deltaTime = ImGui::GetIO().DeltaTime;

        size_t target = static_cast<size_t>(config->particles.particleCount);
        while (globals->particles.size() < target) {
            Particle p;
            p.Position = ImVec2(xDist(gen) * areaSize.x, - (5.0f + xDist(gen) * 40.0f));
            // vertical speed larger for bigger/smoother fall
            float vy = 30.0f + speedDist(gen) * 120.0f * (config->particles.particleSpeed);
            p.Velocity = ImVec2((xDist(gen) - 0.5f) * 8.0f, vy);
            p.Size = sizeDist(gen) * (config->particles.particleSize * 0.9f);
            p.Alpha = 0.55f + xDist(gen) * 0.45f;
            globals->particles.push_back(p);
        }

        for (auto& p : globals->particles) {
            p.Position.x += p.Velocity.x * deltaTime;
            p.Position.y += p.Velocity.y * deltaTime;

            // horizontal wrap-around
            if (p.Position.x < -40.0f) p.Position.x = areaSize.x + 40.0f;
            if (p.Position.x > areaSize.x + 40.0f) p.Position.x = -40.0f;

            // loop vertically: when below, reset to above with random x
            if (p.Position.y > areaSize.y + 20.0f) {
                p.Position.y = - (5.0f + xDist(gen) * 40.0f);
                p.Position.x = xDist(gen) * areaSize.x;
            }
        }
    }

    void DrawMenuParticles(const ImVec2& areaPos, const ImVec2& areaSize) {
        if (!config->particles.enabled) return;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        for (const auto& p : globals->particles) {
            // Only draw within area bounds (with margin)
            if (p.Position.x < -40.0f || p.Position.x > areaSize.x + 40.0f) continue;
            if (p.Position.y < -60.0f || p.Position.y > areaSize.y + 60.0f) continue;

            ImVec2 pos = ImVec2(areaPos.x + p.Position.x, areaPos.y + p.Position.y);
            const ImU32 col = IM_COL32(220, 230, 255, static_cast<int>(220 * p.Alpha));
            draw_list->AddCircleFilled(pos, p.Size * 0.6f, col);
        }
    }

    void DrawWatermark() {
        if (!config->menu.drawWatermark) return;
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        ImGuiIO& io = ImGui::GetIO();

        const char* text = "OniV2 | FPS: %.0f";
        char buffer[256];
        snprintf(buffer, sizeof(buffer), text, io.Framerate);

        ImVec2 textSize = ImGui::CalcTextSize(buffer);
        ImVec2 pos = ImVec2(io.DisplaySize.x - textSize.x - 20, 10);

        draw_list->AddRectFilled(ImVec2(pos.x - 10, pos.y - 5), ImVec2(pos.x + textSize.x + 10, pos.y + textSize.y + 5), IM_COL32(0,0,0,160), 8.0f);
        draw_list->AddRect(ImVec2(pos.x - 10, pos.y - 5), ImVec2(pos.x + textSize.x + 10, pos.y + textSize.y + 5), IM_COL32(255,255,255,30), 8.0f);
        draw_list->AddText(pos, IM_COL32(255,255,255,255), buffer);
    }

    void Draw() {
        // Window setup
        ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        ImGui::Begin("OniV2", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* winDraw = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        // soft rounded background for window content
        winDraw->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), ImGui::GetColorU32(ImVec4(0.03f,0.035f,0.045f,0.95f)), 16.0f);

        // Title centered
        const char* title = "Banwave wtf";
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImVec2 titlePos = ImVec2(winPos.x + winSize.x * 0.5f - titleSize.x * 0.5f, winPos.y + 8.0f);
        winDraw->AddText(titlePos, ImGui::GetColorU32(accentPurple), title);

        ImGui::Dummy(ImVec2(0, 28));

        // Left navigation + content
        float leftWidth = 200.0f;
        ImGui::BeginChild("LeftNav", ImVec2(leftWidth, 0), true);
        static int selectedIndex = 0;
        const char* items[] = { "Crosshair", "Autoclicker", "Configs", "Settings" };
        for (int i = 0; i < IM_ARRAYSIZE(items); ++i) {
            ImGui::PushID(i);
            ImVec2 itemPos = ImGui::GetCursorScreenPos();
            ImVec2 itemSize = ImVec2(leftWidth - 24.0f, 40.0f);
            ImRect bb(itemPos, ImVec2(itemPos.x + itemSize.x, itemPos.y + itemSize.y));
            bool active = (selectedIndex == i);

            if (active) {
                winDraw->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(0.06f,0.07f,0.09f,1.0f)), 10.0f);
                winDraw->AddRectFilled(ImVec2(bb.Min.x + 8.0f, bb.Min.y + 10.0f), ImVec2(bb.Min.x + 12.0f, bb.Max.y - 10.0f), ImGui::GetColorU32(accentPurple), 4.0f);
            }

            if (ImGui::InvisibleButton("nav_btn", itemSize)) selectedIndex = i;
            ImGui::SameLine(); ImGui::SetCursorScreenPos(ImVec2(itemPos.x + 22.0f, itemPos.y + 12.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, active ? accentPurple : ImVec4(0.78f,0.81f,0.85f,1.0f));
            ImGui::TextUnformatted(items[i]);
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("Content", ImVec2(0, 0), false);

        // Content bounds for particles
        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        // draw inner panel background
        ImVec2 panelMin = ImVec2(contentPos.x - 8.0f, contentPos.y - 8.0f);
        ImVec2 panelMax = ImVec2(contentPos.x + contentSize.x + 8.0f, contentPos.y + contentSize.y + 8.0f);
        winDraw->AddRectFilled(panelMin, panelMax, ImGui::GetColorU32(ImVec4(0.05f,0.055f,0.065f,0.9f)), 12.0f);

        // Update & draw particles anchored to content area (persistent loop)
        UpdateMenuParticles(contentSize);
        DrawMenuParticles(contentPos, contentSize);

        ImGui::Spacing(); ImGui::Spacing();

        // Show content depending on left nav selection
        if (selectedIndex == 0) { // Crosshair
            ImGui::Text("Crosshair");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Enable Crosshair", &config->crosshair.enabled);
            if (config->crosshair.enabled) {
                ImGui::SliderInt("Size", &config->crosshair.size, 1, 50);
                ImGui::SliderInt("Thickness", &config->crosshair.thickness, 1, 10);
                ImGui::ColorEdit4("Color", config->crosshair.color, ImGuiColorEditFlags_AlphaBar);

                const char* types[] = { "Cross", "Dot", "Plus", "Triangle", "Circle", "Windmill1954", "Pinwheel" };
                ImGui::Combo("Type", &config->crosshair.type, types, IM_ARRAYSIZE(types));

                ImGui::Checkbox("Rainbow Effect", &config->crosshair.rainbow);
                ImGui::Checkbox("Rotating", &config->crosshair.rotating);
                if (config->crosshair.rotating) ImGui::SliderFloat("Rotation Speed", &config->crosshair.rotationSpeed, 0.1f, 5.0f, "%.1f");
            }

        } else if (selectedIndex == 1) { // Autoclicker
            ImGui::Text("Autoclicker");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Enable Autoclicker", &config->autoclicker.enabled);
            ImGui::Text("Key:"); ImGui::SameLine(); ImGui::Text("%d", config->autoclicker.key);
            ImGui::SliderInt("Minimum CPS", &config->autoclicker.minCps, 1, 200);
            ImGui::SliderInt("Maximum CPS", &config->autoclicker.maxCps, 1, 200);

            // Autoclicker mode: toggle or hold
            const char* modes[] = { "Hold", "Toggle" };
            ImGui::Combo("Mode", &config->autoclicker.mode, modes, IM_ARRAYSIZE(modes));
            ImGui::Checkbox("Humanize", &config->autoclicker.humanize);
            ImGui::Text("Current interval: %.4f ms", GenerateNextIntervalSec() * 1000.0f);

        } else if (selectedIndex == 2) { // Configs
            ImGui::Text("Configs");
            ImGui::Separator();
            ImGui::Spacing();

            // Simple config save/load UI
            static char cfgName[64] = "default";
            ImGui::InputText("Name", cfgName, sizeof(cfgName));
            ImGui::SameLine();
            if (ImGui::Button("Save")) {
                bool ok = SaveConfigToFile(cfgName);
                (void)ok; // could show feedback
            }
            ImGui::SameLine();
            if (ImGui::Button("Save As Default")) {
                SaveConfigToFile("default");
            }

            ImGui::Spacing();
            ImGui::Text("Available configs:");
            ImGui::BeginChild("ConfigsList", ImVec2(0, 160), true);
            static int sel = -1;
            auto list = ListConfigs();
            for (int i = 0; i < (int)list.size(); ++i) {
                bool active = (sel == i);
                if (ImGui::Selectable(list[i].c_str(), active)) sel = i;
            }
            ImGui::EndChild();

            ImGui::Spacing();
            if (ImGui::Button("Load Selected") && sel >= 0 && sel < (int)list.size()) {
                LoadConfigFromFile(list[sel]);
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected") && sel >= 0 && sel < (int)list.size()) {
                DeleteConfigFile(list[sel]);
                sel = -1;
            }

        } else if (selectedIndex == 3) { // Settings
             ImGui::Text("Settings");
             ImGui::Separator();
             ImGui::Spacing();
             ImGui::Checkbox("VSync", &config->menu.vsync);
             ImGui::Checkbox("Stream Proof", &config->menu.streamproof);


         } else {
             ImGui::Text(items[selectedIndex]);
             ImGui::Separator();
             ImGui::Spacing();
             ImGui::Text("No controls added yet.");
         }

          ImGui::EndChild(); // Content

          ImGui::End();
      }
 }
