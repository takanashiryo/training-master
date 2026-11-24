#pragma once
#include <windows.h>
#include <string_view>
namespace training {
enum class LogLevel { Debug, Info, Warn, Error };
void open_log(HMODULE module);
bool debug_enabled() noexcept;
void log(LogLevel level,std::string_view line) noexcept;
void debug_log(const char* format,...) noexcept;
}
