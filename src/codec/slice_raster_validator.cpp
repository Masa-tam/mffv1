#include "codec/slice_raster_validator.hpp"

#include "codec/version3_constraints.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <vector>

namespace mffv1::codec {

namespace {

Status make_slice_error(ErrorCode code, const char* message, std::uint32_t slice_index)
{
    Status status = make_error(code, message);
    set_slice_location_if_missing(status, slice_index);
    return status;
}

struct SweepEvent {
    std::uint32_t x = 0;
    std::size_t slice_position = 0;
    bool starts = false;
};

struct ActiveInterval {
    std::uint32_t end = 0;
};

} // namespace

Status validate_slice_raster_coverage(const syntax::StreamParameters& stream,
                                      std::span<const syntax::SliceDescriptor> slices)
{
    if (stream.num_h_slices == 0 || stream.num_v_slices == 0) {
        return make_error(ErrorCode::InvalidState, "slice grid dimensions must be non-zero");
    }

    const std::uint64_t cell_count64 =
        static_cast<std::uint64_t>(stream.num_h_slices) * static_cast<std::uint64_t>(stream.num_v_slices);
    const bool enforce_parallel_slice_area =
        requires_version3_parallel_slice_limit(
            stream.version, stream.width, stream.height);
    const std::uint64_t maximum_slice_area = cell_count64 / 4;
    std::uint64_t covered_area = 0;
    bool covered_area_exceeds_raster = false;
    std::vector<SweepEvent> events;
    if (slices.size() > events.max_size() / 2) {
        return make_error(ErrorCode::ResourceExhausted, "too many slices to validate raster coverage");
    }
    events.reserve(slices.size() * 2);

    for (std::size_t position = 0; position < slices.size(); ++position) {
        const auto& slice = slices[position];
        if (slice.raster_width == 0 || slice.raster_height == 0) {
            return make_slice_error(ErrorCode::SyntaxError,
                                    "slice raster dimensions must be non-zero",
                                    slice.index);
        }
        if (slice.raster_x > stream.num_h_slices || slice.raster_y > stream.num_v_slices
            || slice.raster_width > stream.num_h_slices - slice.raster_x
            || slice.raster_height > stream.num_v_slices - slice.raster_y) {
            return make_slice_error(ErrorCode::SyntaxError,
                                    "slice raster rectangle is outside the frame raster",
                                    slice.index);
        }
        const std::uint64_t slice_area =
            static_cast<std::uint64_t>(slice.raster_width) * slice.raster_height;
        if (enforce_parallel_slice_area && slice_area > maximum_slice_area) {
            return make_slice_error(ErrorCode::SyntaxError,
                                    "slice raster area exceeds the version 3 parallel decoding limit",
                                    slice.index);
        }
        if (slice_area > cell_count64 - covered_area) {
            covered_area_exceeds_raster = true;
        } else {
            covered_area += slice_area;
        }
        events.push_back({slice.raster_x, position, true});
        events.push_back({slice.raster_x + slice.raster_width, position, false});
    }

    std::sort(events.begin(), events.end(), [](const SweepEvent& left, const SweepEvent& right) {
        if (left.x != right.x) {
            return left.x < right.x;
        }
        if (left.starts != right.starts) {
            return !left.starts;
        }
        return left.slice_position < right.slice_position;
    });

    std::map<std::uint32_t, ActiveInterval> active;
    for (const auto& event : events) {
        const auto& slice = slices[event.slice_position];
        if (!event.starts) {
            active.erase(slice.raster_y);
            continue;
        }

        const auto y_end = slice.raster_y + slice.raster_height;
        const auto next = active.lower_bound(slice.raster_y);
        if ((next != active.end() && next->first < y_end)
            || (next != active.begin() && std::prev(next)->second.end > slice.raster_y)) {
            return make_slice_error(ErrorCode::SyntaxError,
                                    "slice raster rectangles overlap",
                                    slice.index);
        }
        active.emplace(slice.raster_y, ActiveInterval{y_end});
    }

    if (covered_area_exceeds_raster || covered_area != cell_count64) {
        return make_error(ErrorCode::SyntaxError, "slice raster coverage has missing cells");
    }

    return ok_status();
}

bool is_incomplete_slice_raster_coverage_status(const Status& status) noexcept
{
    return status.code == ErrorCode::SyntaxError
        && !status.location.has_byte_offset
        && !status.location.has_frame_index
        && !status.location.has_slice_index;
}

} // namespace mffv1::codec
