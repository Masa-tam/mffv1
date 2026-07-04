#include "entropy/golomb_rice_run.hpp"

#include "util/status.hpp"

#include <algorithm>
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
    pending_count = 0;
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
    if (state.pending_count != 0) {
        const auto row_remaining = width - x;
        GolombRiceRunSegment segment;
        segment.count = std::min(state.pending_count, row_remaining);
        state.pending_count -= segment.count;
        out_segment = segment;
        return ok_status();
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
        } else {
            segment.count = width - x;
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
        const auto row_remaining = width - x;
        if (segment.count > row_remaining) {
            state.pending_count = segment.count - row_remaining;
            segment.count = row_remaining;
            segment.interrupted = false;
        }
    }

    state.run_index = next_run_index;
    out_segment = segment;
    return ok_status();
}

Status write_golomb_rice_run(bitstream::BitWriter& writer,
                             GolombRiceRunState& state,
                             std::uint32_t x,
                             std::uint32_t width,
                             std::uint32_t count)
{
    if (state.run_index >= kLog2Run.size()) {
        return make_error(ErrorCode::InvalidState,
                          "Golomb-Rice run index is out of range");
    }
    if (x > width || count > width - x) {
        return make_error(ErrorCode::InvalidArgument,
                          "Golomb-Rice run is outside the row");
    }

    auto next_run_index = state.run_index;
    auto position = x;
    auto remaining = count;
    while (true) {
        const auto log2_run = kLog2Run[next_run_index];
        const auto full_count = std::uint32_t{1} << log2_run;
        if (remaining >= full_count) {
            if (next_run_index + 1 >= kLog2Run.size()) {
                return make_error(
                    ErrorCode::ResourceExhausted,
                    "Golomb-Rice run index exceeds the supported table");
            }
            Status status = writer.write_bit(1);
            if (!status.ok()) {
                return status;
            }
            remaining -= full_count;
            position += full_count;
            ++next_run_index;
            if (position == width) {
                state.run_index = next_run_index;
                return ok_status();
            }
            continue;
        }

        Status status = writer.write_bit(0);
        if (!status.ok()) {
            return status;
        }
        status = writer.write_bits(remaining, log2_run);
        if (!status.ok()) {
            return status;
        }
        if (next_run_index != 0) {
            --next_run_index;
        }
        state.run_index = next_run_index;
        return ok_status();
    }
}

} // namespace mffv1::entropy
