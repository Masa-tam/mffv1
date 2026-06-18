#include "codec/slice_raster_validator.hpp"

#include "util/status.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ffv1::codec {

namespace {

constexpr std::uint64_t kCifPixelCount = 352u * 288u;

Status make_slice_error(ErrorCode code, const char* message, std::uint32_t slice_index)
{
    Status status = make_error(code, message);
    set_slice_location_if_missing(status, slice_index);
    return status;
}

} // namespace

Status validate_slice_raster_coverage(const syntax::StreamParameters& stream,
                                      std::span<const syntax::SliceDescriptor> slices)
{
    if (stream.num_h_slices == 0 || stream.num_v_slices == 0) {
        return make_error(ErrorCode::InvalidState, "slice grid dimensions must be non-zero");
    }

    const std::uint64_t cell_count64 =
        static_cast<std::uint64_t>(stream.num_h_slices) * static_cast<std::uint64_t>(stream.num_v_slices);
    if (cell_count64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return make_error(ErrorCode::ResourceExhausted, "slice grid is too large to validate");
    }

    std::vector<bool> covered(static_cast<std::size_t>(cell_count64), false);
    std::size_t covered_count = 0;
    const std::uint64_t frame_pixel_count =
        static_cast<std::uint64_t>(stream.width) * static_cast<std::uint64_t>(stream.height);
    const bool enforce_parallel_slice_area = stream.version >= 3 && frame_pixel_count > kCifPixelCount;
    const std::uint64_t maximum_slice_area = cell_count64 / 4;

    for (const auto& slice : slices) {
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

        for (std::uint32_t y = 0; y < slice.raster_height; ++y) {
            const std::uint64_t row =
                static_cast<std::uint64_t>(slice.raster_y + y) * stream.num_h_slices;
            for (std::uint32_t x = 0; x < slice.raster_width; ++x) {
                const auto cell =
                    static_cast<std::size_t>(row + static_cast<std::uint64_t>(slice.raster_x + x));
                if (covered[cell]) {
                    return make_slice_error(ErrorCode::SyntaxError,
                                            "slice raster rectangles overlap",
                                            slice.index);
                }
                covered[cell] = true;
                ++covered_count;
            }
        }
    }

    if (covered_count != covered.size()) {
        return make_error(ErrorCode::SyntaxError, "slice raster coverage has missing cells");
    }

    return ok_status();
}

} // namespace ffv1::codec
