#pragma once

#include <cstdint>

namespace ffv1 {

enum class CpuFeature : std::uint64_t {
    Sse2 = 1ull << 0,
    Ssse3 = 1ull << 1,
    Avx2 = 1ull << 2,
    Neon = 1ull << 16,
};

struct CpuFeatures {
    std::uint64_t allowed = 0;
    bool auto_detect = true;
};

enum class EntropyMode : std::uint8_t {
    GolombRice = 0,
    Range = 1,
};

struct DecoderOptions {
    int thread_count = 0;
    bool verify_crc = true;
    bool strict = true;
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    CpuFeatures cpu = {};
};

struct EncoderOptions {
    int thread_count = 0;
    int version = 3;
    EntropyMode entropy_mode = EntropyMode::Range;
    CpuFeatures cpu = {};
};

} // namespace ffv1
