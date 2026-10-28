#pragma once
#include "core/session.hpp"
#include <windows.h>
#include <d3d9.h>
#include <string>
namespace training {
void set_overlay_status(std::string status,bool failed=false);
void draw_panel(Session& session);
void show_overlay(bool value);
bool install_overlay(Session& session, std::string& reason);
// Used by the standalone preview; DLL uses the hooked real device.
bool init_imgui(HWND window, IDirect3DDevice9* device);
void invalidate_imgui_device_objects();
void shutdown_imgui();
void frame_imgui(Session& session);
LRESULT input_imgui(HWND window, UINT message, WPARAM wparam, LPARAM lparam, bool& consumed);
}
