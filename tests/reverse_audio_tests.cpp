#include "core/reverse_audio.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace {
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
void close(double a,double b,const char* text) { check(std::fabs(a-b)<.00001,text); }
}
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc==2) {
            std::string error; const auto clip=training::decode_reverse_clip(argv[1],error);
            if (!clip) throw std::runtime_error(error);
            check(clip->duration()>110 && clip->duration()<125,"reference song decoded duration mismatch");
            training::ReversePlayer player; player.start(clip,60,1);
            std::vector<float> reversed(2048); player.render(reversed);
            const auto source=60*44100;
            for (std::size_t i=0;i<reversed.size()/2;++i) for (unsigned channel=0;channel<2;++channel)
                close(reversed[2*i+channel],clip->samples[(source-i-1)*2+channel],"actual song PCM is not reversed frame by frame");
            std::cout<<"Real S3V decoded "<<clip->duration()<<" s, reverse PCM matches source exactly\n";
            return 0;
        }
        auto clip=std::make_shared<training::PcmClip>(); clip->samples={1,10,2,20,3,30,4,40};
        training::ReversePlayer player; std::vector<float> out(8,-1);
        check(!player.render(out),"inactive reverse player must leave native bus untouched");
        player.start(clip,4./44100,1);
        check(player.render(out),"reverse player must own music bus");
        check(out==std::vector<float>({4,40,3,30,2,20,1,10}),"must reverse frames and retain L/R channel order");
        close(player.position(),0,"reverse must stop at zero"); player.render(out);
        check(out==std::vector<float>(8,0),"must silence before song start");
        player.seek(3./44100); close(player.position(),3./44100,"pending seek must publish new cursor immediately");
        player.pause(true); player.render(out); check(out==std::vector<float>(8,0),"pause must silence reverse music");
        close(player.position(),3./44100,"pause must freeze cursor");
        player.pause(false); player.rate(.5f); player.render(out);
        close(out[0],3.5,"half rate must interpolate reverse samples"); close(out[1],35,"half rate must preserve channels");
        close(player.position(),1./44100,"half rate must move half as far");
        player.seek(4./44100); player.rate(.25f); player.render(out);
        close(player.position(),3./44100,"quarter rate must move quarter as far");
        player.start(clip,4./44100,1,.5f); player.render(out); close(out[0],2,"song gain must be applied");
        player.stop(); check(!player.owns_bus() && !player.render(out),"stop must restore native bus");
        const auto root=std::filesystem::temp_directory_path();
        check(training::reverse_audio_path(root,"/data/music/song/music.s3v")==root/L"data/music/song/music.s3v","virtual song path resolution failed");
        check(training::reverse_audio_path(root,"/data/music/../../secret.s3v").empty(),"audio path traversal must be rejected");
        check(training::reverse_audio_path(root,"/data/music/song/music.png").empty(),"non-song resource must be rejected");
        std::string error;
        check(!training::decode_reverse_clip(root/L"missing-training-audio.s3v",error) && !error.empty(),"missing audio must fail cleanly");
        std::stop_source stopped; stopped.request_stop();
        // Deterministic PCM16 WAV exercises the production decoder and timestamp placement.
        const auto path=root/(L"training-reverse-"+std::to_wstring(GetCurrentProcessId())+L".wav");
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path,ec); } } cleanup{path};
        std::ofstream wave(path,std::ios::binary);
        const auto u16=[&](std::uint16_t v){wave.write(reinterpret_cast<const char*>(&v),2);};
        const auto u32=[&](std::uint32_t v){wave.write(reinterpret_cast<const char*>(&v),4);};
        wave.write("RIFF",4); u32(36+44100*4); wave.write("WAVEfmt ",8); u32(16); u16(1); u16(2);
        u32(44100); u32(44100*4); u16(4); u16(16); wave.write("data",4); u32(44100*4);
        for (int i=0;i<44100;++i) { u16(static_cast<std::uint16_t>(i%16000)); u16(static_cast<std::uint16_t>(-(i%16000))); }
        wave.close();
        const auto decoded=training::decode_reverse_clip(path,error);
        check(decoded!=nullptr,"PCM WAV decode failed"); close(decoded->duration(),1,"decoded timestamps must preserve duration");
        close(decoded->samples[200],100.f/32768,"decoder sample mismatch"); close(decoded->samples[201],-100.f/32768,"decoder channel mismatch");
        check(!training::decode_reverse_clip(path,error,stopped.get_token()),"cancelled decode must not publish partial audio");
        training::ReverseAsset asset(path); asset.worker.join();
        check(asset.finished.load() && asset.clip.load()!=nullptr && asset.error.empty(),"background decoder must publish a complete ready asset");
        std::cout<<"Reverse stereo playback, pause, seek, rates, gain, decoder and resource scenarios passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
