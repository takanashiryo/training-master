#include "platform/log.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
namespace training {
static std::mutex log_mutex;
static std::ofstream file;
void open_log(HMODULE module) {
    wchar_t name[32768]{};
    GetModuleFileNameW(module, name, 32768);
    // Keep only this launch; subsequent log() calls write sequentially to this stream.
    file.open(std::filesystem::path(name).parent_path() / "trainingmaster.log", std::ios::out | std::ios::trunc);
}
void log(std::string_view line) {
    std::lock_guard lock(log_mutex);
    SYSTEMTIME t{}; GetLocalTime(&t);
    char prefix[64]{};
    wsprintfA(prefix, "[%04u-%02u-%02u %02u:%02u:%02u] ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    OutputDebugStringA((std::string(prefix)+std::string(line)+"\n").c_str());
    if (file) { file << prefix << line << '\n'; file.flush(); }
}
}
