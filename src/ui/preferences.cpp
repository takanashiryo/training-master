#include "ui/preferences.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>

namespace training {
namespace {
std::string trim(std::string_view text) {
    const auto a=text.find_first_not_of(" \t\r\n");
    return a==text.npos?std::string{}:std::string(text.substr(a,text.find_last_not_of(" \t\r\n")-a+1));
}
std::string lower(std::string text) {
    for(auto& c:text) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return text;
}
bool number(std::string_view text,float& value) {
    float parsed{};
    const auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || !std::isfinite(parsed) || parsed<0) return false;
    value=parsed; return true;
}
bool boolean(std::string_view text,bool& value) {
    if(text!="0" && text!="1") return false;
    value=text=="1"; return true;
}
std::string key_token(unsigned key) {
    if(key==VK_INSERT) return "Insert";
    if((key>='A' && key<='Z') || (key>='0' && key<='9')) return std::string(1,static_cast<char>(key));
    if(key>=VK_F1 && key<=VK_F24) return "F"+std::to_string(key-VK_F1+1);
    std::ostringstream out; out<<"0x"<<std::uppercase<<std::hex<<key; return out.str();
}
bool parse_key(std::string text,unsigned& value) {
    text=lower(trim(text));
    unsigned key{};
    if(text=="insert") key=VK_INSERT;
    else if(text.size()==1 && text[0]>='a' && text[0]<='z') key=static_cast<unsigned>(text[0]-'a'+'A');
    else if(text.size()==1 && text[0]>='0' && text[0]<='9') key=static_cast<unsigned>(text[0]);
    else {
        const bool function=text.starts_with('f');
        const bool hex=text.starts_with("0x");
        std::string_view digits=text;
        if(function) digits.remove_prefix(1);
        if(hex) digits.remove_prefix(2);
        const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),key,hex?16:10);
        if(result.ec!=std::errc{} || result.ptr!=digits.data()+digits.size()) return false;
        if(function) { if(key<1 || key>24) return false; key+=VK_F1-1; }
    }
    if(!valid_overlay_key(key)) return false;
    value=key; return true;
}
bool valid(const Preferences& p) {
    return std::isfinite(p.loop_before) && p.loop_before>=0 && std::isfinite(p.loop_after) && p.loop_after>=0 &&
        std::isfinite(p.background_opacity) && p.background_opacity>=0 && p.background_opacity<=1 &&
        p.chart_column_measures>=1 && p.chart_column_measures<=12 &&
        (p.language==i18n::Language::English || p.language==i18n::Language::Chinese) && valid_overlay_key(p.hotkey.key);
}
}
bool valid_overlay_key(unsigned key) {
    return key==VK_BACK || key==VK_TAB || key==VK_RETURN || key==VK_PAUSE || key==VK_CAPITAL ||
        (key>=VK_SPACE && key<=VK_DOWN) || (key>=VK_SNAPSHOT && key<=VK_HELP) ||
        (key>='0' && key<='9') || (key>='A' && key<='Z') || (key>=VK_NUMPAD0 && key<=VK_DIVIDE) ||
        (key>=VK_F1 && key<=VK_F24) || key==VK_NUMLOCK || key==VK_SCROLL ||
        (key>=VK_OEM_1 && key<=VK_OEM_3) || (key>=VK_OEM_4 && key<=VK_OEM_8) || key==VK_OEM_102;
}
std::string hotkey_name(const OverlayHotkey& key) {
    std::string result;
    if(key.ctrl) result+="Ctrl + ";
    if(key.alt) result+="Alt + ";
    if(key.shift) result+="Shift + ";
    const auto scan=MapVirtualKeyW(key.key,MAPVK_VK_TO_VSC);
    LONG bits=static_cast<LONG>(scan<<16);
    if(key.key==VK_INSERT || key.key==VK_DELETE || (key.key>=VK_PRIOR && key.key<=VK_DOWN) || key.key==VK_NUMLOCK || key.key==VK_DIVIDE) bits|=1<<24;
    wchar_t name[128]{};
    if(GetKeyNameTextW(bits,name,128)>0) {
        const auto count=WideCharToMultiByte(CP_UTF8,0,name,-1,nullptr,0,nullptr,nullptr);
        std::string utf8(static_cast<std::size_t>(count),0);
        if(count>0) { WideCharToMultiByte(CP_UTF8,0,name,-1,utf8.data(),count,nullptr,nullptr); utf8.pop_back(); result+=utf8; }
        else result+=key_token(key.key);
    } else result+=key_token(key.key);
    return result;
}
Preferences PreferencesStore::snapshot() const { std::lock_guard lock(mutex_); return value_; }
bool PreferencesStore::load(const std::filesystem::path& path,std::string& error) {
    std::lock_guard lock(mutex_); error.clear(); path_=path; value_={};
    std::error_code ec;
    const bool exists=std::filesystem::exists(path_,ec);
    if(ec) { error="Cannot inspect trainingmaster.ini"; return false; }
    if(!exists) return write(value_,error);
    std::ifstream input(path_,std::ios::binary);
    if(!input) { error="Cannot read trainingmaster.ini"; return false; }
    std::string line,section; bool first=true,ok=true;
    while(std::getline(input,line)) {
        if(first && line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
        first=false;
        const auto comment=line.find_first_of(";#"); if(comment!=line.npos) line.resize(comment);
        line=trim(line); if(line.empty()) continue;
        if(line.front()=='[' && line.back()==']') { section=lower(trim(std::string_view(line).substr(1,line.size()-2))); continue; }
        const auto split=line.find('='); if(split==line.npos) continue;
        const auto key=lower(trim(std::string_view(line).substr(0,split)));
        const auto text=trim(std::string_view(line).substr(split+1)); bool field=true;
        if(section=="loop" && key=="startbufferseconds") field=number(text,value_.loop_before);
        else if(section=="loop" && key=="endbufferseconds") field=number(text,value_.loop_after);
        else if(section=="overlay" && key=="togglekey") field=parse_key(text,value_.hotkey.key);
        else if(section=="overlay" && key=="ctrl") field=boolean(text,value_.hotkey.ctrl);
        else if(section=="overlay" && key=="alt") field=boolean(text,value_.hotkey.alt);
        else if(section=="overlay" && key=="shift") field=boolean(text,value_.hotkey.shift);
        else if(section=="overlay" && key=="language") {
            const auto language=lower(text);
            field=language=="en" || language=="zh-cn";
            if(field) value_.language=language=="en"?i18n::Language::English:i18n::Language::Chinese;
        }
        else if(section=="overlay" && key=="backgroundopacity") {
            float opacity=value_.background_opacity;
            field=number(text,opacity) && opacity<=1;
            if(field) value_.background_opacity=opacity;
        }
        else if(section=="chartview" && key=="defaultcolumnmeasures") {
            unsigned measures{};
            const auto result=std::from_chars(text.data(),text.data()+text.size(),measures);
            field=result.ec==std::errc{} && result.ptr==text.data()+text.size() && measures>=1 && measures<=12;
            if(field) value_.chart_column_measures=static_cast<int>(measures);
        }
        else if(section=="chartview" && key=="followplayback") field=boolean(text,value_.follow_playback);
        if(!field) { ok=false; error="Invalid INI setting: "+section+"."+key+" (using default/previous valid value)"; }
    }
    if(input.bad()) { error="Cannot finish reading trainingmaster.ini"; return false; }
    return ok;
}
bool PreferencesStore::write(const Preferences& value,std::string& error) {
    if(path_.empty()) { error="Configuration path is not initialized"; return false; }
    auto temporary=path_; temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out.imbue(std::locale::classic());
        out<<std::setprecision(std::numeric_limits<float>::max_digits10)
           <<"; Training Master preferences. Changes in Option are saved immediately.\n"
           <<"[Loop]\nStartBufferSeconds="<<value.loop_before<<"\nEndBufferSeconds="<<value.loop_after
           <<"\n\n[Overlay]\n; ToggleKey: Insert, A-Z, 0-9, F1-F24 or a Windows virtual-key code (0xNN).\nToggleKey="
           <<key_token(value.hotkey.key)<<"\nCtrl="<<value.hotkey.ctrl<<"\nAlt="<<value.hotkey.alt<<"\nShift="<<value.hotkey.shift
           <<"\nLanguage="<<(value.language==i18n::Language::Chinese?"zh-CN":"en")
           <<"\nBackgroundOpacity="<<value.background_opacity
           <<"\n\n[ChartView]\nDefaultColumnMeasures="<<value.chart_column_measures
           <<"\nFollowPlayback="<<value.follow_playback<<'\n';
        out.flush();
        if(!out) { out.close(); DeleteFileW(temporary.c_str()); error="Cannot write trainingmaster.ini"; return false; }
    }
    if(!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        const auto code=GetLastError(); DeleteFileW(temporary.c_str());
        error="Cannot save trainingmaster.ini (Windows error "+std::to_string(code)+")"; return false;
    }
    return true;
}
bool PreferencesStore::save(const Preferences& value,std::string& error) {
    std::lock_guard lock(mutex_); error.clear();
    if(!valid(value)) { error="Buffers must be non-negative, opacity must be 0-1, column count must be 1-12 measures; choose a valid keyboard key"; return false; }
    if(!write(value,error)) return false;
    value_=value; return true;
}
PreferencesStore& preferences() { static PreferencesStore store; return store; }
bool initialize_preferences(HMODULE module,std::string& error) {
    wchar_t path[32768]{};
    const auto size=GetModuleFileNameW(module,path,32768);
    if(!size || size>=32768) { error="Cannot locate trainingmaster.ini"; return false; }
    return preferences().load(std::filesystem::path(path).parent_path()/L"trainingmaster.ini",error);
}
}
