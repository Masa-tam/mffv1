#include "entropy/golomb_rice_run.hpp"

#include "util/status.hpp"

#include <array>
#include <cstdint>

namespace mffv1::entropy {

namespace {

constexpr std::array<std::uint8_t, 41> kLog2Run{
    0, 0, 0, 0, 1, 1, 1, 1,
    2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24,
};

} // namespace

void GolombRiceRunState::reset() noexcept
{
    run_index = 0;
}

Status read_golomb_rice_run_segment(bitstream::BitReader& reader,
                                    GolombRiceRunState& state,
                                    std::uint32_t x,
                                    std::uint32_t width,
                                    GolombRiceRunSegment& out_segment) noexcept
{
    if (state.run_index >= kLog2Run.size()) {
        return make_error(ErrorCode::InvalidState, "Golomb-Rice run index is out of range");
    }
    if (x > width) {
        return make_error(ErrorCode::InvalidArgument, "Golomb-Rice run position is outside the row");
    }

    const auto log2_run = kLog2Run[state.run_index];
    std::uint8_t prefix = 0;
    Status status = reader.read_bit(prefix);
    if (!status.ok()) {
        return status;
    }

    GolombRiceRunSegment segment;
    auto next_run_index = state.run_index;
    if (prefix != 0) {
        segment.count = std::uint32_t{1} << log2_run;
        if (segment.count <= width - x) {
            if (next_run_index + 1 >= kLog2Run.size()) {
                return make_error(ErrorCode::ResourceExhausted,
                                  "Golomb-Rice run index exceeds the supported table");
            }
            ++next_run_index;
        }
    } else {
        std::uint64_t remainder = 0;
        status = reader.read_bits(log2_run, remainder);
        if (!status.ok()) {
            return status;
        }
        segment.count = static_cast<std::uint32_t>(remainder);
        segment.interrupted = true;
        if (next_run_index != 0) {
            --next_run_index;
        }
    }

    state.run_index = next_run_index;
    out_segment = segment;
    return ok_status();
}

} // namespace mffv1::entropy
