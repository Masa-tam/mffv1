#include "codec/slice_output_window.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ffv1::codec {

namespace {

std::uint32_t plane_x(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return slice.x >> stream.log2_h_chroma_subsample;
    }
    return slice.x;
}

std::uint32_t plane_y(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return slice.y >> stream.log2_v_chroma_subsample;
    }
    return slice.y;
}

std::uint32_t slice_plane_width(const syntax::StreamParameters& stream,
                                const syntax::SliceDescriptor& slice,
                                std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return syntax::subsampled_extent(slice.width, stream.log2_h_chroma_subsample);
    }
    return slice.width;
}

std::uint32_t slice_plane_height(const syntax::StreamParameters& stream,
                                 const syntax::SliceDescriptor& slice,
                                 std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        return syntax::subsampled_extent(slice.height, stream.log2_v_chroma_subsample);
    }
    return slice.height;
}

std::uint32_t bytes_per_sample(SampleFormat format) noexcept
{
    return format == SampleFormat::UInt16 ? 2u : 1u;
}

} // namespace

Status SliceOutputWindow::validate(const syntax::StreamParameters& stream,
                                   MutableFrameView frame,
                                   const syntax::SliceDescriptor& slice)
{
    if (slice.width == 0 || slice.height == 0) {
        return make_error(ErrorCode::InvalidArgument, "slice dimensions must be non-zero");
    }
    if (slice.x > stream.width || slice.y > stream.height
        || slice.width > stream.width - slice.x
        || slice.height > stream.height - slice.y) {
        return make_error(ErrorCode::InvalidArgument, "slice rectangle is outside the frame");
    }

    const std::size_t required_planes = syntax::coded_plane_count(stream);
    if (frame.plane_count < required_planes) {
        return make_error(ErrorCode::InvalidArgument, "output frame does not have enough planes");
    }
    if (required_planes != 0 && frame.planes == nullptr) {
        return make_error(ErrorCode::InvalidArgument, "output plane array is null");
    }

    planes_.clear();
    planes_.reserve(required_planes);

    for (std::size_t i = 0; i < required_planes; ++i) {
        const auto& plane = frame.planes[i];
        if (plane.data == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "output plane data pointer is null");
        }
        if (plane.info.role != syntax::expected_plane_role(stream, i)) {
            return make_error(ErrorCode::InvalidArgument, "output plane role does not match stream plane order");
        }

        const std::uint32_t px = plane_x(stream, slice, i);
        const std::uint32_t py = plane_y(stream, slice, i);
        const std::uint32_t pw = slice_plane_width(stream, slice, i);
        const std::uint32_t ph = slice_plane_height(stream, slice, i);
        const std::uint32_t frame_width = syntax::plane_width(stream, i);
        const std::uint32_t frame_height = syntax::plane_height(stream, i);

        if (plane.info.width < frame_width || plane.info.height < frame_height) {
            return make_error(ErrorCode::InvalidArgument, "output plane dimensions are smaller than the frame plane");
        }
        if (px > plane.info.width || py > plane.info.height
            || pw > plane.info.width - px
            || ph > plane.info.height - py) {
            return make_error(ErrorCode::InvalidArgument, "slice plane rectangle is outside output plane");
        }

        const std::ptrdiff_t min_stride =
            static_cast<std::ptrdiff_t>(plane.info.width * bytes_per_sample(plane.info.sample_format));
        if (plane.info.stride_bytes < min_stride) {
            return make_error(ErrorCode::InvalidArgument, "output plane stride is too small");
        }

        const auto row_offset = static_cast<std::ptrdiff_t>(py) * plane.info.stride_bytes;
        const auto column_offset =
            static_cast<std::ptrdiff_t>(px * bytes_per_sample(plane.info.sample_format));
        auto* base = static_cast<std::byte*>(plane.data);

        PlaneWindow window;
        window.data = base + row_offset + column_offset;
        window.stride_bytes = plane.info.stride_bytes;
        window.width = pw;
        window.height = ph;
        window.sample_format = plane.info.sample_format;
        planes_.push_back(window);
    }

    return ok_status();
}

std::size_t SliceOutputWindow::plane_count() const noexcept
{
    return planes_.size();
}

std::uint32_t SliceOutputWindow::plane_width(std::size_t plane_index) const noexcept
{
    return plane_index < planes_.size() ? planes_[plane_index].width : 0;
}

std::uint32_t SliceOutputWindow::plane_height(std::size_t plane_index) const noexcept
{
    return plane_index < planes_.size() ? planes_[plane_index].height : 0;
}

std::uint8_t* SliceOutputWindow::row_u8(std::size_t plane_index, std::uint32_t y) const noexcept
{
    if (plane_index >= planes_.size() || y >= planes_[plane_index].height
        || planes_[plane_index].sample_format != SampleFormat::UInt8) {
        return nullptr;
    }
    const auto& plane = planes_[plane_index];
    auto* base = static_cast<std::byte*>(plane.data);
    return reinterpret_cast<std::uint8_t*>(base + static_cast<std::ptrdiff_t>(y) * plane.stride_bytes);
}

std::uint16_t* SliceOutputWindow::row_u16(std::size_t plane_index, std::uint32_t y) const noexcept
{
    if (plane_index >= planes_.size() || y >= planes_[plane_index].height
        || planes_[plane_index].sample_format != SampleFormat::UInt16) {
        return nullptr;
    }
    const auto& plane = planes_[plane_index];
    auto* base = static_cast<std::byte*>(plane.data);
    return reinterpret_cast<std::uint16_t*>(base + static_cast<std::ptrdiff_t>(y) * plane.stride_bytes);
}

} // namespace ffv1::codec
