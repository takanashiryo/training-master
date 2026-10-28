#pragma once
#include <windows.h>
#include <string_view>
namespace training { void open_log(HMODULE module); void log(std::string_view line); }
