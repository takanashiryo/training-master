#include "ui/overlay.hpp"
#include "core/judgment.hpp"
#include "ui/preferences.hpp"
#include <MinHook.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace {
using Microsoft::WRL::ComPtr;
struct TestBackend final:training::Backend {
    bool pause(bool) override { return true; }
    bool seek(float) override { return true; }
    bool rate(float) override { return true; }
    bool finish() override { return true; }
};
void require(HRESULT hr,const char* message) { if (FAILED(hr)) throw std::runtime_error(message); }
unsigned changed_pixels(IDirect3DDevice9* device,IDirect3DSurface9* surface) {
    D3DSURFACE_DESC desc{}; surface->GetDesc(&desc);
    ComPtr<IDirect3DSurface9> copy;
    require(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr),"readback allocation failed");
    require(device->GetRenderTargetData(surface,copy.Get()),"readback failed");
    D3DLOCKED_RECT data{}; require(copy->LockRect(&data,nullptr,D3DLOCK_READONLY),"readback lock failed");
    unsigned changed{};
    for (unsigned y=0;y<desc.Height;++y) {
        const auto* row=reinterpret_cast<const DWORD*>(static_cast<const char*>(data.pBits)+y*data.Pitch);
        for (unsigned x=0;x<desc.Width;++x) changed+=(row[x]&0xffffff)!=0x101010;
    }
    copy->UnlockRect(); return changed;
}
void pass(IDirect3DDevice9* device) {
    require(device->BeginScene(),"BeginScene failed");
    require(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(16,16,16),1,0),"Clear failed");
    require(device->EndScene(),"EndScene failed");
}
int frame_count() { return ImGui::GetCurrentContext()?ImGui::GetFrameCount():0; }
unsigned game_moves{},game_cursor_messages{};
unsigned game_button_ups{};
unsigned game_raw_mouse{},game_raw_keyboard{};
using RawInput=UINT(WINAPI*)(HRAWINPUT,UINT,LPVOID,PUINT,UINT);
RawInput original_raw_input{};
UINT WINAPI raw_input_probe(HRAWINPUT handle,UINT command,LPVOID data,PUINT size,UINT header_size) {
    const auto value=reinterpret_cast<std::uintptr_t>(handle);
    if((value==0x11110000 || value==0x22220000) && command==RID_HEADER && data && *size>=sizeof(RAWINPUTHEADER)) {
        auto* header=static_cast<RAWINPUTHEADER*>(data); *header={};
        header->dwType=value==0x11110000?RIM_TYPEMOUSE:RIM_TYPEKEYBOARD; *size=sizeof(*header); return sizeof(*header);
    }
    return original_raw_input(handle,command,data,size,header_size);
}
BOOL hardware_cursor=TRUE;
BOOL WINAPI cursor_probe(IDirect3DDevice9*,BOOL value) { const BOOL previous=hardware_cursor; hardware_cursor=value; return previous; }
LRESULT CALLBACK game_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_MOUSEMOVE) ++game_moves;
    if(message==WM_LBUTTONUP) ++game_button_ups;
    if(message==WM_SETCURSOR) {
        ++game_cursor_messages;
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));
        return TRUE;
    }
    if(message==WM_INPUT) {
        if(lparam==0x11110000) ++game_raw_mouse;
        if(lparam==0x22220000) ++game_raw_keyboard;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
void corner_marker(ImGuiContext*,ImGuiContextHook*) {
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::GetForegroundDrawList()->AddRectFilled({size.x-12,0},{size.x,12},IM_COL32(255,0,255,255));
    ImGui::GetForegroundDrawList()->AddRectFilled({0,size.y-12},{12,size.y},IM_COL32(255,0,255,255));
}
DWORD pixel(IDirect3DDevice9* device,IDirect3DSurface9* surface,unsigned x,unsigned y) {
    D3DSURFACE_DESC desc{}; surface->GetDesc(&desc);
    ComPtr<IDirect3DSurface9> copy;
    require(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr),"pixel readback allocation failed");
    require(device->GetRenderTargetData(surface,copy.Get()),"pixel readback failed");
    D3DLOCKED_RECT data{}; require(copy->LockRect(&data,nullptr,D3DLOCK_READONLY),"pixel lock failed");
    const auto value=*reinterpret_cast<const DWORD*>(static_cast<const char*>(data.pBits)+y*data.Pitch+x*4);
    copy->UnlockRect(); return value&0xffffff;
}
using EndScene=HRESULT(WINAPI*)(IDirect3DDevice9*);
EndScene original_end_scene{};
DWORD rendered_corner{};
int sampled_frame=-1;
HWND hotkey_window{};
bool hotkey_down{};
DWORD hotkey_clock{1000},hotkey_message_time{};
SHORT WINAPI hotkey_state_probe(int key) {
    return key==VK_INSERT && hotkey_down?static_cast<SHORT>(0x8000):0;
}
HWND WINAPI hotkey_foreground_probe() { return hotkey_window; }
DWORD WINAPI hotkey_clock_probe() { return hotkey_clock; }
LONG WINAPI hotkey_message_time_probe() { return static_cast<LONG>(hotkey_message_time); }
HRESULT WINAPI end_scene_probe(IDirect3DDevice9* device) {
    const auto hr=original_end_scene(device);
    // Windowed Present can discard/clobber pixels outside the visible client.
    // Read the actual GPU result immediately after the overlay EndScene instead.
    if(ImGui::GetCurrentContext() && frame_count()!=sampled_frame) {
        ComPtr<IDirect3DSurface9> back;
        if(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))) rendered_corner=(pixel(device,back.Get(),1355,5)==0xff00ff && pixel(device,back.Get(),5,895)==0xff00ff)?0xff00ff:0;
        sampled_frame=frame_count();
    }
    return hr;
}

}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>3?argv[3]:"present";
        const bool windowed=mode.starts_with("windowed");
        WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=game_proc; wc.lpszClassName=L"TrainingRenderRegression";
        RegisterClassW(&wc);
        RECT rect{0,0,960,600}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,
            windowed?rect.right-rect.left:1360,windowed?rect.bottom-rect.top:900,
            nullptr,nullptr,wc.hInstance,nullptr);
        if (!window) throw std::runtime_error("window creation failed");
        HWND focus=mode=="windowed-focus"?CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr):window;
        ComPtr<IDirect3D9> api; ComPtr<IDirect3D9Ex> api_ex;
        if (mode=="legacy") api.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        else { require(Direct3DCreate9Ex(D3D_SDK_VERSION,&api_ex),"D3D9Ex unavailable"); api=api_ex; }
        if (!api) throw std::runtime_error("D3D9 unavailable");
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_COPY;
        pp.hDeviceWindow=window; pp.BackBufferWidth=1360; pp.BackBufferHeight=900; pp.BackBufferFormat=D3DFMT_A8R8G8B8;
        pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ComPtr<IDirect3DDevice9> device; ComPtr<IDirect3DDevice9Ex> device_ex;
        if (mode=="legacy") require(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&device),"D3D9 device creation failed");
        else {
            require(api_ex->CreateDeviceEx(0,D3DDEVTYPE_HAL,focus,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,nullptr,&device_ex),"device creation failed");
            device=device_ex;
        }
        ComPtr<IDirect3DSwapChain9> chain; device->GetSwapChain(0,&chain);
        const auto display=[&] {
            if (mode=="ex") return device_ex->PresentEx(nullptr,nullptr,nullptr,nullptr,0);
            if (mode=="swap") return chain->Present(nullptr,nullptr,nullptr,nullptr,0);
            return device->Present(nullptr,nullptr,nullptr,nullptr);
        };
        if (MH_Initialize()!=MH_OK) throw std::runtime_error("MinHook init failed");
        training::Session session; std::string reason;
        if (!training::install_overlay(session,reason)) throw std::runtime_error(reason);
        if(mode=="hotkey") {
            hotkey_window=window;
            const auto probe=[](void* entry,void* replacement) {
                void* original{};
                if(MH_CreateHook(entry,replacement,&original)!=MH_OK || MH_EnableHook(entry)!=MH_OK)
                    throw std::runtime_error("hotkey boundary probe failed");
            };
            probe(reinterpret_cast<void*>(GetAsyncKeyState),reinterpret_cast<void*>(hotkey_state_probe));
            probe(reinterpret_cast<void*>(GetKeyState),reinterpret_cast<void*>(hotkey_state_probe));
            probe(reinterpret_cast<void*>(GetForegroundWindow),reinterpret_cast<void*>(hotkey_foreground_probe));
            probe(reinterpret_cast<void*>(GetTickCount),reinterpret_cast<void*>(hotkey_clock_probe));
            probe(reinterpret_cast<void*>(GetMessageTime),reinterpret_cast<void*>(hotkey_message_time_probe));
            const auto tick=[&] { hotkey_clock+=10; pass(device.Get()); require(display(),"hotkey Present failed"); };
            tick();
            hotkey_down=true; tick();
            const auto pressed_at=hotkey_clock;
            if(frame_count()!=1) throw std::runtime_error("first physical press must open overlay");
            hotkey_down=false; tick();
            const auto before=frame_count();
            // The game can pump the original key messages after polling already
            // opened the overlay and observed the physical key release.
            hotkey_message_time=pressed_at;
            SendMessageW(window,WM_KEYDOWN,VK_INSERT,1);
            hotkey_message_time=hotkey_clock;
            SendMessageW(window,WM_KEYUP,VK_INSERT,1LL<<31);
            tick();
            if(frame_count()!=before+1) throw std::runtime_error("first opening must survive delayed key messages instead of immediately hiding");
            const auto expect=[&](bool open,const char* message) {
                const auto frames=frame_count(); tick();
                if(frame_count()!=frames+(open?1:0)) throw std::runtime_error(message);
            };
            const auto message=[&](bool pressed,DWORD time,bool repeat=false) {
                hotkey_message_time=time;
                SendMessageW(window,pressed?WM_KEYDOWN:WM_KEYUP,VK_INSERT,
                    pressed?(repeat?(1LL<<30)|1:1):(1LL<<31)|(1LL<<30)|1);
            };
            hotkey_clock+=2;
            message(true,hotkey_clock); message(false,hotkey_clock+1);
            expect(false,"a later quick message-only tap must close normally");
            hotkey_clock+=2; hotkey_down=true;
            message(true,hotkey_clock);
            expect(true,"message-first press must open exactly once");
            message(true,hotkey_clock,true);
            expect(true,"held-key autorepeat must not toggle overlay");
            hotkey_down=false; message(false,hotkey_clock+1);
            expect(true,"key release must preserve visibility");
            hotkey_down=true; expect(false,"polling-only press must close without window messages");
            hotkey_down=false; expect(false,"polling-only release must stay closed");
            hotkey_down=true; expect(true,"another polling-only press must still open");
            hotkey_down=false; expect(true,"polling-only release must stay open");
            hotkey_clock+=2;
            message(true,hotkey_clock); message(false,hotkey_clock+1);
            expect(false,"old polling history must not swallow a new quick tap");
            hotkey_clock+=2;
            message(true,hotkey_clock); message(false,hotkey_clock+1);
            message(true,hotkey_clock+2); message(false,hotkey_clock+3);
            expect(false,"two distinct quick taps between frames must both be handled");
            hotkey_down=true; expect(true,"first queued-behind-polling press must open");
            const auto first_press=hotkey_clock;
            hotkey_down=false; expect(true,"first queued-behind-polling release");
            const auto first_release=hotkey_clock;
            hotkey_down=true; expect(false,"second queued-behind-polling press must close");
            const auto second_press=hotkey_clock;
            hotkey_down=false; expect(false,"second queued-behind-polling release");
            message(true,first_press); message(false,first_release);
            message(true,second_press); message(false,hotkey_clock);
            expect(false,"delayed messages for multiple polled presses must not toggle again");
            MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize();
            training::shutdown_imgui(); DestroyWindow(window);
            std::cout<<"Overlay hotkey: first-open, delayed messages, held keys, quick taps and polling-only scenarios passed\n";
            return 0;
        }
        training::show_overlay(true); training::set_overlay_status("ready");
        ComPtr<IDirect3DSurface9> back;
        require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"backbuffer missing");
        ComPtr<IDirect3DTexture9> texture; ComPtr<IDirect3DSurface9> offscreen;
        require(device->CreateTexture(1360,900,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr),"texture allocation failed");
        texture->GetSurfaceLevel(0,&offscreen);
        pass(device.Get()); require(display(),"warmup Present failed");
        const int before=frame_count();
        const auto start=std::chrono::steady_clock::now();
        // A mouse-driven UI renderer can rebuild many cached textures per displayed frame.
        // Drive the real window handler and installed hooks, not a simulated render policy.
        device->SetRenderTarget(0,offscreen.Get());
        const unsigned passes=argc>1?static_cast<unsigned>(std::stoi(argv[1])):32;
        const unsigned mouse_moves=argc>2?static_cast<unsigned>(std::stoi(argv[2])):16;
        for (unsigned i=0;i<passes;++i) {
            for (unsigned mouse=0;mouse<mouse_moves;++mouse)
                SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(40+(i*16+mouse)%1000,120+i));
            pass(device.Get());
        }
        const int offscreen_frames=frame_count()-before;
        const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const unsigned contaminated=changed_pixels(device.Get(),offscreen.Get());
        // Leave the cached texture bound at Present, as engines may do. Overlay belongs to backbuffer.
        device->SetRenderTarget(0,back.Get());
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(16,16,16),1,0),"backbuffer clear failed");
        device->SetRenderTarget(0,offscreen.Get());
        D3DCAPS9 caps{}; device->GetDeviceCaps(&caps);
        ComPtr<IDirect3DSurface9> secondary;
        if (caps.NumSimultaneousRTs>1) {
            require(device->CreateRenderTarget(1360,900,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&secondary,nullptr),"MRT allocation failed");
            require(device->ColorFill(secondary.Get(),nullptr,D3DCOLOR_XRGB(16,16,16)),"MRT clear failed");
            device->SetRenderTarget(1,secondary.Get());
        }
        ComPtr<IDirect3DSurface9> depth;
        require(device->CreateDepthStencilSurface(1360,900,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr),"depth allocation failed");
        device->SetDepthStencilSurface(depth.Get());
        const D3DVIEWPORT9 viewport{11,13,1024,768,.2f,.8f}; device->SetViewport(&viewport);
        const RECT scissor{20,30,700,600}; device->SetScissorRect(&scissor);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,8); device->SetRenderState(D3DRS_SRGBWRITEENABLE,TRUE);
        device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE); device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        require(display(),"Present failed");
        const int displayed_frames=frame_count()-before;
        ComPtr<IDirect3DSurface9> restored; device->GetRenderTarget(0,&restored);
        const unsigned visible_pixels=changed_pixels(device.Get(),back.Get());
        D3DVIEWPORT9 restored_viewport{}; device->GetViewport(&restored_viewport);
        RECT restored_scissor{}; device->GetScissorRect(&restored_scissor);
        ComPtr<IDirect3DSurface9> restored_depth; device->GetDepthStencilSurface(&restored_depth);
        ComPtr<IDirect3DSurface9> restored_secondary;
        if (secondary) device->GetRenderTarget(1,&restored_secondary);
        DWORD write{},srgb{},sampler_srgb{},blend{};
        device->GetRenderState(D3DRS_COLORWRITEENABLE,&write); device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);
        device->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&sampler_srgb); device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        const bool states=std::memcmp(&viewport,&restored_viewport,sizeof(viewport))==0 &&
            std::memcmp(&scissor,&restored_scissor,sizeof(scissor))==0 && restored_depth.Get()==depth.Get() &&
            write==8 && srgb==TRUE && sampler_srgb==TRUE && blend==FALSE &&
            (!secondary || (restored_secondary.Get()==secondary.Get() && changed_pixels(device.Get(),secondary.Get())==0));
        if (!states) std::cout<<"State detail: viewport="<<restored_viewport.X<<','<<restored_viewport.Y<<','<<restored_viewport.Width<<','<<restored_viewport.Height
            <<" scissor="<<restored_scissor.left<<','<<restored_scissor.top<<','<<restored_scissor.right<<','<<restored_scissor.bottom
            <<" depth="<<(restored_depth.Get()==depth.Get())<<" writes="<<write<<" srgb="<<srgb<<" sampler="<<sampler_srgb<<" blend="<<blend<<'\n';
        std::cout<<passes<<" cached UI passes + "<<passes*mouse_moves<<" mouse moves: overlay frames="<<offscreen_frames
                 <<", contaminated texture pixels="<<contaminated<<", elapsed_ms="<<milliseconds<<'\n';
        std::cout<<"One "<<mode<<": total overlay frames="<<displayed_frames<<", backbuffer overlay pixels="<<visible_pixels
                 <<", target restored="<<(restored.Get()==offscreen.Get())<<", state restored="<<states<<'\n';
        bool ok=offscreen_frames==0 && contaminated==0 && displayed_frames==1 && visible_pixels>100 && restored.Get()==offscreen.Get() && states;
        training::show_overlay(false); int hidden_before=frame_count();
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf); device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device->SetRenderTarget(0,back.Get());
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(16,16,16),1,0),"hidden clear failed");
        device->SetRenderTarget(0,offscreen.Get());
        for (unsigned i=0;i<8;++i) { pass(device.Get()); require(display(),"hidden Present failed"); }
        const auto hidden_pixels=changed_pixels(device.Get(),back.Get());
        const bool hidden=frame_count()==hidden_before && hidden_pixels==0;
        if (!hidden) std::cout<<"Hidden detail: frames="<<frame_count()-hidden_before<<" pixels="<<hidden_pixels<<'\n';
        std::cout<<"Hidden overlay: no frames/draws="<<hidden<<'\n'; ok &= hidden;
        const auto config=std::filesystem::temp_directory_path()/(L"training-render-"+std::to_wstring(GetCurrentProcessId())+L".ini");
        std::string config_error;
        if(!training::preferences().load(config,config_error)) throw std::runtime_error("counter config fixture failed");
        session.begin({"fixture.vox",1,0,120},true);
        training::judgment::set_available(true);
        const auto default_before=frame_count();
        for(int i=0;i<3;++i) { pass(device.Get()); require(display(),"default hidden counter Present failed"); }
        const bool default_hidden=frame_count()==default_before && changed_pixels(device.Get(),back.Get())==0;
        std::cout<<"Default hidden counter="<<default_hidden<<'\n'; ok &= default_hidden;
        auto options=training::preferences().snapshot(); options.always_show_judgment_counter=true;
        if(!training::preferences().save(options,config_error)) throw std::runtime_error("counter config save failed");
        const auto counter_before=frame_count();
        // ImGui measures a newly created auto-sized window on its first frame.
        for(int i=0;i<3;++i) { pass(device.Get()); require(display(),"judgment-only Present failed"); }
        const auto counter_pixels=changed_pixels(device.Get(),back.Get());
        const bool judgment_visible=frame_count()==counter_before+3 && counter_pixels>100;
        if(!judgment_visible) std::cout<<"Judgment detail: frames="<<frame_count()-counter_before<<" pixels="<<counter_pixels<<'\n';
        std::cout<<"Hidden main panel, active judgment window="<<judgment_visible<<'\n'; ok &= judgment_visible;
        TestBackend backend; session.end(backend);
        std::filesystem::remove(config);
        hidden_before=frame_count();
        // Real device reset: overlay-owned default-pool resources must not keep Reset from succeeding.
        device->SetRenderTarget(1,nullptr); device->SetRenderTarget(0,back.Get()); device->SetDepthStencilSurface(nullptr);
        restored.Reset(); restored_depth.Reset(); restored_secondary.Reset(); secondary.Reset(); depth.Reset();
        offscreen.Reset(); texture.Reset(); back.Reset(); chain.Reset();
        const auto reset=device_ex?device_ex->ResetEx(&pp,nullptr):device->Reset(&pp);
        require(reset,"device Reset/ResetEx failed");
        device->GetSwapChain(0,&chain); device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
        training::show_overlay(true); pass(device.Get()); require(display(),"Present after Reset failed");
        const bool recovered=frame_count()==hidden_before+1 && changed_pixels(device.Get(),back.Get())>100;
        std::cout<<"Reset recovery: "<<recovered<<'\n'; ok &= recovered;
        if(windowed) {
            if(MH_CreateHook(reinterpret_cast<void*>(GetRawInputData),reinterpret_cast<void*>(raw_input_probe),reinterpret_cast<void**>(&original_raw_input))!=MH_OK ||
                MH_EnableHook(reinterpret_cast<void*>(GetRawInputData))!=MH_OK) throw std::runtime_error("raw input boundary probe failed");
            void* original_cursor{};
            if(MH_CreateHook((*reinterpret_cast<void***>(device.Get()))[12],reinterpret_cast<void*>(cursor_probe),&original_cursor)!=MH_OK || MH_EnableHook((*reinterpret_cast<void***>(device.Get()))[12])!=MH_OK)
                throw std::runtime_error("cursor boundary probe failed");
            auto* end_entry=(*reinterpret_cast<void***>(device.Get()))[42];
            if(MH_CreateHook(end_entry,reinterpret_cast<void*>(end_scene_probe),reinterpret_cast<void**>(&original_end_scene))!=MH_OK || MH_EnableHook(end_entry)!=MH_OK)
                throw std::runtime_error("EndScene readback probe failed");
            ImGuiContextHook hook{}; hook.Type=ImGuiContextHookType_NewFramePost; hook.Callback=corner_marker;
            ImGui::AddContextHook(ImGui::GetCurrentContext(),&hook);
            const auto tick=[&] { pass(device.Get()); require(display(),"windowed Present failed"); };
            SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(950,590)); tick(); tick();
            // Outside the overlay panel: game mouse handlers must still stay quiet
            // while the overlay owns editing, and movement must not build a backlog.
            game_moves=game_cursor_messages=0;
            const auto before_input=frame_count();
            const auto input_start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<8192;++n) SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(940+n%11,580+n%11));
            for(unsigned n=0;n<128;++n) SendMessageW(window,WM_SETCURSOR,reinterpret_cast<WPARAM>(window),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
            for(unsigned n=0;n<1024;++n) SendMessageW(window,WM_INPUT,RIM_INPUT,0x11110000);
            SendMessageW(window,WM_INPUT,RIM_INPUT,0x22220000);
            SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(950,590));
            const auto queued=ImGui::GetCurrentContext()->InputEventsQueue.Size;
            const auto input_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-input_start).count();
            const bool no_input_draw=frame_count()==before_input;
            device->ShowCursor(TRUE); tick();
            const bool hardware_hidden=device->ShowCursor(FALSE)==FALSE;
            const auto& io=ImGui::GetIO();
            const bool canvas=io.DisplaySize.x==1360 && io.DisplaySize.y==900;
            const bool mapped=std::abs(io.MousePos.x-950.f*1360/960)<1 && std::abs(io.MousePos.y-590.f*900/600)<1;
            const bool corner=rendered_corner==0xff00ff;
            std::cout<<"Windowed 960x600 -> 1360x900: full canvas="<<canvas<<", mouse mapped="<<mapped<<", corner covered="<<corner<<'\n';
            std::cout<<"8193 moves / 128 cursor messages: forwarded="<<game_moves<<'/'<<game_cursor_messages
                     <<", queued="<<queued<<", hardware cursor hidden="<<hardware_hidden<<", no input draws="<<no_input_draw<<", ms="<<input_ms<<'\n';
            ok &= canvas && mapped && corner && game_moves==0 && game_cursor_messages==0 && queued<=2 && hardware_hidden && io.MouseDrawCursor && no_input_draw;
            std::cout<<"Raw input forwarded: mouse="<<game_raw_mouse<<", keyboard="<<game_raw_keyboard<<'\n';
            ok &= game_raw_mouse==0 && game_raw_keyboard==1;
            RECT resized{0,0,1280,720}; AdjustWindowRect(&resized,WS_OVERLAPPEDWINDOW,FALSE);
            SetWindowPos(window,nullptr,0,0,resized.right-resized.left,resized.bottom-resized.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            tick(); SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(1270,710)); tick();
            const bool resize_mapping=std::abs(io.MousePos.x-1270.f*1360/1280)<1 && std::abs(io.MousePos.y-710.f*900/720)<1;
            auto* panel=ImGui::FindWindowByName("Training Master | Close Beta###Training Master");
            const auto move=[&](float x,float y) { SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(static_cast<int>(x*1280/1360),static_cast<int>(y*720/900))); tick(); };
            move(panel->Pos.x+100,panel->Pos.y+10); tick();
            SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,0); tick(); tick();
            move(1340,880); tick();
            SendMessageW(window,WM_LBUTTONUP,0,0); tick(); tick();
            const bool drag=panel->Pos.x>1000 && panel->Pos.y>800;
            std::cout<<"Client resize remaps input="<<resize_mapping<<", drag reaches right/bottom="<<drag<<'\n';
            ok &= resize_mapping && drag;
            SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,0); tick();
            training::show_overlay(false); tick();
            SendMessageW(window,WM_LBUTTONUP,0,0);
            const bool release=game_button_ups==0 && GetCapture()!=window;
            const bool restored_cursor=device->ShowCursor(TRUE)==TRUE;
            std::cout<<"Hidden overlay restores game hardware cursor="<<restored_cursor<<", releases owned drag="<<release<<'\n'; ok &= restored_cursor && release;
            const auto hidden_moves=game_moves;
            SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(50,50));
            ok &= game_moves==hidden_moves+1;
        }
        MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize();
        if (ImGui::GetCurrentContext()) training::shutdown_imgui();
        device->SetRenderTarget(0,back.Get());
        DestroyWindow(window);
        if(focus!=window) DestroyWindow(focus);
        if (!ok) { std::cerr<<"FAIL: overlay must render once at Present, never into cached UI textures\n"; return 1; }
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
