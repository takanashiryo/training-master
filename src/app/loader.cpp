#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

struct Handle {
    HANDLE value{};
    ~Handle(){ if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::optional<MODULEENTRY32W> find_module(DWORD pid,const std::wstring& name) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
    if(snapshot.value==INVALID_HANDLE_VALUE) return {};
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    for(BOOL ok=Module32FirstW(snapshot.value,&entry);ok;ok=Module32NextW(snapshot.value,&entry))
        if(_wcsicmp(entry.szModule,name.c_str())==0) return entry;
    return {};
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) { std::wcerr<<L"Usage: training_loader.exe <game PID> <path to trainingmaster.dll>\n"; return 1; }
    wchar_t* tail{};
    const auto pid=wcstoul(argv[1],&tail,10);
    if(!pid || !tail || *tail) { std::wcerr<<L"Invalid PID\n"; return 1; }
    std::error_code error;
    const auto path=std::filesystem::canonical(argv[2],error);
    if(error || !std::filesystem::is_regular_file(path)) { std::wcerr<<L"DLL does not exist\n"; return 1; }
    if(!find_module(pid,L"soundvoltex.dll")) { std::wcerr<<L"Target has no loaded soundvoltex.dll. Wait until game startup completes.\n"; return 2; }
    if(find_module(pid,path.filename().wstring()) || find_module(pid,L"trainingmaster.dll") || find_module(pid,L"sdvx_training.dll")) {
        std::wcerr<<L"Training Master is already loaded. Restart the game to replace or rename it.\n"; return 2;
    }
    Handle process{OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid)};
    if(!process.value) { std::wcerr<<L"OpenProcess failed: "<<GetLastError()<<L'\n'; return 3; }
    USHORT machine{},native{};
    if(!IsWow64Process2(process.value,&machine,&native) || machine!=IMAGE_FILE_MACHINE_UNKNOWN || native!=IMAGE_FILE_MACHINE_AMD64) {
        std::wcerr<<L"Target must be a native Windows x64 process\n"; return 3;
    }
    const auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE owner{};
    if(!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(load),&owner)) return 4;
    wchar_t module_name[32768]{}; GetModuleFileNameW(owner,module_name,32768);
    const auto remote_owner=find_module(pid,std::filesystem::path(module_name).filename().wstring());
    if(!remote_owner) { std::wcerr<<L"Could not resolve target loader module\n"; return 4; }
    const auto offset=reinterpret_cast<std::uintptr_t>(load)-reinterpret_cast<std::uintptr_t>(owner);
    const auto remote_load=reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<std::uintptr_t>(remote_owner->modBaseAddr)+offset);
    const auto text=path.wstring(); const auto bytes=(text.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(process.value,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote) return 5;
    SIZE_T written{};
    if(!WriteProcessMemory(process.value,remote,text.c_str(),bytes,&written) || written!=bytes) {
        VirtualFreeEx(process.value,remote,0,MEM_RELEASE); return 5;
    }
    Handle thread{CreateRemoteThread(process.value,nullptr,0,remote_load,remote,0,nullptr)};
    if(!thread.value) { VirtualFreeEx(process.value,remote,0,MEM_RELEASE); return 6; }
    if(WaitForSingleObject(thread.value,30000)!=WAIT_OBJECT_0) {
        // The loader may still read this buffer; do not free it on timeout.
        std::wcerr<<L"LoadLibrary is still pending. Check the game and log; do not retry blindly.\n"; return 7;
    }
    VirtualFreeEx(process.value,remote,0,MEM_RELEASE);
    if(!find_module(pid,path.filename().wstring())) { std::wcerr<<L"LoadLibrary failed. Check architecture and DLL dependencies.\n"; return 8; }
    std::wcout<<L"DLL loaded. Check trainingmaster.log next to it for hook readiness. Press Insert in the game.\n";
    return 0;
}
