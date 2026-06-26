#pragma once

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/sample_format.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace mffv1::codec {

[[nodiscard]] inline Status checked_plane_row_bytes(
    const PlaneInfo& info,
    std::uint64_t& out_row_bytes,
    std::string row_size_message)
{
    const auto maximum_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const auto sample_bytes =
        static_cast<std::uint64_t>(samples::bytes_per_sample(info.sample_format));
    out_row_bytes = static_cast<std::uint64_t>(info.width) * sample_bytes;
    if (out_row_bytes > maximum_offset) {
        return make_error(ErrorCode::ResourceExhausted, std::move(row_size_message));
    }
    return ok_status();
}

[[nodiscard]] inline Status checked_plane_window_offset(
    const PlaneInfo& info,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height,
    std::ptrdiff_t& out_offset,
    std::string row_offset_message,
    std::string sample_offset_message,
    std::string rows_message,
    std::string extent_message)
{
    const auto maximum_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const auto sample_bytes =
        static_cast<std::uint64_t>(samples::bytes_per_sample(info.sample_format));
    const auto stride = static_cast<std::uint64_t>(info.stride_bytes);
    if (y != 0 && stride > maximum_offset / y) {
        return make_error(ErrorCode::ResourceExhausted, std::move(row_offset_message));
    }
    const auto row_offset = stride * y;
    const auto column_offset = static_cast<std::uint64_t>(x) * sample_bytes;
    if (column_offset > maximum_offset - row_offset) {
        return make_error(ErrorCode::ResourceExhausted, std::move(sample_offset_message));
    }

    if (width != 0 && height != 0) {
        const auto last_y = static_cast<std::uint64_t>(y) + height - 1;
        if (last_y != 0 && stride > maximum_offset / last_y) {
            return make_error(ErrorCode::ResourceExhausted, std::move(rows_message));
        }
        const auto last_row_offset = stride * last_y;
        const auto last_x = static_cast<std::uint64_t>(x) + width - 1;
        const auto last_column_offset = last_x * sample_bytes;
        if (last_column_offset > maximum_offset - last_row_offset) {
            return make_error(ErrorCode::ResourceExhausted, std::move(extent_message));
        }
    }

    out_offset = static_cast<std::ptrdiff_t>(row_offset + column_offset);
    return ok_status();
}

} // namespace mffv1::codec
