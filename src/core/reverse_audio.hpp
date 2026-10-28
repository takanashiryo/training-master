#pragma once
#include "core/atomic_shared_ptr.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace training {
struct PcmClip {
    static constexpr unsigned sample_rate=44100, channels=2;
    std::vector<float> samples;
    double duration() const { return static_cast<double>(samples.size()/channels)/sample_rate; }
};
std::shared_ptr<const PcmClip> decode_reverse_clip(const std::filesystem::path&,std::string& error,std::stop_token stop={});
// Worker owns the decoder; completed samples are immutable. No decoding in game/audio callbacks.
struct ReverseAsset {
    AtomicSharedPtr<const PcmClip> clip;
    std::atomic<bool> finished{};
    std::string error; // written before finished.store(release), then immutable
    std::jthread worker; // declared last: stops/joins before other members are destroyed
    explicit ReverseAsset(const std::filesystem::path&);
    explicit ReverseAsset(std::shared_ptr<const PcmClip> value):clip(std::move(value)),finished(true){}
};
std::filesystem::path reverse_audio_path(const std::filesystem::path& contents,const std::string& virtual_path);
// One game-thread command producer, one audio-thread renderer. Commands allocate only on producer.
class ReversePlayer {
public:
    void start(std::shared_ptr<const PcmClip>,double seconds,float rate,float gain=1.f);
    void seek(double seconds);
    void pause(bool value);
    void rate(float value);
    void stop();
    bool owns_bus() const { return command_.load()!=nullptr; }
    double position() const;
    // Interleaved stereo frames. Returns true even while paused (silence replaces native music).
    bool render(std::span<float> output);
private:
    struct Command { std::shared_ptr<const PcmClip> clip; double seconds{}; float rate{1},gain{1}; bool paused{}; std::uint64_t epoch{}; };
    void publish(Command);
    AtomicSharedPtr<const Command> command_;
    std::uint64_t next_epoch_{}; // producer only
    std::shared_ptr<const Command> active_; // audio thread only
    double cursor_{};
    std::atomic<double> position_{};
    std::atomic<std::uint64_t> consumed_epoch_{};
};
}
