#include "codec/slice_output_window.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace mffv1::codec {

namespace {

std::uint32_t plane_x(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        if (slice.raster_width != 0) {
            return syntax::scaled_grid_position(syntax::plane_width(stream, plane_index),
                                                stream.num_h_slices,
                                                slice.raster_x);
        }
        return slice.x >> stream.log2_h_chroma_subsample;
    }
    return slice.x;
}

std::uint32_t plane_y(const syntax::StreamParameters& stream,
                      const syntax::SliceDescriptor& slice,
                      std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        if (slice.raster_height != 0) {
            return syntax::scaled_grid_position(syntax::plane_height(stream, plane_index),
                                                stream.num_v_slices,
                                                slice.raster_y);
        }
        return slice.y >> stream.log2_v_chroma_subsample;
    }
    return slice.y;
}

std::uint32_t slice_plane_width(const syntax::StreamParameters& stream,
                                const syntax::SliceDescriptor& slice,
                                std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        if (slice.raster_width != 0) {
            const auto start = syntax::scaled_grid_position(syntax::plane_width(stream, plane_index),
                                                            stream.num_h_slices,
                                                            slice.raster_x);
            const auto end = syntax::scaled_grid_position(
                syntax::plane_width(stream, plane_index),
                stream.num_h_slices,
                static_cast<std::uint64_t>(slice.raster_x) + slice.raster_width);
            return end - start;
        }
        return syntax::subsampled_extent(slice.width, stream.log2_h_chroma_subsample);
    }
    return slice.width;
}

std::uint32_t slice_plane_height(const syntax::StreamParameters& stream,
                                 const syntax::SliceDescriptor& slice,
                                 std::size_t plane_index) noexcept
{
    if (syntax::is_chroma_plane(stream, plane_index)) {
        if (slice.raster_height != 0) {
            const auto start = syntax::scaled_grid_position(syntax::plane_height(stream, plane_index),
                                                            stream.num_v_slices,
                                                            slice.raster_y);
            const auto end = syntax::scaled_grid_position(
                syntax::plane_height(stream, plane_index),
                stream.num_v_slices,
                static_cast<std::uint64_t>(slice.raster_y) + slice.raster_height);
            return end - start;
        }
        return syntax::subsampled_extent(slice.height, stream.log2_v_chroma_subsample);
    }
    return slice.height;
}

std::uint32_t bytes_per_sample(SampleFormat format) noexcept
{
    return format == SampleFormat::UInt16 ? 2u : 1u;
}

Status checked_plane_window_offset(const PlaneInfo& info,
                                   std::uint32_t x,
                                   std::uint32_t y,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::ptrdiff_t& out_offset)
{
    const auto max_offset = static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    const auto sample_bytes = static_cast<std::uint64_t>(bytes_per_sample(info.sample_format));
    const auto minimum_stride = static_cast<std::uint64_t>(info.width) * sample_bytes;
    if (minimum_stride > max_offset) {
        return make_error(ErrorCode::ResourceExhausted, "output plane row size exceeds ptrdiff_t");
    }

    const auto stride = static_cast<std::uint64_t>(info.stride_bytes);
    if (y != 0 && stride > max_offset / y) {
        return make_error(ErrorCode::ResourceExhausted, "output plane row offset exceeds ptrdiff_t");
    }
    const auto row_offset = stride * y;
    const auto column_offset = static_cast<std::uint64_t>(x) * sample_bytes;
    if (column_offset > max_offset - row_offset) {
        return make_error(ErrorCode::ResourceExhausted, "output plane sample offset exceeds ptrdiff_t");
    }

    if (width != 0 && height != 0) {
        const auto last_y = static_cast<std::uint64_t>(y) + height - 1;
        if (last_y != 0 && stride > max_offset / last_y) {
            return make_error(ErrorCode::ResourceExhausted, "output plane window rows exceed ptrdiff_t");
        }
        const auto last_row_offset = stride * last_y;
        const auto last_x = static_cast<std::uint64_t>(x) + width - 1;
        const auto last_column_offset = last_x * sample_bytes;
        if (last_column_offset > max_offset - last_row_offset) {
            return make_error(ErrorCode::ResourceExhausted, "output plane window exceeds ptrdiff_t");
        }
    }

    out_offset = static_cast<std::ptrdiff_t>(row_offset + column_offset);
    return ok_status();
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

        const auto sample_bytes = static_cast<std::uint64_t>(bytes_per_sample(plane.info.sample_format));
        const auto minimum_stride = static_cast<std::uint64_t>(plane.info.width) * sample_bytes;
        if (minimum_stride > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
            return make_error(ErrorCode::ResourceExhausted, "output plane row size exceeds ptrdiff_t");
        }
        if (plane.info.stride_bytes < static_cast<std::ptrdiff_t>(minimum_stride)) {
            return make_error(ErrorCode::InvalidArgument, "output plane stride is too small");
        }

        std::ptrdiff_t plane_offset = 0;
        Status status = checked_plane_window_offset(plane.info, px, py, pw, ph, plane_offset);
        if (!status.ok()) {
            return status;
        }
        auto* base = static_cast<std::byte*>(plane.data);

        PlaneWindow window;
        window.data = base + plane_offset;
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

} // namespace mffv1::codec
