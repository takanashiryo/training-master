#pragma once
#include <string>
#include <string_view>

namespace training::i18n {
enum class Language { English, Chinese };
struct Translation { const char* english; const char* chinese; const char* english_label; const char* chinese_label; };
// Stable English IDs keep menus, windows and controls intact across languages.
#define TM_TEXT(en,zh) Translation{en,zh,en "###" en,zh "###" en}
inline constexpr Translation catalog[]{
    TM_TEXT("Judgment counter unavailable","判定计数器不可用"),
    TM_TEXT("Judgment counter","判定计数器"),
    TM_TEXT("Always show judgment counter","始终显示判定计数器"),
    TM_TEXT("Option","选项"),
    TM_TEXT("About","关于"),
    TM_TEXT("Training Master","训练大师"),
    TM_TEXT("About Training Master","关于训练大师"),
    TM_TEXT("Interface","界面"),
    TM_TEXT("Keyboard shortcut","快捷键"),
    TM_TEXT("Language","语言"),
    TM_TEXT("Close","关闭"),
    TM_TEXT("Chart View","谱面预览"),
    TM_TEXT("M %.0f - %.0f","小节 %.0f - %.0f"),
    TM_TEXT("M %.0f","小节 %.0f"),
    TM_TEXT("Musical timing unavailable","缺少小节与拍数信息"),
    TM_TEXT("Invalid musical tempo map","BPM 时间映射无效"),
    TM_TEXT("Invalid musical line map","小节或拍线数据无效"),
    TM_TEXT("Default measures per column","默认每列小节数"),
    TM_TEXT("%d measures / column","每列 %d 小节"),
    TM_TEXT("Follow playback","跟随播放"),
    TM_TEXT("Background opacity","背景不透明度"),
    TM_TEXT("Loop buffer","循环缓冲"),
    TM_TEXT("Start buffer (s)","开始缓冲（秒）"),
    TM_TEXT("End buffer (s)","结束缓冲（秒）"),
    TM_TEXT("0 = no buffer. Clamped to the track.","设为 0 以关闭缓冲区，限制在选区内"),
    TM_TEXT("Overlay hotkey","界面快捷键"),
    TM_TEXT("Press a key... (Esc cancels)","请按下快捷键…（Esc 取消）"),
    TM_TEXT("Ctrl / Alt / Shift combinations supported.","支持 Ctrl / Alt / Shift 组合键。"),
    TM_TEXT("Third-party Licenses","第三方许可证"),
    TM_TEXT("Build date %s","构建日期 %s"),
    TM_TEXT("Music ID","曲目 ID"),
    TM_TEXT("Title","曲名"),
    TM_TEXT("Artist","艺术家"),
    TM_TEXT("Difficulty","难度"),
    TM_TEXT("NO COVER","暂无封面"),
    TM_TEXT("Enable training for this track","为本曲开启训练"),
    TM_TEXT("Use Finish Immediately to exit training.","请使用“立即结束”退出训练。"),
    TM_TEXT("Unavailable: unsupported mode or missing native context. Load plugin before selecting a song. Check log.","当前不可用：模式不受支持或缺少游戏上下文。请在选曲前加载插件，并检查日志。"),
    TM_TEXT("Transport","播放控制"),
    TM_TEXT("Playback rate","播放倍率"),
    TM_TEXT("Setting","设置"),
    TM_TEXT("AutoPlay","自动演奏"),
    TM_TEXT("Hide song visuals (Experimental)","隐藏歌曲演出（实验性）"),
    TM_TEXT("Experimental feature. Some effects cannot be removed.","实验性功能，部分效果无法被移除"),
    TM_TEXT("Hide the song's special visual effects.","隐藏歌曲的特殊演出效果"),
    TM_TEXT("Song visual control is unavailable for this track","此曲目的歌曲演出控制不可用"),
    TM_TEXT("All","全部"),
    TM_TEXT("Buttons only","仅按钮"),
    TM_TEXT("Knobs only","仅旋钮"),
    TM_TEXT("Fix Lane Speed","固定谱面流速"),
    TM_TEXT("Block camera movement","固定镜头"),
    TM_TEXT("Gauge###gauge-enabled","血条"),
    TM_TEXT("Override gauge","覆盖血条设置"),
    TM_TEXT("Initial Gauge: %.1f%%","初始血量：%.1f%%"),
    TM_TEXT("Native AutoPlay is unavailable.","原生自动演奏不可用。"),
    TM_TEXT("Finish Immediately","立即结束"),
    TM_TEXT("Hook entry not found!","未找到 Hook 入口！"),
    TM_TEXT("Choose a track to start training.","请选择一首曲目以开始训练。"),
    TM_TEXT("Loop off (click: repeat selection)","循环已关闭（点击：持续循环）"),
    TM_TEXT("Repeat selection (click: loop once)","持续循环选区（点击：循环一次）"),
    TM_TEXT("Loop once, then pause (click: off)","循环一次后暂停（点击：关闭）"),
    TM_TEXT("Reverse playback","倒放"),
    TM_TEXT("Pause","暂停"),
    TM_TEXT("Play forward","正向播放"),
    TM_TEXT("Preparing reverse audio...","正在准备倒放音频…"),
    TM_TEXT("Reverse audio unavailable%s%s","倒放音频不可用%s%s"),
    TM_TEXT("Drag to move selection","拖动以移动选区"),
    TM_TEXT("Drag to resize selection","拖动以调整选区范围"),
    TM_TEXT("The chart appears when a track starts.","进入曲目后显示谱面。"),
    TM_TEXT("Chart preview unavailable: %s","谱面预览不可用：%s"),
    TM_TEXT("no chart snapshot","尚未获取谱面"),
    TM_TEXT("Fit","适应"),
    TM_TEXT("Double-click: Left = IN / Right = OUT.","双击：左键标记 IN / 右键标记 OUT。"),
    TM_TEXT("NOW","当前"),
    TM_TEXT("%.1f - %.1f s","%.1f - %.1f 秒"),
    TM_TEXT("%.1fs","%.1f秒"),
    TM_TEXT("Track is not supported","不支持此曲目"),
    TM_TEXT("Waiting for gameplay","等待进入游玩"),
    TM_TEXT("Camera control is unavailable for this track","此曲目的镜头控制不可用"),
    TM_TEXT("Lane Speed control is unavailable for this track","此曲目的流速控制不可用"),
    TM_TEXT("Lane Speed control failed; paused. Results remain blocked.","流速控制失败，已暂停，成绩仍被阻止。"),
    TM_TEXT("Gauge return failed; paused. Results remain blocked.","血量归零回转失败，已暂停，成绩仍被阻止。"),
    TM_TEXT("Track end control failed; paused. Use Finish Immediately.","曲终控制失败，已暂停，请使用“立即结束”。"),
    TM_TEXT("Track end pause failed. Use Finish Immediately.","曲终暂停失败，请使用“立即结束”。"),
    TM_TEXT("Native AutoPlay is unavailable for this track","此曲目的原生自动演奏不可用"),
    TM_TEXT("Required game hooks are unavailable","所需游戏 Hook 不可用"),
    TM_TEXT("Reverse audio is loading or unavailable","倒放音频正在加载或不可用"),
    TM_TEXT("IN must be at least 50 ms before OUT","IN 必须比 OUT 提前至少 50 毫秒"),
    TM_TEXT("OUT must follow IN and stay inside the track","OUT 必须晚于 IN，且位于曲目范围内"),
    TM_TEXT("Region must stay inside the track and be at least 50 ms long","选区必须位于曲目范围内，且长度不少于 50 毫秒"),
    TM_TEXT("Rate must be between 0.25 and 2.00","倍率必须介于 0.25 与 2.00 之间"),
    TM_TEXT("Invalid game clock","游戏时钟无效"),
    TM_TEXT("Transport failed; paused. Results remain blocked. Use Finish Immediately.","播放控制失败，已暂停，成绩仍被阻止。请使用“立即结束”。"),
    TM_TEXT("Cannot inspect trainingmaster.ini","无法检查 trainingmaster.ini"),
    TM_TEXT("Cannot read trainingmaster.ini","无法读取 trainingmaster.ini"),
    TM_TEXT("Cannot finish reading trainingmaster.ini","无法完成读取 trainingmaster.ini"),
    TM_TEXT("Configuration path is not initialized","配置路径尚未初始化"),
    TM_TEXT("Cannot write trainingmaster.ini","无法写入 trainingmaster.ini"),
    TM_TEXT("Cannot locate trainingmaster.ini","无法定位 trainingmaster.ini"),
    TM_TEXT("Buffers must be non-negative, opacity must be 0-1, column count must be 1-12 measures; choose a valid keyboard key","缓冲须为非负数，不透明度须为 0–1，每列须为 1–12 小节，并选择有效的键盘按键"),
    TM_TEXT("Cannot read chart array","无法读取谱面数组"),
    TM_TEXT("Invalid chart array bounds","谱面数组范围无效"),
    TM_TEXT("Cannot copy chart objects","无法复制谱面对象"),
    TM_TEXT("Native chart is unavailable","原生谱面不可用"),
    TM_TEXT("Invalid note time or lane","音符时间或轨道无效"),
    TM_TEXT("Invalid laser side or width","旋钮侧别或宽度无效"),
    TM_TEXT("Invalid laser point bounds","旋钮节点范围无效"),
    TM_TEXT("Cannot copy laser points","无法复制旋钮节点"),
    TM_TEXT("Invalid laser control point","旋钮控制点无效"),
    TM_TEXT("Invalid measure time","小节时间无效"),
};
#undef TM_TEXT
inline thread_local Language current_language=Language::English;
inline void set_language(Language language) { current_language=language; }
inline const Translation* find(std::string_view english) {
    for(const auto& entry:catalog) if(english==entry.english) return &entry;
    return nullptr;
}
inline const char* text(const char* english) {
    if(current_language==Language::Chinese) if(const auto* entry=find(english)) return entry->chinese;
    return english;
}
inline const char* label(const char* english) {
    if(const auto* entry=find(english)) return current_language==Language::Chinese?entry->chinese_label:entry->english_label;
    return english;
}
inline std::string message(const std::string& english) {
    constexpr std::string_view prefix="Cannot save trainingmaster.ini (Windows error ";
    if(current_language==Language::Chinese && english.starts_with(prefix))
        return "无法保存 trainingmaster.ini（Windows 错误 "+english.substr(prefix.size(),english.size()-prefix.size()-1)+"）";
    return text(english.c_str());
}
}
