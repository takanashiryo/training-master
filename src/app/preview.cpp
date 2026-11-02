#include "ui/overlay.hpp"
#include "ui/preferences.hpp"
#include "platform/log.hpp"
#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
IDirect3DDevice9* device{};
D3DPRESENT_PARAMETERS present{};
class SimulatedGame final : public training::Backend {
public:
    float position{}, speed{1}; bool paused{}, finished{},reversing{};
    bool pause(bool value) override { paused=value; return true; }
    bool seek(float seconds) override { position=seconds; return true; }
    bool rate(float value) override { speed=value; return true; }
    bool finish() override { finished=true; return true; }
    bool reverse(bool value) override { reversing=value; return true; }
    bool sync_reverse_chart(float value) override { position=value; return true; }
    bool seek_chart(float value) override { position=value; return true; }
    bool lane_speed(bool) override { return true; }
    bool gauge(const training::GaugeConfig&) override { return true; }
};
std::shared_ptr<const training::Chart> demo_chart() {
    auto chart=std::make_shared<training::Chart>();
    for (unsigned i=0;i<300;++i) {
        const float time=1.f+i*.48f;
        chart->notes.push_back({time,time+(i%5==0?1.2f:0.f),i%6});
        if (i%4==0) chart->notes.push_back({time,time,(i+2)%6});
    }
    for (unsigned i=0;i<26;++i) {
        const float start=2.f+i*5.5f;
        training::ChartLaser laser{i%2};
        for (unsigned j=0;j<7;++j) laser.points.push_back({start+j*.5f,(j%3)*.5f,0,j>=4});
        // Duplicate-time knots exercise visible vertical slams.
        laser.points.insert(laser.points.begin()+3,{start+1.f,.95f,0,false});
        chart->lasers.push_back(std::move(laser));
    }
    chart->tempos={{0,120},{60,180},{100,90}};
    float time=0;
    unsigned beat=0;
    for(unsigned measure=0;time<150;++measure) {
        chart->measures.push_back({time,measure});
        // Simulate both a changing BPM and a 3/4 section.
        const unsigned count=measure>=32 && measure<40?3:4;
        for(unsigned b=0;b<count;++b) {
            chart->beats.push_back({time,beat++});
            const float bpm=time>=100?90.f:time>=60?180.f:120.f;
            const float next=time>=100?1e6f:time>=60?100.f:60.f;
            const float end=time+60.f/bpm;
            time=end<=next?end:next+(1.f-(next-time)*bpm/60.f)*60.f/(next==60?180.f:90.f);
        }
    }
    chart->timing=training::MusicalTimeline::build(chart->measures,chart->beats,chart->tempos);
    return chart;
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    bool used=false; training::input_imgui(w,m,a,b,used);
    if (m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m==WM_SIZE && device && a!=SIZE_MINIMIZED) {
        present.BackBufferWidth=LOWORD(b); present.BackBufferHeight=HIWORD(b);
        training::invalidate_imgui_device_objects();
        if (SUCCEEDED(device->Reset(&present))) ImGui_ImplDX9_CreateDeviceObjects();
    }
    if (used) return 1;
    return DefWindowProcW(w,m,a,b);
}
bool screenshot(const std::filesystem::path& path) {
    IDirect3DSurface9 *back{},*copy{};
    if (FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))) return false;
    D3DSURFACE_DESC desc{}; back->GetDesc(&desc);
    bool ok=SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    if (ok) ok=SUCCEEDED(device->GetRenderTargetData(back,copy));
    if (ok) {
        D3DLOCKED_RECT data{};
        ok=SUCCEEDED(copy->LockRect(&data,nullptr,D3DLOCK_READONLY));
        if (ok) {
            BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
            info.biSize=sizeof(info); info.biWidth=static_cast<LONG>(desc.Width);
            info.biHeight=-static_cast<LONG>(desc.Height); info.biPlanes=1; info.biBitCount=32;
            info.biCompression=BI_RGB; info.biSizeImage=desc.Width*desc.Height*4;
            file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=file.bfOffBits+info.biSizeImage;
            std::ofstream out(path,std::ios::binary);
            out.write(reinterpret_cast<const char*>(&file),sizeof(file)); out.write(reinterpret_cast<const char*>(&info),sizeof(info));
            for (UINT row=0;row<desc.Height;++row) out.write(static_cast<const char*>(data.pBits)+row*data.Pitch,desc.Width*4);
            ok=out.good(); copy->UnlockRect();
        }
    }
    if(copy) copy->Release(); back->Release(); return ok;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int) {
    const bool smoke=std::wstring(command).find(L"--smoke")!=std::wstring::npos;
    const bool smoke_reset=std::wstring(command).find(L"--smoke-reset")!=std::wstring::npos;
    const bool premium=std::wstring(command).find(L"--premium-time")!=std::wstring::npos;
    const bool narrow=std::wstring(command).find(L"--narrow")!=std::wstring::npos;
    const bool compact=std::wstring(command).find(L"--compact")!=std::wstring::npos;
    const bool loop_off=std::wstring(command).find(L"--loop-off")!=std::wstring::npos;
    const bool repeat=std::wstring(command).find(L"--repeat")!=std::wstring::npos;
    const bool automatic=std::wstring(command).find(L"--autoplay")!=std::wstring::npos;
    const bool reverse=std::wstring(command).find(L"--reverse")!=std::wstring::npos;
    const bool paused_preview=std::wstring(command).find(L"--paused")!=std::wstring::npos;
    const bool entry_preview=std::wstring(command).find(L"--entry-rewind")!=std::wstring::npos;
    training::open_log(nullptr);
    std::string config_error;
    if(!training::initialize_preferences(nullptr,config_error)) training::log(config_error);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=proc; wc.lpszClassName=L"TrainingMasterPreview";
    RegisterClassW(&wc);
    auto window_language=training::preferences().snapshot().language;
    const auto window_title=[](training::i18n::Language language) {
        return language==training::i18n::Language::Chinese?L"训练大师":L"Training Master";
    };
    auto window=CreateWindowW(wc.lpszClassName,window_title(window_language),WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,narrow?1080:1360,900,nullptr,nullptr,instance,nullptr);
    auto* api=Direct3DCreate9(D3D_SDK_VERSION);
    if (!window || !api) return 1;
    present.Windowed=TRUE; present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    present.hDeviceWindow=window; present.BackBufferFormat=D3DFMT_X8R8G8B8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    if (FAILED(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&present,&device))) return 2;
    if (!training::init_imgui(window,device)) return 3;
    training::set_overlay_status("OFFLINE PREVIEW / simulated track");
    training::show_overlay(true);
    training::Session session;
    const auto options=training::preferences().snapshot();
    session.set_loop_buffers(options.loop_before,options.loop_after);
    const auto chart=demo_chart();
    training::Track track{"/data/music/preview/preview_4i.vox",1001,3,150.f};
    track.native_quick_finish=premium; track.preview=chart; track.autoplay_ready=true;
    auto info=std::make_shared<training::SongInfo>();
    info->title="Training Preview / 練習用サンプル"; info->artist="Simulated track"; info->difficulty="INFINITE  Lv. 18.0";
    track.info=info;
    track.camera_ready=true;
    track.lane_speed_ready=true; track.gauge_ready=true;
    auto silent=std::make_shared<training::PcmClip>(); silent->samples.resize(2,0.f);
    track.reverse_audio=std::make_shared<training::ReverseAsset>(silent);
    // Optional real-resource fixture; never bundle or copy game assets into the preview.
    const std::wstring arguments(command);
    const auto resource_arg=arguments.find(L"--contents=\"");
    if (resource_arg!=std::wstring::npos) {
        const auto begin=resource_arg+12, end=arguments.find(L'"',begin);
        training::SongCatalog catalog; std::string error;
        if (end!=std::wstring::npos && catalog.load(arguments.substr(begin,end-begin),error)) {
            track.music_id=1767; track.difficulty=4;
            track.chart="/data/music/1767_mixxon_misoilepunch/1767_mixxon_misoilepunch_5m.vox";
            track.info=catalog.lookup(track.music_id,track.difficulty,track.chart);
        }
    }
    session.begin(std::move(track),true);
    SimulatedGame game;
    if (entry_preview) {
        game.position=90.f; session.tick(90.f,false,game);
        session.submit({session.snapshot().generation,training::Action::Enable});
    } else if (smoke) {
        session.submit({session.snapshot().generation,training::Action::Enable});
        session.submit({session.snapshot().generation,training::Action::Seek,32.f});
        session.submit({session.snapshot().generation,training::Action::SetIn,24.f});
        session.submit({session.snapshot().generation,training::Action::SetOut,48.f});
        session.submit({session.snapshot().generation,training::Action::SetLoop,loop_off?0.f:1.f});
        session.submit({session.snapshot().generation,training::Action::SetRate,.75f});
    }
    if (std::wstring(command).find(L"--gauge")!=std::wstring::npos) {
        session.submit({session.snapshot().generation,training::Action::SetGaugeEnabled,1.f});
        session.submit({session.snapshot().generation,training::Action::SetGaugeType,5.f});
        session.submit({session.snapshot().generation,training::Action::SetGaugeInitial,65.f});
    }
    if (automatic) session.submit({session.snapshot().generation,training::Action::SetAutoplay,1.f});
    if (repeat) session.submit({session.snapshot().generation,training::Action::SetRepeat,1.f});
    if (reverse) session.submit({session.snapshot().generation,training::Action::Reverse});
    if (paused_preview) session.submit({session.snapshot().generation,training::Action::Pause});
    ShowWindow(window,SW_SHOWDEFAULT);
    auto last=std::chrono::steady_clock::now();
    unsigned frames{}; bool done=false, saved=!smoke;
    while (!done) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==WM_QUIT) done=true;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (done) break;
        const auto language=training::preferences().snapshot().language;
        if(language!=window_language) { window_language=language; SetWindowTextW(window,window_title(language)); }
        const auto now=std::chrono::steady_clock::now();
        const float elapsed=std::chrono::duration<float>(now-last).count();
        if (!game.paused) game.position+=elapsed*game.speed*(game.reversing?-1.f:1.f);
        last=now;
        session.tick(game.position,game.paused,game,elapsed);
        if (game.finished) { session.result_blocked(); session.end(game); game.finished=false; }
        if(compact) ImGui::SetWindowSize("###Training Master",{780,400});
        device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(12,16,27),1,0);
        if (SUCCEEDED(device->BeginScene())) { training::frame_imgui(session); device->EndScene(); }
        if (smoke && entry_preview && frames==0) saved=screenshot(std::filesystem::current_path()/L"entry-start.bmp");
        if (smoke && frames==15) saved=screenshot(std::filesystem::current_path()/L"preview.bmp");
        if (smoke_reset && frames==10) {
            training::invalidate_imgui_device_objects();
            if (FAILED(device->Reset(&present))) return 5;
            ImGui_ImplDX9_CreateDeviceObjects();
        }
        const auto hr=device->Present(nullptr,nullptr,nullptr,nullptr);
        if (hr==D3DERR_DEVICELOST && device->TestCooperativeLevel()==D3DERR_DEVICENOTRESET) {
            training::invalidate_imgui_device_objects();
            if (SUCCEEDED(device->Reset(&present))) ImGui_ImplDX9_CreateDeviceObjects();
        }
        if (smoke) {
            ++frames;
            if (!entry_preview && frames>=30) done=true;
            else if (entry_preview && frames>=30 && !session.snapshot().rewinding) {
                const auto state=session.snapshot();
                saved=saved && state.position==0.f && state.paused &&
                    screenshot(std::filesystem::current_path()/L"entry-end.bmp");
                done=true;
            } else if (entry_preview && frames>=600) return 6;
        }
        Sleep(5);
    }
    training::shutdown_imgui();
    device->Release(); api->Release(); DestroyWindow(window); UnregisterClassW(wc.lpszClassName,instance);
    return saved?0:4;
}
