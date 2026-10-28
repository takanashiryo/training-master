#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace training {
struct SongInfo {
    std::string title, artist, difficulty;
    std::filesystem::path jacket;
};
// Value-only catalog: load on the startup thread, then read without COM/game pointers.
class SongCatalog {
public:
    bool load(const std::filesystem::path& contents, std::string& error);
    std::shared_ptr<const SongInfo> lookup(std::uint32_t id, std::uint32_t difficulty,
                                         const std::string& chart) const;
    std::size_t size() const { return entries_.size(); }
private:
    struct Entry {
        std::string title, artist, folder;
        unsigned infinite_version{};
        std::array<unsigned,6> levels{};
    };
    std::filesystem::path root_;
    std::unordered_map<std::uint32_t,Entry> entries_;
    bool tenths_{};
};
std::string difficulty_name(std::uint32_t difficulty, unsigned infinite_version=0);
struct JacketPixels {
    unsigned width{}, height{};
    std::vector<std::uint8_t> bgra;
};
// Bounded PNG decode, scaled to at most 256px. Missing/corrupt covers return empty.
JacketPixels load_jacket(const std::filesystem::path& path);
}
