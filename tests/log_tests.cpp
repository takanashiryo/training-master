#include "platform/log.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* failure) {
    if (!value) throw std::runtime_error(failure);
}

void write_fixture() {
    using training::LogLevel;
    training::open_log(nullptr);
    training::log(LogLevel::Info,"ordinary fixture message");
    training::log(LogLevel::Warn,"warning fixture message");
    training::log(LogLevel::Error,"error fixture message");
    training::log(LogLevel::Debug,"debug fixture message");
    training::debug_log("formatted fixture value=%d",23);
}

void launch(const std::filesystem::path& executable,const wchar_t* arguments) {
    auto command=std::wstring(arguments);
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
        nullptr,nullptr,&startup,&process)!=FALSE,"could not launch logger fixture");
    CloseHandle(process.hThread);
    const auto wait=WaitForSingleObject(process.hProcess,5000);
    DWORD exit_code{};
    const bool exited=wait==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&exit_code);
    CloseHandle(process.hProcess);
    check(exited && exit_code==0,"logger fixture did not exit successfully");
}

struct Fixture {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/
        (L"training-log-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::path executable=directory/L"log fixture.exe";
    std::filesystem::path log=directory/L"trainingmaster.log";
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove(log,error);
        std::filesystem::remove(executable,error);
        std::filesystem::remove(directory,error);
    }
};
}

int main(int argc,char** argv) {
    if (argc>1 && std::string_view(argv[1])=="--write-log") { write_fixture(); return 0; }
    try {
        Fixture fixture;
        wchar_t self[32768]{};
        const auto length=GetModuleFileNameW(nullptr,self,32768);
        check(length && length<32768,"could not locate the logger test executable");
        std::filesystem::create_directory(fixture.directory);
        std::filesystem::copy_file(self,fixture.executable);
        const struct { const wchar_t* command; bool debug; } cases[]{
            {L"\"log fixture.exe\" --write-log --debug",true},
            {L"\"log fixture.exe\" --write-log",false},
            {L"\"log fixture.exe\" --write-log \"--debug\"",true},
            {L"\"log fixture.exe\" --write-log --debugger",false},
            {L"\"log fixture.exe\" --write-log --debug=1",false},
            {L"\"log fixture.exe\" --write-log \"a path with --debug inside\"",false},
            {L"--debug --write-log",false}
        };
        for (const auto& scenario:cases) {
            launch(fixture.executable,scenario.command);
            std::ifstream input(fixture.log);
            check(input.is_open(),"logger fixture did not create the log file");
            const std::string contents{std::istreambuf_iterator<char>(input),{}};
            check(contents.find("[INFO] ordinary fixture message")!=contents.npos,"INFO was filtered");
            check(contents.find("[WARN] warning fixture message")!=contents.npos,"WARN was filtered");
            check(contents.find("[ERROR] error fixture message")!=contents.npos,"ERROR was filtered");
            check((contents.find("[DEBUG] debug fixture message")!=contents.npos)==scenario.debug,"unexpected DEBUG filtering");
            check((contents.find("[DEBUG] formatted fixture value=23")!=contents.npos)==scenario.debug,"unexpected formatted DEBUG filtering");
        }
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
