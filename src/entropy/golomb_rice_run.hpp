#pragma once

#include "bitstream/bit_reader.hpp"
#include "bitstream/bit_writer.hpp"

#include <cstdint>

namespace mffv1::entropy {

struct GolombRiceRunState {
    std::uint8_t run_index = 0;

    void reset() noexcept;
};

struct GolombRiceRunSegment {
    std::uint32_t count = 0;
    bool interrupted = false;
};

Status read_golomb_rice_run_segment(bitstream::BitReader& reader,
                                    GolombRiceRunState& state,
                                    std::uint32_t x,
                                    std::uint32_t width,
                                    GolombRiceRunSegment& out_segment) noexcept;
Status write_golomb_rice_run(bitstream::BitWriter& writer,
                             GolombRiceRunState& state,
                             std::uint32_t x,
                             std::uint32_t width,
                             std::uint32_t count);

} // namespace mffv1::entropy
