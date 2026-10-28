#include "core/reverse_audio.hpp"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace training {
namespace {
using Microsoft::WRL::ComPtr;
struct Runtime {
    HRESULT com{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    HRESULT mf{(SUCCEEDED(com)||com==RPC_E_CHANGED_MODE)?MFStartup(MF_VERSION,MFSTARTUP_FULL):com};
    ~Runtime() { if (SUCCEEDED(mf)) MFShutdown(); if (SUCCEEDED(com)) CoUninitialize(); }
};
void require(HRESULT hr,const char* operation) {
    if (FAILED(hr)) { char text[128]{}; sprintf_s(text,"%s (0x%08lX)",operation,static_cast<unsigned long>(hr)); throw std::runtime_error(text); }
}
}
std::shared_ptr<const PcmClip> decode_reverse_clip(const std::filesystem::path& path,std::string& error,std::stop_token stop) {
    error.clear();
    try {
        std::error_code ec;
        const auto bytes=std::filesystem::file_size(path,ec);
        if (ec || bytes>128*1024*1024) throw std::runtime_error("Song audio missing or exceeds 128 MB");
        Runtime runtime; require(runtime.mf,"Media Foundation startup");
        ComPtr<IMFSourceReader> reader;
        require(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader),"Open song audio");
        constexpr auto all_streams=static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
        constexpr auto audio_stream=static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        require(reader->SetStreamSelection(all_streams,FALSE),"Select audio stream");
        require(reader->SetStreamSelection(audio_stream,TRUE),"Select first audio stream");
        ComPtr<IMFMediaType> type; require(MFCreateMediaType(&type),"Create PCM type");
        type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio); type->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);
        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2); type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,32); type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,8);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,44100*8);
        require(reader->SetCurrentMediaType(audio_stream,nullptr,type.Get()),"Decode stereo float PCM");
        ComPtr<IMFMediaType> actual; require(reader->GetCurrentMediaType(audio_stream,&actual),"Read PCM format");
        UINT32 channels{},rate{},bits{}; GUID subtype{};
        actual->GetGUID(MF_MT_SUBTYPE,&subtype); actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels);
        actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate); actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,&bits);
        if (subtype!=MFAudioFormat_Float || channels!=2 || rate!=44100 || bits!=32) throw std::runtime_error("Unsupported decoded PCM format");
        auto clip=std::make_shared<PcmClip>();
        constexpr std::size_t max_samples=44100*2*600; // bounded ten-minute song cache
        while (!stop.stop_requested()) {
            DWORD flags{}; LONGLONG timestamp{}; ComPtr<IMFSample> sample;
            require(reader->ReadSample(audio_stream,0,nullptr,&flags,&timestamp,&sample),"Read song PCM");
            if (flags & (MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) throw std::runtime_error("Song PCM stream changed or failed");
            if (sample) {
                ComPtr<IMFMediaBuffer> buffer; require(sample->ConvertToContiguousBuffer(&buffer),"Read PCM buffer");
                BYTE* data{}; DWORD length{}; require(buffer->Lock(&data,nullptr,&length),"Lock PCM buffer");
                // Preserve timestamps, including leading silence. Do not shift music relative to chart time.
                if (timestamp>600LL*10000000) { buffer->Unlock(); throw std::runtime_error("Song PCM timestamp exceeds cache limit"); }
                const auto frame=static_cast<std::size_t>((std::max<LONGLONG>(0,timestamp)*44100+5000000)/10000000);
                const auto offset=frame*2, count=static_cast<std::size_t>(length/sizeof(float));
                if (length%8 || offset>max_samples || count>max_samples-offset) {
                    buffer->Unlock(); throw std::runtime_error("Song PCM exceeds cache limit or has invalid frames");
                }
                try {
                    clip->samples.resize(std::max(clip->samples.size(),offset+count),0.f);
                    std::memcpy(clip->samples.data()+offset,data,length);
                } catch (...) { buffer->Unlock(); throw; }
                buffer->Unlock();
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        }
        if (stop.stop_requested()) throw std::runtime_error("Audio loading cancelled");
        if (clip->samples.empty()) throw std::runtime_error("Song audio contains no PCM frames");
        return clip;
    } catch (const std::exception& e) { error=e.what(); return {}; }
}
ReverseAsset::ReverseAsset(const std::filesystem::path& path):worker([this,path](std::stop_token stop) {
    clip.store(decode_reverse_clip(path,error,stop)); finished.store(true,std::memory_order_release);
}) {}
std::filesystem::path reverse_audio_path(const std::filesystem::path& contents,const std::string& virtual_path) {
    if (contents.empty() || (!virtual_path.starts_with("/data/music/") && !virtual_path.starts_with("/data/sound/automa/waves/"))) return {};
    try {
        const std::filesystem::path relative(std::u8string(reinterpret_cast<const char8_t*>(virtual_path.data()+1),virtual_path.size()-1));
        if (relative.has_root_path() || relative.extension()!=L".s3v") return {};
        for (const auto& part:relative) if (part==L".." || part==L".") return {};
        return contents/relative;
    } catch (const std::exception&) { return {}; }
}
void ReversePlayer::publish(Command value) {
    value.epoch=++next_epoch_;
    command_.store(std::make_shared<const Command>(std::move(value)));
}
void ReversePlayer::start(std::shared_ptr<const PcmClip> clip,double seconds,float rate,float gain) {
    if (!clip || clip->samples.empty()) return;
    publish({std::move(clip),std::max(0.,seconds),std::clamp(rate,.25f,1.f),std::clamp(gain,0.f,1.f)});
}
double ReversePlayer::position() const {
    const auto command=command_.load();
    if (!command) return 0.;
    if (consumed_epoch_.load(std::memory_order_acquire)!=command->epoch) return command->seconds;
    return position_.load();
}
void ReversePlayer::seek(double seconds) { if (const auto c=command_.load()) { auto next=*c; next.seconds=std::max(0.,seconds); publish(std::move(next)); } }
void ReversePlayer::pause(bool value) { if (const auto c=command_.load()) { auto next=*c; next.seconds=position(); next.paused=value; publish(std::move(next)); } }
void ReversePlayer::rate(float value) { if (const auto c=command_.load()) { auto next=*c; next.seconds=position(); next.rate=std::clamp(value,.25f,1.f); publish(std::move(next)); } }
void ReversePlayer::stop() { command_.store(nullptr); }
bool ReversePlayer::render(std::span<float> output) {
    const auto command=command_.load();
    if (!command) { active_.reset(); return false; }
    if (active_!=command) { active_=command; cursor_=command->seconds*PcmClip::sample_rate; }
    std::fill(output.begin(),output.end(),0.f);
    if (!command->paused) {
        const auto& samples=command->clip->samples;
        const auto frames=samples.size()/2;
        for (std::size_t frame=0;frame<output.size()/2;++frame) {
            if (cursor_<=0.) continue;
            cursor_=std::max(0.,cursor_-command->rate);
            if (cursor_>=static_cast<double>(frames)) continue;
            const auto a=static_cast<std::size_t>(cursor_),b=std::min(a+1,frames-1);
            const float mix=static_cast<float>(cursor_-static_cast<double>(a));
            for (unsigned channel=0;channel<2;++channel)
                output[frame*2+channel]=(samples[a*2+channel]*(1-mix)+samples[b*2+channel]*mix)*command->gain;
        }
    }
    position_.store(cursor_/PcmClip::sample_rate);
    consumed_epoch_.store(command->epoch,std::memory_order_release);
    return true;
}
}
