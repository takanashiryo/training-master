#include "ui/overlay.hpp"
#include "core/playback_rate.hpp"
#include "core/judgment.hpp"
#include "platform/log.hpp"
#include "ui/chart_view.hpp"
#include "training_about.hpp"
#include "ui/preferences.hpp"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <MinHook.h>
#include <mutex>
#include <utility>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <array>
#include <atomic>
#include <deque>
#include <wrl/client.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace training {
namespace {
std::mutex status_mutex;
std::string status_text="Initializing";
bool status_failed{};
std::atomic<bool> visible{false};
bool options_open{};
// A single palette shared by the main panel, Option and About. Keep ImGui's
// default metrics so the theme does not change layout or hit targets.
void apply_theme() {
    ImGui::StyleColorsDark();
    auto& style=ImGui::GetStyle();
    style.WindowRounding=style.ChildRounding=style.FrameRounding=0.f;
    style.PopupRounding=style.ScrollbarRounding=style.GrabRounding=style.TabRounding=0.f;
    auto& c=style.Colors;
    c[ImGuiCol_Text]={.88f,.92f,.95f,1.f};
    c[ImGuiCol_TextDisabled]={.49f,.57f,.63f,1.f};
    c[ImGuiCol_WindowBg]={.055f,.073f,.091f,1.f};
    c[ImGuiCol_ChildBg]={0,0,0,0};
    c[ImGuiCol_PopupBg]={.075f,.098f,.12f,.98f};
    c[ImGuiCol_Border]={.23f,.30f,.35f,.65f};
    c[ImGuiCol_BorderShadow]={0,0,0,0};
    c[ImGuiCol_FrameBg]={.12f,.16f,.19f,1.f};
    c[ImGuiCol_FrameBgHovered]={.17f,.25f,.28f,1.f};
    c[ImGuiCol_FrameBgActive]={.18f,.33f,.35f,1.f};
    c[ImGuiCol_TitleBg]={.065f,.09f,.11f,1.f};
    c[ImGuiCol_TitleBgActive]={.10f,.18f,.21f,1.f};
    c[ImGuiCol_TitleBgCollapsed]={.065f,.09f,.11f,.9f};
    c[ImGuiCol_MenuBarBg]={.085f,.115f,.14f,1.f};
    c[ImGuiCol_ScrollbarBg]={.04f,.06f,.075f,.5f};
    c[ImGuiCol_ScrollbarGrab]={.24f,.32f,.37f,1.f};
    c[ImGuiCol_ScrollbarGrabHovered]={.32f,.45f,.49f,1.f};
    c[ImGuiCol_ScrollbarGrabActive]={.28f,.65f,.62f,1.f};
    c[ImGuiCol_CheckMark]={.40f,.88f,.76f,1.f};
    c[ImGuiCol_CheckboxSelectedBg]={.10f,.31f,.29f,1.f};
    c[ImGuiCol_SliderGrab]={.26f,.69f,.61f,1.f};
    c[ImGuiCol_SliderGrabActive]={.45f,.92f,.79f,1.f};
    c[ImGuiCol_Button]={.15f,.21f,.25f,1.f};
    c[ImGuiCol_ButtonHovered]={.20f,.36f,.38f,1.f};
    c[ImGuiCol_ButtonActive]={.15f,.46f,.42f,1.f};
    c[ImGuiCol_Header]={.14f,.23f,.27f,1.f};
    c[ImGuiCol_HeaderHovered]={.19f,.35f,.37f,1.f};
    c[ImGuiCol_HeaderActive]={.16f,.40f,.37f,1.f};
    c[ImGuiCol_Separator]={.24f,.32f,.37f,.7f};
    c[ImGuiCol_SeparatorHovered]={.28f,.65f,.62f,1.f};
    c[ImGuiCol_SeparatorActive]={.40f,.88f,.76f,1.f};
    c[ImGuiCol_ResizeGrip]={.27f,.56f,.53f,.25f};
    c[ImGuiCol_ResizeGripHovered]={.32f,.73f,.65f,.7f};
    c[ImGuiCol_ResizeGripActive]={.40f,.88f,.76f,1.f};
    c[ImGuiCol_InputTextCursor]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_Tab]={.11f,.17f,.21f,1.f};
    c[ImGuiCol_TabHovered]=c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_TabSelected]=c[ImGuiCol_Header];
    c[ImGuiCol_TabSelectedOverline]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_TabDimmed]={.08f,.12f,.15f,1.f};
    c[ImGuiCol_TabDimmedSelected]={.13f,.20f,.23f,1.f};
    c[ImGuiCol_TabDimmedSelectedOverline]=c[ImGuiCol_ScrollbarGrab];
    c[ImGuiCol_PlotLines]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_PlotLinesHovered]={1.f,.75f,.42f,1.f};
    c[ImGuiCol_PlotHistogram]=c[ImGuiCol_SliderGrab];
    c[ImGuiCol_PlotHistogramHovered]=c[ImGuiCol_PlotLinesHovered];
    c[ImGuiCol_TableHeaderBg]={.11f,.17f,.21f,1.f};
    c[ImGuiCol_TableBorderStrong]=c[ImGuiCol_Border];
    c[ImGuiCol_TableBorderLight]={.18f,.24f,.28f,.6f};
    c[ImGuiCol_TableRowBgAlt]={.5f,.65f,.7f,.035f};
    c[ImGuiCol_TextLink]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_TextSelectedBg]={.20f,.64f,.55f,.32f};
    c[ImGuiCol_TreeLines]=c[ImGuiCol_Separator];
    c[ImGuiCol_DragDropTarget]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_DragDropTargetBg]={.20f,.64f,.55f,.18f};
    c[ImGuiCol_NavCursor]=c[ImGuiCol_CheckMark];
    c[ImGuiCol_ModalWindowDimBg]={.015f,.025f,.035f,.65f};
}
void section(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::SeparatorText(i18n::text(label));
    ImGui::PopStyleColor();
}
bool state_button(const char* label,ImVec2 size,bool selected) {
    if(selected) {
        ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    }
    const bool pressed=ImGui::Button(label,size);
    if(selected) ImGui::PopStyleColor(2);
    return pressed;
}
std::atomic<bool> capturing_hotkey{false};
std::atomic<unsigned> captured_hotkey{};
struct HotkeyEvent { DWORD time; bool down, matches; };
std::mutex hotkey_events_mutex;
std::vector<HotkeyEvent> hotkey_events;
std::array<bool,256> capture_keys{};
bool wait_hotkey_release{};
bool modifiers_match(const OverlayHotkey& hotkey,bool queued=false) {
    const auto state=queued?GetKeyState:GetAsyncKeyState;
    return ((state(VK_CONTROL)&0x8000)!=0)==hotkey.ctrl &&
           ((state(VK_MENU)&0x8000)!=0)==hotkey.alt &&
           ((state(VK_SHIFT)&0x8000)!=0)==hotkey.shift;
}
void poll_overlay_hotkey() {
    struct PolledPress { DWORD from, until; bool held; };
    static OverlayHotkey previous_hotkey;
    static bool initialized{};
    static bool down{};
    static bool message_down{};
    static DWORD message_time{}, sampled_at{};
    static std::deque<PolledPress> polled_presses;
    const auto hotkey=preferences().snapshot().hotkey;
    const auto now=GetTickCount();
    const bool next=(GetAsyncKeyState(static_cast<int>(hotkey.key))&0x8000)!=0;
    std::vector<HotkeyEvent> events;
    { std::lock_guard lock(hotkey_events_mutex); events.swap(hotkey_events); }
    const bool first=!initialized;
    if(first || previous_hotkey!=hotkey) {
        previous_hotkey=hotkey; initialized=true;
        polled_presses.clear(); message_down=false;
        if(!first) { down=next; sampled_at=now; return; }
    }
    if(capturing_hotkey || wait_hotkey_release) {
        if(!capturing_hotkey && !next) wait_hotkey_release=false;
        polled_presses.clear(); message_down=false;
        down=next; sampled_at=now; return;
    }
    // A physical press may be seen before the game pumps its WM_KEYDOWN.
    // Keep its observed interval after release so that delayed messages only
    // acknowledge that press; a later quick tap must still toggle normally.
    const auto before=[](DWORD a,DWORD b) { return static_cast<LONG>(a-b)<0; };
    if(down && !polled_presses.empty() && polled_presses.back().held) {
        polled_presses.back().until=now;
        polled_presses.back().held=next;
    }
    unsigned presses{};
    for(const auto& event:events) {
        message_down=event.down;
        if(!event.down) continue;
        message_time=event.time;
        if(!event.matches) continue;
        const auto match=std::find_if(polled_presses.begin(),polled_presses.end(),[&](const PolledPress& press) {
            return !before(event.time,press.from) && !before(press.until,event.time);
        });
        if(match!=polled_presses.end()) polled_presses.erase(match);
        else ++presses;
        std::erase_if(polled_presses,[&](const PolledPress& press) { return !press.held && before(press.until,event.time); });
    }
    if(next && !down && !(message_down && (first || !before(message_time,sampled_at))) && modifiers_match(hotkey)) {
        ++presses;
        polled_presses.push_back({first?now:sampled_at,now,true});
        // Bound history when a launcher consumes every keyboard message.
        if(polled_presses.size()>128) polled_presses.pop_front();
    }
    if(presses%2) {
        DWORD foreground_pid{}; GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
        const bool typing=visible.load() && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
        if(foreground_pid==GetCurrentProcessId() && !(typing && !hotkey.ctrl && !hotkey.alt)) visible=!visible;
    }
    down=next; sampled_at=now;
}
std::recursive_mutex imgui_mutex;
std::mutex motion_mutex;
struct MouseMotion { HWND window{}; WPARAM buttons{}; LPARAM position{}; bool pending{}; };
MouseMotion motion;
std::atomic<unsigned> overlay_mouse_buttons{};
HWND game_window{};
WNDPROC old_proc{};
IDirect3DDevice9* game_device{};
IDirect3DDevice9* cover_device{};
IDirect3DTexture9* cover_texture{};
std::shared_ptr<const SongInfo> cover_info;
JacketPixels cover_pixels;
bool cover_attempted{};
float client_width{},client_height{},canvas_width{},canvas_height{};
bool cursor_borrowed{},os_cursor_hidden{};
BOOL native_cursor_visible{};
HCURSOR native_os_cursor{};
class WindowDpiScope {
    using GetContext=HANDLE(WINAPI*)(HWND);
    using SetContext=HANDLE(WINAPI*)(HANDLE);
    static SetContext setter() {
        static const auto value=reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext"));
        return value;
    }
    HANDLE previous_{};
public:
    explicit WindowDpiScope(HWND window) {
        static const auto get=reinterpret_cast<GetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowDpiAwarenessContext"));
        if(window && get && setter()) previous_=setter()(get(window));
    }
    ~WindowDpiScope() { if(previous_) setter()(previous_); }
};
bool update_geometry() {
    RECT client{};
    if(!game_window || !cover_device || !GetClientRect(game_window,&client) || client.right<=0 || client.bottom<=0) return false;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> back;
    D3DSURFACE_DESC desc{};
    if(FAILED(cover_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)) || FAILED(back->GetDesc(&desc))) return false;
    client_width=static_cast<float>(client.right); client_height=static_cast<float>(client.bottom);
    canvas_width=static_cast<float>(desc.Width); canvas_height=static_cast<float>(desc.Height);
    return canvas_width>0 && canvas_height>0;
}
void map_mouse_position(float x,float y) {
    if(client_width>0 && client_height>0)
        ImGui::GetIO().AddMousePosEvent(x*canvas_width/client_width,y*canvas_height/client_height);
}
void flush_mouse_motion() {
    MouseMotion latest;
    { std::lock_guard lock(motion_mutex); latest=motion; motion.pending=false; }
    if(!latest.pending) return;
    ImGui_ImplWin32_WndProcHandler(latest.window,WM_MOUSEMOVE,latest.buttons,latest.position);
    map_mouse_position(static_cast<float>(static_cast<short>(LOWORD(latest.position))),
                       static_cast<float>(static_cast<short>(HIWORD(latest.position))));
}
unsigned mouse_button_bit(UINT message,WPARAM wparam) {
    switch(message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: return 1;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: return 2;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: return 4;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP: return HIWORD(wparam)==XBUTTON1?8:16;
    default: return 0;
    }
}
bool mouse_button_up(UINT message) {
    return message==WM_LBUTTONUP || message==WM_RBUTTONUP || message==WM_MBUTTONUP || message==WM_XBUTTONUP;
}
void update_cursor(bool open) {
    if(!cover_device || !ImGui::GetCurrentContext()) return;
    if(!open && !cursor_borrowed) return;
    auto& io=ImGui::GetIO(); io.MouseDrawCursor=open;
    DWORD foreground_pid{}; GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    const bool foreground=foreground_pid==GetCurrentProcessId();
    if(open) {
        const BOOL previously_visible=cover_device->ShowCursor(FALSE);
        if(!cursor_borrowed) {
            native_cursor_visible=previously_visible; native_os_cursor=GetCursor(); cursor_borrowed=true;
        } else if(previously_visible) native_cursor_visible=TRUE;
        POINT point{};
        bool inside=foreground && GetCursorPos(&point) && ScreenToClient(game_window,&point) &&
            point.x>=0 && point.y>=0 && point.x<client_width && point.y<client_height;
        if(inside) { if(GetCursor()!=nullptr) SetCursor(nullptr); os_cursor_hidden=true; }
        else if(os_cursor_hidden) {
            if(foreground && GetCursor()==nullptr) SetCursor(native_os_cursor);
            os_cursor_hidden=false;
        }
    } else if(cursor_borrowed) {
        cover_device->ShowCursor(native_cursor_visible);
        if(os_cursor_hidden && foreground && GetCursor()==nullptr) SetCursor(native_os_cursor);
        cursor_borrowed=os_cursor_hidden=false;
        { std::lock_guard lock(motion_mutex); motion.pending=false; }
        io.ClearEventsQueue(); io.ClearInputKeys(); io.ClearInputMouse();
    }
}
void release_cover_texture() {
    if (cover_texture) { cover_texture->Release(); cover_texture=nullptr; }
    cover_attempted=false;
}
void clear_cover() { release_cover_texture(); cover_info.reset(); cover_pixels={}; }
IDirect3DTexture9* song_cover(const std::shared_ptr<const SongInfo>& info) {
    if (!cover_device) return nullptr;
    if (cover_info!=info) {
        clear_cover(); cover_info=info;
        if (info) cover_pixels=load_jacket(info->jacket);
    }
    if (cover_attempted || cover_pixels.bgra.empty()) return cover_texture;
    cover_attempted=true;
    const auto& pixels=cover_pixels;
    IDirect3DTexture9* staging{};
    if (FAILED(cover_device->CreateTexture(pixels.width,pixels.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr))) return nullptr;
    D3DLOCKED_RECT locked{};
    if (SUCCEEDED(staging->LockRect(0,&locked,nullptr,0))) {
        for (unsigned row=0;row<pixels.height;++row)
            std::memcpy(static_cast<char*>(locked.pBits)+row*locked.Pitch,pixels.bgra.data()+row*pixels.width*4,pixels.width*4);
        staging->UnlockRect(0);
        if (SUCCEEDED(cover_device->CreateTexture(pixels.width,pixels.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&cover_texture,nullptr)) &&
            FAILED(cover_device->UpdateTexture(staging,cover_texture))) release_cover_texture();
    }
    staging->Release(); cover_attempted=true;
    return cover_texture;
}
Session* bound_session{};
using Present=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using PresentEx=HRESULT(WINAPI*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using SwapPresent=HRESULT(WINAPI*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using Reset=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using ResetEx=HRESULT(WINAPI*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
Present original_present{};
PresentEx original_present_ex{};
SwapPresent original_swap_present{};
Reset original_reset{};
ResetEx original_reset_ex{};
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    bool consumed=false;
    input_imgui(window,message,wparam,lparam,consumed);
    return consumed?1:CallWindowProcW(old_proc,window,message,wparam,lparam);
}
// Render target/depth bindings are NOT part of D3DSBT_ALL. ImGui's DX9 backend
// already preserves the ordinary draw state; keep only our additional changes here.
class PresentationTarget {
    using Surface=Microsoft::WRL::ComPtr<IDirect3DSurface9>;
    IDirect3DDevice9* device_;
    std::array<Surface,4> targets_;
    Surface depth_;
    D3DVIEWPORT9 viewport_{};
    RECT scissor_{};
    DWORD srgb_{},write_{},sampler_srgb_{};
    UINT frequency_{1};
    unsigned count_{};
    bool captured_{};
public:
    explicit PresentationTarget(IDirect3DDevice9* device):device_(device){}
    bool bind(IDirect3DSurface9* back) {
        D3DCAPS9 caps{};
        if (FAILED(device_->GetDeviceCaps(&caps)) || FAILED(device_->GetViewport(&viewport_)) ||
            FAILED(device_->GetScissorRect(&scissor_))) return false;
        count_=static_cast<unsigned>(std::clamp<DWORD>(caps.NumSimultaneousRTs,1,4));
        for (unsigned i=0;i<count_;++i) {
            const auto hr=device_->GetRenderTarget(i,&targets_[i]);
            if (FAILED(hr) && (i==0 || hr!=D3DERR_NOTFOUND)) return false;
        }
        const auto depth=device_->GetDepthStencilSurface(&depth_);
        if (FAILED(depth) && depth!=D3DERR_NOTFOUND) return false;
        if (FAILED(device_->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb_)) ||
            FAILED(device_->GetRenderState(D3DRS_COLORWRITEENABLE,&write_)) ||
            FAILED(device_->GetStreamSourceFreq(0,&frequency_)) ||
            FAILED(device_->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&sampler_srgb_))) return false;
        captured_=true;
        for (unsigned i=1;i<count_;++i) if (FAILED(device_->SetRenderTarget(i,nullptr))) return false;
        if (FAILED(device_->SetDepthStencilSurface(nullptr)) || FAILED(device_->SetRenderTarget(0,back))) return false;
        device_->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
        device_->SetStreamSourceFreq(0,1);
        device_->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        return true;
    }
    ~PresentationTarget() {
        if (!captured_) return;
        device_->SetRenderTarget(0,targets_[0].Get());
        for (unsigned i=1;i<count_;++i) device_->SetRenderTarget(i,targets_[i].Get());
        device_->SetDepthStencilSurface(depth_.Get());
        device_->SetViewport(&viewport_);
        device_->SetScissorRect(&scissor_);
        device_->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb_);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE,write_);
        device_->SetStreamSourceFreq(0,frequency_);
        device_->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,sampler_srgb_);
    }
};
void present_overlay(IDirect3DDevice9* device,HWND destination) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDirect3DSurface9> back;
    if (FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))) return;
    if (!game_device) {
        D3DDEVICE_CREATION_PARAMETERS params{};
        D3DSURFACE_DESC desc{};
        ComPtr<IDirect3DSwapChain9> chain;
        D3DPRESENT_PARAMETERS pp{};
        HWND output=destination;
        if(!output && SUCCEEDED(device->GetSwapChain(0,&chain)) && SUCCEEDED(chain->GetPresentParameters(&pp))) output=pp.hDeviceWindow;
        if(!output && SUCCEEDED(device->GetCreationParameters(&params))) output=params.hFocusWindow;
        if (SUCCEEDED(back->GetDesc(&desc)) &&
            desc.Width>=640 && desc.Height>=480 && IsWindow(output)) {
            DWORD pid{}; GetWindowThreadProcessId(output,&pid);
            if (pid==GetCurrentProcessId() && init_imgui(output,device)) {
                game_window=output;
                SetLastError(0);
                old_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));
                if (!old_proc && GetLastError()!=0) {
                    shutdown_imgui();
                    log("Could not attach overlay input handler");
                } else { game_device=device; log("ImGui attached to primary D3D9 Present; offscreen EndScene passes are not drawn"); }
            }
        }
    }
    if (device==game_device && bound_session && (!destination || destination==game_window)) {
        // Poll the configured chord even when a launcher consumes keyboard messages.
        { std::lock_guard lock(imgui_mutex); poll_overlay_hotkey(); }
        if (!visible.load() && !(preferences().snapshot().always_show_judgment_counter && bound_session->snapshot().playing)) {
            std::lock_guard lock(imgui_mutex);
            if(cursor_borrowed) { WindowDpiScope dpi(game_window); update_cursor(false); }
            return;
        }
        if (FAILED(device->BeginScene())) return;
        {
            PresentationTarget target(device);
            if (target.bind(back.Get())) frame_imgui(*bound_session);
        }
        device->EndScene();
    }
}
thread_local unsigned presentation_depth{};
struct PresentationCall { PresentationCall(){++presentation_depth;} ~PresentationCall(){--presentation_depth;} };
HRESULT WINAPI present(IDirect3DDevice9* device,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty) {
    PresentationCall call;
    if (presentation_depth==1) present_overlay(device,window);
    return original_present(device,source,dest,window,dirty);
}
HRESULT WINAPI present_ex(IDirect3DDevice9Ex* device,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty,DWORD flags) {
    PresentationCall call;
    if (presentation_depth==1) present_overlay(device,window);
    return original_present_ex(device,source,dest,window,dirty,flags);
}
HRESULT WINAPI swap_present(IDirect3DSwapChain9* chain,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty,DWORD flags) {
    PresentationCall call;
    if (presentation_depth==1) {
        Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
        Microsoft::WRL::ComPtr<IDirect3DSwapChain9> primary;
        if (SUCCEEDED(chain->GetDevice(&device)) && SUCCEEDED(device->GetSwapChain(0,&primary)) && primary.Get()==chain)
            present_overlay(device.Get(),window);
    }
    return original_swap_present(chain,source,dest,window,dirty,flags);
}
HRESULT WINAPI reset(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* params) {
    if (device==game_device) invalidate_imgui_device_objects();
    const auto hr=original_reset(device,params);
    if (device==game_device && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}
HRESULT WINAPI reset_ex(IDirect3DDevice9Ex* device,D3DPRESENT_PARAMETERS* params,D3DDISPLAYMODEEX* mode) {
    if (device==game_device) invalidate_imgui_device_objects();
    const auto hr=original_reset_ex(device,params,mode);
    if (device==game_device && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}
}
void set_overlay_status(std::string status,bool failed) {
    std::lock_guard lock(status_mutex); status_text=std::move(status); status_failed=failed;
}
void show_overlay(bool value) { visible=value; if(!value) capturing_hotkey=false; }
bool init_imgui(HWND window,IDirect3DDevice9* device) {
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
    ImFont* ui_font{};
    // Both languages use the same primary face. Merge installed glyphs only
    // for characters absent from YaHei UI, such as uncommon Japanese titles.
    wchar_t windows_path[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows_path,MAX_PATH)) {
        static ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        static constexpr ImWchar symbols[]{0x0100,0x052f,0x2000,0x27ff,0,};
        builder.AddRanges(symbols); builder.BuildRanges(&ranges);
        // Microsoft YaHei UI is face 1 of the Windows msyh.ttc collection.
        const auto primary=std::filesystem::path(windows_path)/L"Fonts"/L"msyh.ttc";
        if(GetFileAttributesW(primary.c_str())!=INVALID_FILE_ATTRIBUTES) {
            const auto encoded=primary.u8string();
            ImFontConfig config; config.FontNo=1; config.PixelSnapH=true;
            strcpy_s(config.Name,"Microsoft YaHei UI");
            ui_font=io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(encoded.c_str()),15.f,&config,ranges.Data);
        }
        if(!ui_font) {
            log("Microsoft YaHei UI is unavailable; using the default font fallback");
            ui_font=io.Fonts->AddFontDefault();
        }
        for (const wchar_t* name:{L"meiryo.ttc",L"msgothic.ttc",L"msyh.ttc"}) {
            const auto path=std::filesystem::path(windows_path)/L"Fonts"/name;
            if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) continue;
            const auto encoded=path.u8string();
            ImFontConfig merge; merge.MergeMode=true; merge.PixelSnapH=true;
            if(io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(encoded.c_str()),15.f,&merge,ranges.Data)) break;
        }
    }
    if(!ui_font) ui_font=io.Fonts->AddFontDefault();
    io.FontDefault=ui_font;
    ImGui::GetStyle().FontSizeBase=15.f;
    apply_theme();
    if (!ImGui_ImplWin32_Init(window)) { ImGui::DestroyContext(); return false; }
    if (!ImGui_ImplDX9_Init(device)) { ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); return false; }
    game_window=window;
    clear_cover(); cover_device=device;
    return true;
}
void invalidate_imgui_device_objects() {
    release_cover_texture();
    ImGui_ImplDX9_InvalidateDeviceObjects();
}
void shutdown_imgui() {
    std::lock_guard lock(imgui_mutex);
    update_cursor(false);
    clear_cover(); cover_device=nullptr;
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
}
namespace {
std::string transport_time(float seconds) {
    const double whole=std::isfinite(seconds)?std::floor(std::max(0.,static_cast<double>(seconds))):0.;
    char text[64]{};
    sprintf_s(text,"%.0f:%02u",std::floor(whole/60.),static_cast<unsigned>(std::fmod(whole,60.)));
    return text;
}
enum class TransportIcon { Rewind, Pause, Play, Loop, LoopOnce };
bool transport_button(const char* id,const char* tooltip,TransportIcon icon,bool selected) {
    const float side=ImGui::GetFrameHeight();
    const bool pressed=state_button(id,{side,side},selected);
    const auto min=ImGui::GetItemRectMin();
    const auto max=ImGui::GetItemRectMax();
    const ImVec2 center{(min.x+max.x)*.5f,(min.y+max.y)*.5f};
    const float radius=side*.22f;
    auto* draw=ImGui::GetWindowDrawList();
    const bool looping=icon==TransportIcon::Loop || icon==TransportIcon::LoopOnce;
    const auto color=icon==TransportIcon::LoopOnce?ImGui::GetColorU32(ImVec4{1.f,.78f,.43f,1.f}):
        looping && !selected?ImGui::GetColorU32(ImVec4{1.f,1.f,1.f,1.f}):
        ImGui::GetColorU32(selected?ImGuiCol_CheckMark:ImGuiCol_Text);
    if(looping) {
        // Draw at the current button size; no font glyphs or texture resources.
        const float x=side*.28f,y=side*.24f,arrow=side*.14f;
        draw->AddLine({center.x-x,center.y-1},{center.x-x,center.y-y},color,1.2f);
        draw->AddLine({center.x-x,center.y-y},{center.x+x,center.y-y},color,1.2f);
        draw->AddTriangleFilled({center.x+x,center.y-y},{center.x+x-arrow,center.y-y-arrow},
            {center.x+x-arrow,center.y-y+arrow},color);
        draw->AddLine({center.x+x,center.y+1},{center.x+x,center.y+y},color,1.2f);
        draw->AddLine({center.x+x,center.y+y},{center.x-x,center.y+y},color,1.2f);
        draw->AddTriangleFilled({center.x-x,center.y+y},{center.x-x+arrow,center.y+y-arrow},
            {center.x-x+arrow,center.y+y+arrow},color);
        if(icon==TransportIcon::LoopOnce) {
            const float height=side*.14f;
            draw->AddLine({center.x,center.y-height},{center.x,center.y+height},color,1.2f);
            draw->AddLine({center.x-1.5f,center.y-height+1.5f},{center.x,center.y-height},color,1.2f);
            draw->AddLine({center.x-1.5f,center.y+height},{center.x+1.5f,center.y+height},color,1.2f);
        }
    } else if (icon==TransportIcon::Pause) {
        const float half=radius*.35f;
        draw->AddRectFilled({center.x-radius,center.y-radius},{center.x-half,center.y+radius},color);
        draw->AddRectFilled({center.x+half,center.y-radius},{center.x+radius,center.y+radius},color);
    } else {
        const float direction=icon==TransportIcon::Play?1.f:-1.f;
        draw->AddTriangleFilled({center.x+direction*radius,center.y},
            {center.x-direction*radius,center.y-radius},{center.x-direction*radius,center.y+radius},color);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",i18n::text(tooltip));
    return pressed;
}
void draw_song_info(const Track& track) {
    const auto info=track.info;
    const float side=std::clamp(ImGui::GetContentRegionAvail().x*.27f,100.f,108.f);
    ImGui::BeginGroup();
    const auto start=ImGui::GetCursorScreenPos();
    if (const auto texture=song_cover(info)) {
        // Preserve aspect ratio for unusual jackets inside the square slot.
        const float scale=side/static_cast<float>(std::max(cover_pixels.width,cover_pixels.height));
        const ImVec2 size{cover_pixels.width*scale,cover_pixels.height*scale};
        ImGui::Dummy({side,side});
        const ImVec2 top{start.x+(side-size.x)*.5f,start.y+(side-size.y)*.5f};
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture),top,{top.x+size.x,top.y+size.y});
    } else {
        ImGui::Dummy({side,side});
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(start,{start.x+side,start.y+side},ImGui::GetColorU32(ImGuiCol_FrameBg));
        const char* text=i18n::text("NO COVER"); const auto size=ImGui::CalcTextSize(text);
        draw->AddText({start.x+(side-size.x)*.5f,start.y+(side-size.y)*.5f},ImGui::GetColorU32(ImGuiCol_TextDisabled),text);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float available=ImGui::GetContentRegionAvail().x;
    if (ImGui::BeginTable("song-details",2,ImGuiTableFlags_SizingFixedFit, {available,0})) {
        ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthFixed,std::max(ImGui::CalcTextSize(i18n::text("Difficulty")).x,ImGui::CalcTextSize(i18n::text("Music ID")).x));
        ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);
        const auto row=[](const char* label,const std::string& text) {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextDisabled("%s",i18n::text(label)); ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(text.empty()?"--":text.c_str()); ImGui::PopTextWrapPos();
        };
        row("Music ID",std::to_string(track.music_id));
        row("Title",info?info->title:"");
        row("Artist",info?info->artist:"");
        row("Difficulty",info?info->difficulty:difficulty_name(track.difficulty));
        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s",track.chart.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Separator();
}
void draw_options(Session& session,bool focus=false) {
    static std::string save_error;
    if(!visible || !options_open) { capturing_hotkey=false; return; }
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*.5f,display.y*.5f},ImGuiCond_FirstUseEver,{.5f,.5f});
    ImGui::SetNextWindowSize({360,0},ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({360,0},{360,10000});
    ImGui::SetNextWindowBgAlpha(preferences().snapshot().background_opacity);
    if(focus) ImGui::SetNextWindowFocus();
    if(!ImGui::Begin(i18n::label("Option"),&options_open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse)) {
        capturing_hotkey=false; ImGui::End(); return;
    }
    if(capturing_hotkey) {
        DWORD foreground_pid{}; GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
        if(foreground_pid!=GetCurrentProcessId() || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) capturing_hotkey=false;
    }
    auto value=preferences().snapshot();
    bool changed=false;
    section("Interface");
    ImGui::TextUnformatted(i18n::text("Language"));
    ImGui::SetNextItemWidth(-1);
    const char* languages[]{"English","中文"};
    if(ImGui::BeginCombo("##language",languages[static_cast<int>(value.language)])) {
        for(int index=0;index<2;++index) {
            const bool selected=static_cast<int>(value.language)==index;
            if(ImGui::Selectable(languages[index],selected)) {
                value.language=static_cast<i18n::Language>(index); changed=true;
            }
            if(selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(i18n::text("Background opacity"));
    ImGui::SetNextItemWidth(-1);
    float opacity_percent=value.background_opacity*100.f;
    if(ImGui::SliderFloat("##background-opacity",&opacity_percent,0.f,100.f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) {
        value.background_opacity=opacity_percent*.01f; changed=true;
    }

    section("Judgment counter");
    changed|=ImGui::Checkbox(i18n::label("Always show judgment counter"),&value.always_show_judgment_counter);

    section("Keyboard shortcut");
    ImGui::TextUnformatted(i18n::text("Overlay hotkey"));
    const auto label=capturing_hotkey?std::string(i18n::text("Press a key... (Esc cancels)")):hotkey_name(value.hotkey);
    if(ImGui::Button((label+"###overlay-hotkey").c_str(),{-1,0})) {
        captured_hotkey=0; capturing_hotkey=true;
        for(unsigned i=0;i<capture_keys.size();++i) capture_keys[i]=(GetAsyncKeyState(static_cast<int>(i))&0x8000)!=0;
    }
    ImGui::TextDisabled("%s",i18n::text("Ctrl / Alt / Shift combinations supported."));

    section("Chart View");
    ImGui::TextUnformatted(i18n::text("Default measures per column"));
    ImGui::SetNextItemWidth(-1);
    changed|=ImGui::SliderInt("##default-column-measures",&value.chart_column_measures,1,12,i18n::text("%d measures / column"),ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::Checkbox(i18n::label("Follow playback"),&value.follow_playback);

    section("Loop buffer");
    if(ImGui::BeginTable("##loop-buffers",2,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(i18n::text("Start buffer (s)"));
        ImGui::SetNextItemWidth(-1);
        changed|=ImGui::InputFloat("##start-buffer",&value.loop_before,.5f,5.f,"%.2f");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(i18n::text("End buffer (s)"));
        ImGui::SetNextItemWidth(-1);
        changed|=ImGui::InputFloat("##end-buffer",&value.loop_after,.5f,5.f,"%.2f");
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s",i18n::text("0 = no buffer. Clamped to the track."));
    if(capturing_hotkey) {
        const auto captured=captured_hotkey.exchange(0);
        if(captured==VK_ESCAPE) capturing_hotkey=false;
        else if(captured) {
            value.hotkey={captured&255,(captured&256)!=0,(captured&512)!=0,(captured&1024)!=0};
            capturing_hotkey=false; wait_hotkey_release=true; changed=true;
        }
    }
    if(capturing_hotkey) {
        for(unsigned key=VK_BACK;key<256;++key) {
            const bool down=(GetAsyncKeyState(static_cast<int>(key))&0x8000)!=0;
            const bool pressed=down && !capture_keys[key]; capture_keys[key]=down;
            if(!pressed) continue;
            if(key==VK_ESCAPE) { capturing_hotkey=false; break; }
            if(!valid_overlay_key(key)) continue;
            value.hotkey={key,(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,
                (GetAsyncKeyState(VK_MENU)&0x8000)!=0,(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0};
            capturing_hotkey=false; wait_hotkey_release=true; changed=true; break;
        }
    }
    if(changed && preferences().save(value,save_error)) session.set_loop_buffers(value.loop_before,value.loop_after);
    if(!save_error.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored({1,.35f,.25f,1},"%s",i18n::message(save_error).c_str());
        ImGui::PopTextWrapPos();
    }
    if(ImGui::Button(i18n::label("Close"))) options_open=false;
    if(!options_open) capturing_hotkey=false;
    ImGui::End();
}
void draw_about() {
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(720.f,display.x-56.f),std::min(540.f,display.y-64.f)},ImGuiCond_Appearing);
    bool open=true;
    ImGui::SetNextWindowBgAlpha(preferences().snapshot().background_opacity);
    if (!ImGui::BeginPopupModal(i18n::label("About Training Master"),&open,ImGuiWindowFlags_NoResize)) return;
    ImGui::TextUnformatted(i18n::text("Training Master"));
    ImGui::Text(i18n::text("Version %s"),about::version);
    ImGui::TextUnformatted("Build by Takanashi Ryo");
    section("Third-party Licenses");
    static int selected{};
    bool changed=false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##license-choice",about::licenses[selected].name)) {
        for (int i=0;i<static_cast<int>(about::licenses.size());++i) {
            if (ImGui::Selectable(about::licenses[i].name,selected==i)) { selected=i; changed=true; }
            if (selected==i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginChild("license-text",{0,-ImGui::GetFrameHeightWithSpacing()},ImGuiChildFlags_Borders)) {
        if (changed) ImGui::SetScrollY(0);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(about::licenses[selected].text);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    if (ImGui::Button(i18n::label("Close"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
void draw_controls(Session& session,const State& s) {
    auto send=[&](Action action,float value=0.f){session.submit({s.generation,action,value});};
    draw_song_info(s.track);
    bool enabled=s.enabled;
    ImGui::BeginDisabled(!s.ready || s.enabled || s.finish_requested);
    if (ImGui::Checkbox(i18n::label("Enable training for this track"),&enabled)) send(enabled?Action::Enable:Action::Disable);
    ImGui::EndDisabled();
    if(s.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",i18n::text("Use Finish Immediately to exit training."));
    if (!s.ready) ImGui::TextWrapped("%s",i18n::text("Unavailable: unsupported mode or missing native context. Load plugin before selecting a song. Check log."));
    if (s.message!="Training is off" &&
        s.message!="Practice track: results and upload disabled" &&
        s.message!="Training off; this track still cannot save results" &&
        s.message!="AutoPlay enabled; results and upload disabled" &&
        s.message!="Practice finished; result collection remains blocked")
        ImGui::TextWrapped("%s",i18n::text(s.message.c_str()));
    const float control_spacing=ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{control_spacing,control_spacing});
    section("Transport");
    ImGui::BeginDisabled(!s.enabled || s.rewinding);
    // Keep the local drag value while dragging; never overwrite it with snapshots.
    static std::uint64_t editing_generation{};
    static float seek_value{}, rate_value{1};
    static bool seek_active{};
    if (editing_generation!=s.generation) { editing_generation=s.generation; seek_value=s.position; rate_value=s.rate; seek_active=false; }
    if (!seek_active) seek_value=s.position;
    if (!ImGui::IsAnyItemActive()) rate_value=s.rate;
    ImGui::Text("%s / %s",transport_time(seek_value).c_str(),transport_time(s.track.duration).c_str());
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(40.f,ImGui::GetContentRegionAvail().x-4*(ImGui::GetFrameHeight()+gap)));
    // Display minutes:seconds; native seek/loop units remain seconds. No raw-seconds text entry.
    if (ImGui::SliderFloat("##position",&seek_value,0.f,s.track.duration,"",ImGuiSliderFlags_NoInput)) send(Action::Seek,seek_value);
    seek_active=ImGui::IsItemActive();
    ImGui::SameLine();
    const auto loop_tooltip=!s.loop?"Loop off (click: repeat selection)":s.repeat?
        "Repeat selection (click: loop once)":"Loop once, then pause (click: off)";
    if(transport_button("##transport-loop",loop_tooltip,s.loop && !s.repeat?TransportIcon::LoopOnce:TransportIcon::Loop,s.loop))
        send(Action::CycleLoop);
    ImGui::SameLine();
    const bool reverse_ready=s.track.reverse_audio && s.track.reverse_audio->clip.load()!=nullptr;
    ImGui::BeginDisabled(!reverse_ready);
    if (transport_button("##transport-rewind","Reverse playback",TransportIcon::Rewind,s.reversing && !s.paused)) send(Action::Reverse);
    ImGui::EndDisabled();
    if (!reverse_ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const auto asset=s.track.reverse_audio;
        if (asset && !asset->finished.load(std::memory_order_acquire)) ImGui::SetTooltip("%s",i18n::text("Preparing reverse audio..."));
        else ImGui::SetTooltip(i18n::text("Reverse audio unavailable%s%s"),asset?": ":"",asset?i18n::text(asset->error.c_str()):"");
    }
    ImGui::SameLine();
    if (transport_button("##transport-pause","Pause",TransportIcon::Pause,s.paused)) send(Action::Pause);
    ImGui::SameLine();
    if (transport_button("##transport-play","Play forward",TransportIcon::Play,!s.paused && !s.reversing)) send(Action::Resume);
    section("Playback rate");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##rate",&rate_value,min_playback_rate,max_playback_rate,"%.2fx")) send(Action::SetRate,rate_value);
    const float rate_width=std::max(40.f,(ImGui::GetContentRegionAvail().x-3*ImGui::GetStyle().ItemSpacing.x)/4);
    for (const float rate:{.5f,.75f,1.f,1.5f}) {
        char label[16]{}; sprintf_s(label,"%.2fx",rate);
        if (state_button(label,{rate_width,0},std::abs(rate_value-rate)<.001f)) { rate_value=rate; send(Action::SetRate,rate); }
        if (rate<1.5f) ImGui::SameLine();
    }
    ImGui::EndDisabled();
    section("Setting");
    ImGui::BeginDisabled(!s.enabled);
    bool automatic=s.autoplay;
    ImGui::BeginDisabled(!s.ready || !s.track.autoplay_ready);
    if (ImGui::Checkbox(i18n::label("AutoPlay"),&automatic)) send(Action::SetAutoplay,automatic?1.f:0.f);
    ImGui::EndDisabled();
    bool lane_speed_locked=s.lane_speed_locked;
    ImGui::BeginDisabled(!s.ready || !s.track.lane_speed_ready);
    if (ImGui::Checkbox(i18n::label("Fix Lane Speed"),&lane_speed_locked)) send(Action::SetLaneSpeedLocked,lane_speed_locked?1.f:0.f);
    ImGui::EndDisabled();
    bool camera_locked=s.camera_locked;
    ImGui::BeginDisabled(!s.ready || !s.track.camera_ready);
    if (ImGui::Checkbox(i18n::label("Block camera movement"),&camera_locked)) send(Action::SetCameraLocked,camera_locked?1.f:0.f);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!s.ready || !s.track.gauge_ready);
    bool gauge_enabled=s.gauge.enabled;
    if(ImGui::Checkbox(i18n::label("Gauge###gauge-enabled"),&gauge_enabled)) send(Action::SetGaugeEnabled,gauge_enabled?1.f:0.f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",i18n::text("Override gauge"));
    ImGui::SameLine(); ImGui::BeginDisabled(!s.gauge.enabled);
    ImGui::SetNextItemWidth(-1.f);
    int gauge_type=static_cast<int>(s.gauge.type);
    if(ImGui::Combo("##gauge-type",&gauge_type,gauge_names.data(),static_cast<int>(gauge_names.size())))
        send(Action::SetGaugeType,static_cast<float>(gauge_type));
    float initial_gauge=s.gauge.initial;
    ImGui::SetNextItemWidth(-1.f);
    if(ImGui::SliderFloat("##gauge-initial",&initial_gauge,0.f,100.f,i18n::text("Initial Gauge: %.1f%%")))
        send(Action::SetGaugeInitial,initial_gauge);
    ImGui::EndDisabled(); ImGui::EndDisabled();
    if (!s.track.autoplay_ready) ImGui::TextDisabled("%s",i18n::text("Native AutoPlay is unavailable."));
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
}
void draw_finish(Session& session,const State& s) {
    auto send=[&](Action action){session.submit({s.generation,action});};
    ImGui::Separator();
    ImGui::BeginDisabled(!s.tainted || !s.playing);
    ImGui::PushStyleColor(ImGuiCol_Button,{.28f,.16f,.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.43f,.22f,.25f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.54f,.25f,.28f,1.f});
    ImGui::PushStyleColor(ImGuiCol_Text,{1.f,.77f,.76f,1.f});
    if (ImGui::Button(i18n::label("Finish Immediately"),{-1,0})) send(Action::Finish);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
}
}
void draw_panel(Session& session) {
    if (!visible) return;
    const auto options=preferences().snapshot();
    i18n::set_language(options.language);
    session.set_loop_buffers(options.loop_before,options.loop_after);
    const auto s=session.snapshot();
    const auto display=ImGui::GetIO().DisplaySize;
    const auto& style=ImGui::GetStyle();
    const float initial_top=16.f+2*(ImGui::GetTextLineHeight()+2*style.CellPadding.y)+
        ImGui::GetTextLineHeightWithSpacing()+2*style.WindowPadding.y+style.ItemSpacing.y+8.f;
    ImGui::SetNextWindowSize({std::min(1180.f,display.x-32.f),std::max(400.f,std::min(680.f,display.y-initial_top-16.f))},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({16,initial_top},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({780,400},{10000,10000});
    bool open=visible.load();
    ImGui::SetNextWindowBgAlpha(preferences().snapshot().background_opacity);
    const bool expanded=ImGui::Begin(i18n::label("Training Master"),&open,ImGuiWindowFlags_MenuBar);
    if (!open) { visible=false; capturing_hotkey=false; }
    if (!expanded) { ImGui::End(); draw_options(session); return; }
    bool show_about=false,focus_options=false;
    if (ImGui::BeginMenuBar()) {
        if(ImGui::MenuItem(i18n::label("Option"))) { options_open=true; focus_options=true; }
        show_about=ImGui::MenuItem(i18n::label("About"));
        ImGui::EndMenuBar();
    }
    if (show_about) ImGui::OpenPopup(i18n::label("About Training Master"));
    draw_about();
    bool failed;
    { std::lock_guard lock(status_mutex); failed=status_failed; }
    if (failed || !s.playing) {
        clear_cover();
        ImGui::TextColored(failed?ImVec4{1,.2f,.2f,1}:ImVec4{1,1,0,1},"%s",
                           i18n::text(failed?"Hook entry not found!":"Choose a track to start training."));
        ImGui::End(); draw_options(session,focus_options); return;
    }
    const float left=std::min(380.f,ImGui::GetContentRegionAvail().x*.46f);
    ImGui::BeginChild("training-sidebar",{left,0},ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    // Reserve a footer independently of metadata length or window height.
    const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y+1.f;
    ImGui::BeginChild("training-controls",{0,-footer});
    draw_controls(session,s);
    ImGui::EndChild();
    draw_finish(session,s);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("training-chart",{0,0});
    draw_chart_preview(s,&session,preferences().snapshot().background_opacity);
    ImGui::EndChild();
    ImGui::End();
    draw_options(session,focus_options);
}
void draw_judgment_counter(const State& state) {
    if(!state.playing || (!visible.load() && !preferences().snapshot().always_show_judgment_counter)) return;
    const auto stats=judgment::snapshot();
    const auto display=ImGui::GetIO().DisplaySize;
    const float column=std::max(62.f,ImGui::CalcTextSize("S-CRITICAL").x+2*ImGui::GetStyle().CellPadding.x);
    ImGui::SetNextWindowPos({display.x*.5f,16.f},ImGuiCond_FirstUseEver,{.5f,0.f});
    ImGui::SetNextWindowSize({column*7+ImGui::GetStyle().WindowPadding.x*2,0},ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(preferences().snapshot().background_opacity);
    const auto flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_AlwaysAutoResize|
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoCollapse|(!visible.load()?ImGuiWindowFlags_NoInputs:0);
    if(ImGui::Begin("##judgment-counter",nullptr,flags)) {
        if(!stats.available) ImGui::TextDisabled("%s",i18n::text("Judgment counter unavailable"));
        else {
            const char* full_labels[]{"ERROR","NEAR","CRITICAL","S-CRITICAL","CRITICAL","NEAR","ERROR"};
            const char* standard_labels[]{"ERROR","NEAR","CRITICAL","NEAR","ERROR"};
            const auto* labels=stats.s_critical_enabled?full_labels:standard_labels;
            const int columns=stats.s_critical_enabled?7:5;
            const std::uint64_t standard_counts[]{stats.counts[0],stats.counts[1],
                stats.counts[2]+stats.counts[3]+stats.counts[4],stats.counts[5],stats.counts[6]};
            const auto* counts=stats.s_critical_enabled?stats.counts.data():standard_counts;
            if(ImGui::BeginTable("##judgments",columns,ImGuiTableFlags_Borders|ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextRow();
                for(int i=0;i<columns;++i) {
                    ImGui::TableNextColumn();
                    char count[32]; sprintf_s(count,"%llu",static_cast<unsigned long long>(counts[i]));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,(ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(count).x)*.5f));
                    ImGui::TextUnformatted(count);
                }
                ImGui::TableNextRow();
                for(int i=0;i<columns;++i) {
                    ImGui::TableNextColumn();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.f,(ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(labels[i]).x)*.5f));
                    ImGui::TextUnformatted(labels[i]);
                }
                ImGui::EndTable();
            }
            const auto available=ImGui::GetContentRegionAvail().x;
            constexpr ImVec4 red{1.f,.42f,.58f,1.f},blue{.33f,.70f,1.f,1.f};
            const auto early=stats.colors_swapped?blue:red,late=stats.colors_swapped?red:blue;
            const auto average_color=stats.average_ms<0?early:stats.average_ms>0?late:ImGui::GetStyleColorVec4(ImGuiCol_Text);
            char avg[64],signed_average[32],magnitude[32];
            sprintf_s(avg,"Avg. %+.1f ± %.1f ms",stats.average_ms,stats.absolute_average_ms);
            sprintf_s(signed_average,"%+.1f",stats.average_ms);
            sprintf_s(magnitude," ± %.1f ms",stats.absolute_average_ms);
            ImGui::TextColored(early,"(-) EARLY");
            ImGui::SameLine(std::max(0.f,(available-ImGui::CalcTextSize(avg).x)*.5f));
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Avg. "); ImGui::SameLine(0,0);
            ImGui::TextColored(average_color,"%s",signed_average); ImGui::SameLine(0,0);
            ImGui::TextUnformatted(magnitude);
            ImGui::EndGroup();
            ImGui::SameLine(std::max(0.f,available-ImGui::CalcTextSize("LATE (+)").x));
            ImGui::TextColored(late,"LATE (+)");
        }
    }
    ImGui::End();
}
void frame_imgui(Session& session) {
    std::lock_guard lock(imgui_mutex);
    poll_overlay_hotkey();
    WindowDpiScope dpi(game_window);
    if(!update_geometry()) return;
    update_cursor(visible.load());
    flush_mouse_motion();
    ImGui_ImplDX9_NewFrame(); ImGui_ImplWin32_NewFrame();
    auto& io=ImGui::GetIO();
    io.DisplaySize={canvas_width,canvas_height}; io.DisplayFramebufferScale={1,1};
    DWORD foreground_pid{}; GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    if(foreground_pid==GetCurrentProcessId()) {
        POINT point{};
        if(GetCursorPos(&point) && ScreenToClient(game_window,&point)) map_mouse_position(static_cast<float>(point.x),static_cast<float>(point.y));
    }
    ImGui::NewFrame();
    draw_panel(session);
    i18n::set_language(preferences().snapshot().language);
    draw_judgment_counter(session.snapshot());
    ImGui::EndFrame(); ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    if(!visible.load()) update_cursor(false);
}
LRESULT input_imgui(HWND window,UINT message,WPARAM wparam,LPARAM lparam,bool& consumed) {
    consumed=false;
    if (!ImGui::GetCurrentContext()) return 0;
    const bool open=visible.load();
    const bool key_message=message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP;
    const bool key_down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
    if(key_message && capturing_hotkey) {
        if(key_down && !(lparam&(1LL<<30)) && (wparam==VK_ESCAPE || valid_overlay_key(static_cast<unsigned>(wparam)))) {
            unsigned captured=static_cast<unsigned>(wparam);
            if(wparam!=VK_ESCAPE) captured|=((GetKeyState(VK_CONTROL)&0x8000)?256u:0u)|
                ((GetKeyState(VK_MENU)&0x8000)?512u:0u)|((GetKeyState(VK_SHIFT)&0x8000)?1024u:0u);
            captured_hotkey=captured;
        }
        consumed=true; return 0;
    }
    bool toggle_key=false;
    if(key_message && !capturing_hotkey) {
        const auto hotkey=preferences().snapshot().hotkey;
        const bool configured_key=wparam==hotkey.key;
        toggle_key=configured_key && modifiers_match(hotkey,true);
        if(configured_key && (!key_down || !(lparam&(1LL<<30)))) {
            std::lock_guard lock(hotkey_events_mutex);
            hotkey_events.push_back({static_cast<DWORD>(GetMessageTime()),key_down,toggle_key});
        }
    }
    if(!open && toggle_key) { consumed=true; return 0; }
    if(open && message==WM_SETCURSOR && LOWORD(lparam)==HTCLIENT) {
        consumed=true; return TRUE; // Keep native SetCursor handlers from resurrecting a second cursor.
    }
    if(open && message==WM_INPUT) {
        RAWINPUTHEADER header{}; UINT size=sizeof(header);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_HEADER,&header,&size,sizeof(header))==sizeof(header) &&
            header.dwType==RIM_TYPEMOUSE) {
            // Preserve Windows' foreground raw-input cleanup without invoking the
            // game's mouse path. Keyboard/HID raw input still reaches the game.
            consumed=true; return DefWindowProcW(window,message,wparam,lparam);
        }
    }
    if(open && message==WM_MOUSEMOVE) {
        std::lock_guard lock(motion_mutex);
        motion={window,wparam,lparam,true};
        consumed=true; return 0;
    }
    // A button pressed while editing belongs to the overlay until its release,
    // even if the overlay hotkey closes it while the user is still dragging.
    const unsigned button=mouse_button_bit(message,wparam);
    const bool releasing=mouse_button_up(message) && (overlay_mouse_buttons.load()&button)!=0;
    if (open || releasing) {
        std::lock_guard lock(imgui_mutex);
        WindowDpiScope dpi(game_window);
        flush_mouse_motion();
        const auto result=ImGui_ImplWin32_WndProcHandler(window,message,wparam,lparam);
        if(button) {
            if(mouse_button_up(message)) overlay_mouse_buttons.fetch_and(~button);
            else overlay_mouse_buttons.fetch_or(button);
        }
        if(message==WM_NCMOUSEMOVE) {
            POINT point{static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
            if(ScreenToClient(window,&point)) map_mouse_position(static_cast<float>(point.x),static_cast<float>(point.y));
        }
        const bool mouse=(message>=WM_MOUSEFIRST && message<=WM_MOUSELAST);
        const bool keyboard=(message>=WM_KEYFIRST && message<=WM_KEYLAST) || message==WM_CHAR;
        consumed=mouse || (keyboard && ImGui::GetIO().WantCaptureKeyboard);
        if (toggle_key || (key_message && capturing_hotkey)) consumed=true;
        return result;
    }
    return 0;
}
bool install_overlay(Session& session,std::string& reason) {
    bound_session=&session;
    const auto options=preferences().snapshot();
    session.set_loop_buffers(options.loop_before,options.loop_after);
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"SDVXTrainingProbe";
    const auto atom=RegisterClassW(&wc);
    if (!atom && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { reason="Probe window registration failed"; return false; }
    HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    if (!window) { reason="Probe window creation failed"; return false; }
    IDirect3D9Ex* api{}; IDirect3DDevice9Ex* device{};
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window; pp.BackBufferFormat=D3DFMT_UNKNOWN;
    HRESULT hr=Direct3DCreate9Ex(D3D_SDK_VERSION,&api);
    if (SUCCEEDED(hr)) hr=api->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,nullptr,&device);
    bool ok=SUCCEEDED(hr);
    std::vector<void*> created;
    if (ok) {
        auto** table=*reinterpret_cast<void***>(device);
        struct Hook { unsigned slot; void* replacement; void** original; };
        const Hook hooks[]{
            {17,reinterpret_cast<void*>(present),reinterpret_cast<void**>(&original_present)},
            {121,reinterpret_cast<void*>(present_ex),reinterpret_cast<void**>(&original_present_ex)},
            {16,reinterpret_cast<void*>(reset),reinterpret_cast<void**>(&original_reset)},
            {132,reinterpret_cast<void*>(reset_ex),reinterpret_cast<void**>(&original_reset_ex)}
        };
        for (const auto& h:hooks) {
            if (MH_CreateHook(table[h.slot],h.replacement,h.original)!=MH_OK) { ok=false; break; }
            created.push_back(table[h.slot]);
        }
        if (ok) {
            Microsoft::WRL::ComPtr<IDirect3DSwapChain9> chain;
            ok=SUCCEEDED(device->GetSwapChain(0,&chain));
            if (ok) {
                auto** swap_table=*reinterpret_cast<void***>(chain.Get());
                ok=MH_CreateHook(swap_table[3],reinterpret_cast<void*>(swap_present),reinterpret_cast<void**>(&original_swap_present))==MH_OK;
                if (ok) created.push_back(swap_table[3]);
            }
        }
        if (ok) { for (auto* p:created) MH_QueueEnableHook(p); ok=MH_ApplyQueued()==MH_OK; }
        if (!ok) for (auto* p:created) { MH_DisableHook(p); MH_RemoveHook(p); }
    }
    if (device) device->Release();
    if (api) api->Release();
    DestroyWindow(window); if (atom) UnregisterClassW(wc.lpszClassName,wc.hInstance);
    reason=ok?"D3D9 overlay hooks installed":"Could not install D3D9 overlay hooks";
    return ok;
}
}
