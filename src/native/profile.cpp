#include "native/profile.hpp"
#include <bcrypt.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>
namespace training::native {
static std::string hash_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
    DWORD n{}, got{};
    if (BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&n),sizeof(n),&got,0)<0) {
        BCryptCloseAlgorithmProvider(alg,0); return {};
    }
    std::vector<UCHAR> object(n);
    if (BCryptCreateHash(alg,&hash,object.data(),n,nullptr,0,0)<0) {
        BCryptCloseAlgorithmProvider(alg,0); return {};
    }
    bool ok=true;
    std::array<char,65536> block{};
    while (in) {
        in.read(block.data(),block.size());
        if (in.gcount() && BCryptHashData(hash,reinterpret_cast<PUCHAR>(block.data()),static_cast<ULONG>(in.gcount()),0)<0) { ok=false; break; }
    }
    if (in.bad()) ok=false;
    std::array<UCHAR,32> digest{};
    ok=ok && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    if (!ok) return {};
    std::string result;
    for (auto b:digest) { result += "0123456789abcdef"[b>>4]; result += "0123456789abcdef"[b&15]; }
    return result;
}
namespace {
Resolution active;
bool copy_readable(void* target,const void* source,std::size_t size) {
    __try { std::memcpy(target,source,size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool snapshot(HMODULE module,std::vector<std::byte>& image,std::string& reason) {
    MODULEINFO info{};
    if (!module || !K32GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info)) ||
        !info.SizeOfImage || info.SizeOfImage>512u*1024u*1024u) {
        reason="Cannot inspect mapped game module"; return false;
    }
    image.resize(info.SizeOfImage);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    std::size_t offset{};
    while (offset<image.size()) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(base+offset),&region,sizeof(region)) ||
            region.AllocationBase!=module) { reason="Game image mapping changed"; return false; }
        const auto end=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
        if (end<=base+offset) { reason="Invalid game image region"; return false; }
        const auto count=std::min(image.size()-offset,static_cast<std::size_t>(end-base-offset));
        const auto access=region.Protect&0xff;
        if (region.State==MEM_COMMIT && !(region.Protect&PAGE_GUARD) &&
            (access==PAGE_READONLY || access==PAGE_READWRITE || access==PAGE_WRITECOPY ||
             access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY)) {
            if (!copy_readable(image.data()+offset,reinterpret_cast<void*>(base+offset),count)) {
                reason="Cannot snapshot readable game image"; return false;
            }
        }
        offset+=count;
    }
    return true;
}
}
const Addresses& addresses() { return active.addresses; }
const Resolution& resolution() { return active; }
const WorldLayout& world_layout() { return active.world; }
const ResultLayout& result_layout() { return active.result; }
bool validate(HMODULE module,std::string& reason) {
    active={};
    std::vector<std::byte> image;
    if (!snapshot(module,image,reason)) return false;
    std::string diagnostic;
    wchar_t path[32768]{};
    if (GetModuleFileNameW(module,path,32768)) {
        const auto hash=hash_file(path);
        if (!hash.empty()) diagnostic="; diagnostic SHA-256="+hash;
    }
    Resolution resolved;
    if(!resolve_image(image,reinterpret_cast<std::uintptr_t>(module),resolved,reason)) { reason+=diagnostic; return false; }
    active=std::move(resolved); reason+=diagnostic; return true;
}
}
