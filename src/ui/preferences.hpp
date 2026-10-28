#pragma once
#include "ui/i18n.hpp"
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <string>

namespace training {
struct OverlayHotkey {
    unsigned key{VK_INSERT};
    bool ctrl{},alt{},shift{};
    bool operator==(const OverlayHotkey&) const = default;
};
struct Preferences {
    float loop_before{1.f},loop_after{1.f};
    OverlayHotkey hotkey;
    float background_opacity{.90f};
    int chart_column_measures{4};
    bool follow_playback{true};
    i18n::Language language{i18n::Language::English};
    bool operator==(const Preferences&) const = default;
};
bool valid_overlay_key(unsigned key);
std::string hotkey_name(const OverlayHotkey& key);
// Disk access happens only at startup or on an edit, never during playback/polling.
// A failed save leaves both the previous file and active preferences intact.
class PreferencesStore {
public:
    bool load(const std::filesystem::path& path,std::string& error);
    bool save(const Preferences& value,std::string& error);
    Preferences snapshot() const;
private:
    bool write(const Preferences& value,std::string& error);
    mutable std::mutex mutex_;
    Preferences value_;
    std::filesystem::path path_;
};
PreferencesStore& preferences();
bool initialize_preferences(HMODULE module,std::string& error);
}
