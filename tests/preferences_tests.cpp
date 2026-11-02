#include "ui/preferences.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary); return {std::istreambuf_iterator<char>(input),{}};
}
}
int main() {
    using namespace training;
    const auto directory=std::filesystem::temp_directory_path()/(L"training-preferences-配置-"+std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        const auto path=directory/L"trainingmaster.ini";
        PreferencesStore store; std::string error;
        check(store.load(path,error),"missing INI must create defaults beside requested path");
        check(store.snapshot()==Preferences{},"defaults are 1 second on each side and Insert");
        check(!store.snapshot().always_show_judgment_counter && read(path).find("AlwaysShowJudgmentCounter=0")!=std::string::npos,
              "judgment counter must default to overlay-only and write its preference");
        check(store.snapshot().background_opacity==.90f,"default background opacity must be 90 percent");
        check(read(path).find("StartBufferSeconds=1")!=std::string::npos && read(path).find("EndBufferSeconds=1")!=std::string::npos,"defaults must be written");
        auto changed=store.snapshot(); changed.loop_before=0; changed.loop_after=2.75f;
        changed.background_opacity=.37f; changed.chart_column_measures=7; changed.follow_playback=false; changed.always_show_judgment_counter=true; changed.language=i18n::Language::Chinese;
        changed.hotkey={VK_F9,true,false,true};
        check(store.save(changed,error),"changed preferences must persist");
        PreferencesStore restarted;
        check(restarted.load(path,error) && restarted.snapshot()==changed,"restart must restore zero, fractional delay and chord");
        const auto bytes=read(path);
        for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            auto bad=changed; bad.loop_before=invalid;
            check(!store.save(bad,error) && store.snapshot()==changed && read(path)==bytes,"invalid edits must not alter live/file state");
        }
        auto bad_language=changed; bad_language.language=static_cast<i18n::Language>(99);
        check(!store.save(bad_language,error) && store.snapshot()==changed && read(path)==bytes,"unsupported language must not alter preferences");
        check(read(path).find("AlwaysShowJudgmentCounter=1")!=std::string::npos,"counter visibility preference must persist");
        check(read(path).find("Language=zh-CN")!=std::string::npos,"Chinese language must persist as zh-CN");
        auto bad=changed; bad.hotkey.key=VK_LBUTTON;
        check(!store.save(bad,error) && store.snapshot()==changed,"mouse buttons cannot become keyboard shortcuts");
        for(float invalid:{-.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            auto bad_opacity=changed; bad_opacity.background_opacity=invalid;
            check(!store.save(bad_opacity,error) && store.snapshot()==changed && read(path)==bytes,
                  "invalid opacity must not alter live/file state");
        }
        check(read(path).find("DefaultColumnMeasures=7")!=std::string::npos && read(path).find("FollowPlayback=0")!=std::string::npos,
              "chart view options must be written to INI");
        for(int invalid:{-1,0,13,100000}) {
            auto bad_column=changed; bad_column.chart_column_measures=invalid;
            check(!store.save(bad_column,error) && store.snapshot()==changed && read(path)==bytes,
                  "invalid column duration must not alter live/file state");
        }
        // Lock the destination to force the real atomic replacement to fail.
        const auto lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(lock!=INVALID_HANDLE_VALUE,"fixture must lock INI");
        auto blocked=changed; blocked.loop_before=9;
        const bool saved=store.save(blocked,error); CloseHandle(lock);
        check(!saved && !error.empty() && store.snapshot()==changed && read(path)==bytes,"failed write must retain old file and active values");
        {
            std::ofstream out(path,std::ios::binary|std::ios::trunc);
            out<<"\xef\xbb\xbf[Loop]\nStartBufferSeconds=-5\nEndBufferSeconds=nan\n[Overlay]\nToggleKey=0x01\nLanguage=invalid\nCtrl=2\nBackgroundOpacity=1.1\nAlwaysShowJudgmentCounter=2\n[ChartView]\nDefaultColumnMeasures=13\nFollowPlayback=2\n";
        }
        PreferencesStore malformed;
        check(!malformed.load(path,error) && malformed.snapshot()==Preferences{},"malformed known values must fall back safely");
        {
            std::ofstream out(path,std::ios::trunc);
            out<<"[Loop]\nStartBufferSeconds=1.25 ; seconds\nEndBufferSeconds=0\n[Overlay]\nToggleKey=0x7B\nCtrl=1\nAlt=1\nShift=0\n[ChartView]\nDefaultColumnSeconds=12\n";
        }
        check(malformed.load(path,error) && malformed.snapshot().hotkey==OverlayHotkey{VK_F12,true,true,false} &&
              malformed.snapshot().loop_before==1.25f && malformed.snapshot().loop_after==0 && malformed.snapshot().background_opacity==.90f && malformed.snapshot().chart_column_measures==4 && malformed.snapshot().follow_playback && !malformed.snapshot().always_show_judgment_counter && malformed.snapshot().language==i18n::Language::English,
              "legacy INI must preserve its keys and ignore the obsolete seconds unit, defaulting to 4 measures");
        for(const char* invalid:{"1.5","nan","0","13"}) {
            { std::ofstream out(path,std::ios::trunc); out<<"[ChartView]\nDefaultColumnMeasures="<<invalid<<'\n'; }
            check(!malformed.load(path,error) && malformed.snapshot().chart_column_measures==4,"invalid or fractional bar counts must fall back to 4");
        }
        for(float opacity:{0.f,1.f}) {
            auto boundary=changed; boundary.background_opacity=opacity;
            check(store.save(boundary,error) && restarted.load(path,error) && restarted.snapshot()==boundary,
                  "fully transparent and opaque values must round-trip");
        }
        for(int measures:{1,12}) {
            auto boundary=changed; boundary.chart_column_measures=measures;
            check(store.save(boundary,error) && restarted.load(path,error) && restarted.snapshot()==boundary,
                  "column duration boundaries must round-trip");
        }
        for(auto language:{i18n::Language::English,i18n::Language::Chinese}) {
            auto localized=changed; localized.language=language;
            check(store.save(localized,error) && restarted.load(path,error) && restarted.snapshot()==localized,"both language choices must round-trip");
        }
        check(!hotkey_name(changed.hotkey).empty() && !valid_overlay_key(VK_ESCAPE) && !valid_overlay_key(VK_CONTROL),"key naming and capture restrictions");
        std::filesystem::remove_all(directory);
        std::cout<<"INI defaults, Unicode path, persistence, malformed values and failed atomic saves passed\n"; return 0;
    } catch(const std::exception& e) {
        std::error_code ec; std::filesystem::remove_all(directory,ec);
        std::cerr<<e.what()<<'\n'; return 1;
    }
}
