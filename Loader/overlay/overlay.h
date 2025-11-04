#pragma once
#include <iostream>

#include <dwmapi.h>

#include "menu/menu.h"

#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

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

	void draw_gui(); // Declaration added to match implementation

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
	inline int CrosshairSize = 18;        // Radius / half length
	inline int LineThickness = 3;
	inline ImU32 CrosshairColor = IM_COL32(255,255,255,255);
	inline std::string CrosshairShape = "Dot"; // "Dot","Plus","Cross","Triangle","Pinwheel","Windmill1954"

	inline bool IsRotating = false;
	inline float RotationAngleDeg = 0.0f; // used when rotating
	inline bool RainbowCrosshair = false;

	// Constant for PI
	inline constexpr double PI_DOUBLE = 3.14159265358979323846;
}