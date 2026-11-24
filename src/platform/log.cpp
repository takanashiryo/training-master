#include "platform/log.hpp"
#include <shellapi.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace training {
namespace {
std::mutex log_mutex;
std::ofstream file;
std::atomic<bool> debug{};

bool debug_requested() {
    int count{};
    auto* arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    if (!arguments) return false;
    bool requested=false;
    for (int i=1;i<count;++i) {
        if (std::wstring_view(arguments[i])==L"--debug") { requested=true; break; }
    }
    LocalFree(arguments);
    return requested;
}
}
void open_log(HMODULE module) {
    debug.store(debug_requested(),std::memory_order_relaxed);
    wchar_t name[32768]{};
    const auto length=GetModuleFileNameW(module,name,32768);
    {
        std::lock_guard lock(log_mutex);
        if (length && length<32768)
            file.open(std::filesystem::path(name).parent_path()/"trainingmaster.log",std::ios::out|std::ios::trunc);
        if (file.is_open()) return;
    }
    log(LogLevel::Warn,"Could not open trainingmaster.log beside the module");
}
bool debug_enabled() noexcept { return debug.load(std::memory_order_relaxed); }
void log(LogLevel level,std::string_view line) noexcept {
    if (level==LogLevel::Debug && !debug_enabled()) return;
    constexpr const char* labels[]{"DEBUG","INFO","WARN","ERROR"};
    const auto last_error=GetLastError();
    try {
        std::lock_guard lock(log_mutex);
        SYSTEMTIME t{};
        GetLocalTime(&t);
        char prefix[64]{};
        std::snprintf(prefix,sizeof(prefix),"[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] ",
            t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,labels[static_cast<unsigned>(level)]);
        const auto message=std::string(prefix)+std::string(line)+'\n';
        OutputDebugStringA(message.c_str());
        if (file) { file << message; file.flush(); }
    } catch (...) {
        // Logging must not interrupt a game callback.
    }
    SetLastError(last_error);
}
void debug_log(const char* format,...) noexcept {
    if (!debug_enabled()) return;
    char line[1024]{};
    va_list args;
    va_start(args,format);
    const auto length=std::vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if (length>=0) log(LogLevel::Debug,line);
}
}
