#include "codec/slice_input_window.hpp"

#include "mffv1/sample_format.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace mffv1::codec {

namespace {

std::uint32_t plane_x(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return syntax::scaled_grid_position(
            syntax::plane_width(stream, plane_index),
            stream.num_h_slices,
            slice.raster_x);
    }
    return slice.x;
}

std::uint32_t plane_y(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return syntax::scaled_grid_position(
            syntax::plane_height(stream, plane_index),
            stream.num_v_slices,
            slice.raster_y);
    }
    return slice.y;
}

std::uint32_t plane_extent(std::uint32_t frame_extent,
                           std::uint32_t grid_extent,
                           std::uint32_t position,
                           std::uint32_t length) noexcept
{
    const auto start =
        syntax::scaled_grid_position(frame_extent, grid_extent, position);
    const auto end = syntax::scaled_grid_position(
        frame_extent,
        grid_extent,
        static_cast<std::uint64_t>(position) + length);
    return end - start;
}

std::uint32_t slice_plane_width(const syntax::StreamParameters& stream,
                                const syntax::SliceDescriptor& slice,
                                std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return plane_extent(
            syntax::plane_width(stream, plane_index),
            stream.num_h_slices,
            slice.raster_x,
            slice.raster_width);
    }
    return slice.width;
}

std::uint32_t slice_plane_height(const syntax::StreamParameters& stream,
                                 const syntax::SliceDescriptor& slice,
                                 std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return plane_extent(
            syntax::plane_height(stream, plane_index),
            stream.num_v_slices,
            slice.raster_y,
            slice.raster_height);
    }
    return slice.height;
}

} // namespace

Status SliceInputWindow::validate(const syntax::StreamParameters& stream,
                                  FrameView frame,
                                  const syntax::SliceDescriptor& slice)
{
    if (slice.width == 0 || slice.height == 0
        || slice.raster_width == 0 || slice.raster_height == 0) {
        return make_error(
            ErrorCode::InvalidArgument,
            "input slice dimensions must be non-zero");
    }

    const auto required_planes =
        static_cast<std::size_t>(syntax::coded_plane_count(stream));
    if (frame.plane_count != required_planes
        || (required_planes != 0 && frame.planes == nullptr)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "input frame plane count does not match the stream");
    }

    std::vector<PlaneWindow> windows;
    windows.reserve(required_planes);
    const auto expected_format =
        samples::sample_format_for_bit_depth(stream.bits_per_raw_sample);
    const auto maximum_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());

    for (std::size_t index = 0; index < required_planes; ++index) {
        const auto& plane = frame.planes[index];
        if (plane.data == nullptr
            || plane.info.role != syntax::expected_plane_role(stream, index)
            || plane.info.sample_format != expected_format) {
            return make_error(
                ErrorCode::InvalidArgument,
                "input slice plane does not match the stream");
        }

        const auto frame_width = syntax::plane_width(stream, index);
        const auto frame_height = syntax::plane_height(stream, index);
        if (plane.info.width != frame_width
            || plane.info.height != frame_height
            || plane.info.stride_bytes < 0) {
            return make_error(
                ErrorCode::InvalidArgument,
                "input plane geometry does not match the stream");
        }

        const auto x = plane_x(stream, slice, index);
        const auto y = plane_y(stream, slice, index);
        const auto width = slice_plane_width(stream, slice, index);
        const auto height = slice_plane_height(stream, slice, index);
        if (width == 0 || height == 0
            || x > frame_width || y > frame_height
            || width > frame_width - x || height > frame_height - y) {
            return make_error(
                ErrorCode::InvalidArgument,
                "input slice plane rectangle is outside the frame");
        }

        const auto sample_bytes =
            static_cast<std::uint64_t>(samples::bytes_per_sample(expected_format));
        const auto row_bytes =
            static_cast<std::uint64_t>(frame_width) * sample_bytes;
        const auto stride =
            static_cast<std::uint64_t>(plane.info.stride_bytes);
        if (row_bytes > maximum_offset || stride < row_bytes
            || (y != 0 && stride > maximum_offset / y)) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "input slice plane offset is not representable");
        }
        const auto row_offset = stride * y;
        const auto column_offset =
            static_cast<std::uint64_t>(x) * sample_bytes;
        if (column_offset > maximum_offset - row_offset) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "input slice plane offset is not representable");
        }
        const auto last_y =
            static_cast<std::uint64_t>(y) + height - 1;
        if (last_y != 0 && stride > maximum_offset / last_y) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "input slice plane rows are not representable");
        }
        const auto last_row_offset = stride * last_y;
        const auto last_x =
            static_cast<std::uint64_t>(x) + width - 1;
        const auto last_column_offset = last_x * sample_bytes;
        if (last_column_offset > maximum_offset - last_row_offset) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "input slice plane extent is not representable");
        }

        windows.push_back({
            static_cast<const std::byte*>(plane.data)
                + static_cast<std::ptrdiff_t>(row_offset + column_offset),
            plane.info.stride_bytes,
            width,
            height,
            expected_format,
        });
    }

    planes_ = std::move(windows);
    return ok_status();
}

std::size_t SliceInputWindow::plane_count() const noexcept
{
    return planes_.size();
}

std::uint32_t SliceInputWindow::plane_width(std::size_t plane_index) const noexcept
{
    return plane_index < planes_.size() ? planes_[plane_index].width : 0;
}

std::uint32_t SliceInputWindow::plane_height(std::size_t plane_index) const noexcept
{
    return plane_index < planes_.size() ? planes_[plane_index].height : 0;
}

const std::uint8_t* SliceInputWindow::row_u8(
    std::size_t plane_index,
    std::uint32_t y) const noexcept
{
    if (plane_index >= planes_.size()
        || y >= planes_[plane_index].height
        || planes_[plane_index].sample_format != SampleFormat::UInt8) {
        return nullptr;
    }
    const auto& plane = planes_[plane_index];
    const auto* base = static_cast<const std::byte*>(plane.data);
    return reinterpret_cast<const std::uint8_t*>(
        base + static_cast<std::ptrdiff_t>(y) * plane.stride_bytes);
}

const std::uint16_t* SliceInputWindow::row_u16(
    std::size_t plane_index,
    std::uint32_t y) const noexcept
{
    if (plane_index >= planes_.size()
        || y >= planes_[plane_index].height
        || planes_[plane_index].sample_format != SampleFormat::UInt16) {
        return nullptr;
    }
    const auto& plane = planes_[plane_index];
    const auto* base = static_cast<const std::byte*>(plane.data);
    return reinterpret_cast<const std::uint16_t*>(
        base + static_cast<std::ptrdiff_t>(y) * plane.stride_bytes);
}

} // namespace mffv1::codec
