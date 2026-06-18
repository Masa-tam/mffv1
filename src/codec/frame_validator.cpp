#include "codec/frame_validator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ffv1::codec {

namespace {

Status validate_stream_shape(const syntax::StreamParameters& stream)
{
    if (stream.width == 0 || stream.height == 0) {
        return make_error(ErrorCode::InvalidArgument, "stream dimensions must be non-zero");
    }
    if (stream.bits_per_raw_sample == 0 || stream.bits_per_raw_sample > 16) {
        return make_error(ErrorCode::UnsupportedFeature, "only 1-16 bit samples are supported");
    }
    if (stream.colorspace_type < 0 || stream.colorspace_type > 1) {
        return make_error(ErrorCode::UnsupportedFeature, "unsupported colorspace_type");
    }
    if (stream.colorspace_type == 1
        && (!stream.chroma_planes
            || stream.log2_h_chroma_subsample != 0
            || stream.log2_v_chroma_subsample != 0)) {
        return make_error(ErrorCode::InvalidArgument,
                          "RGB streams require chroma planes without subsampling");
    }
    if (stream.num_h_slices == 0 || stream.num_v_slices == 0) {
        return make_error(ErrorCode::InvalidArgument, "slice grid dimensions must be non-zero");
    }
    return ok_status();
}

SampleFormat expected_sample_format(const syntax::StreamParameters& stream) noexcept
{
    return stream.bits_per_raw_sample <= 8 ? SampleFormat::UInt8 : SampleFormat::UInt16;
}

Status minimum_stride_bytes(const syntax::StreamParameters& stream,
                            std::size_t plane_index,
                            std::ptrdiff_t& out_stride)
{
    const std::uint64_t bytes_per_sample = stream.bits_per_raw_sample <= 8 ? 1u : 2u;
    const auto required = static_cast<std::uint64_t>(syntax::plane_width(stream, plane_index))
        * bytes_per_sample;
    if (required > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        return make_error(ErrorCode::ResourceExhausted, "plane row size exceeds ptrdiff_t");
    }
    out_stride = static_cast<std::ptrdiff_t>(required);
    return ok_status();
}

Status validate_plane_info(const syntax::StreamParameters& stream,
                           const PlaneInfo& info,
                           std::size_t plane_index)
{
    if (info.role != syntax::expected_plane_role(stream, plane_index)) {
        return make_error(ErrorCode::InvalidArgument, "plane role does not match stream plane order");
    }
    if (info.sample_format != expected_sample_format(stream)) {
        return make_error(ErrorCode::InvalidArgument, "plane sample format does not match stream bit depth");
    }
    if (info.width < syntax::plane_width(stream, plane_index)
        || info.height < syntax::plane_height(stream, plane_index)) {
        return make_error(ErrorCode::InvalidArgument, "plane dimensions are smaller than the stream requires");
    }
    std::ptrdiff_t minimum_stride = 0;
    Status status = minimum_stride_bytes(stream, plane_index, minimum_stride);
    if (!status.ok()) {
        return status;
    }
    if (info.stride_bytes < minimum_stride) {
        return make_error(ErrorCode::InvalidArgument, "plane stride is smaller than the stream requires");
    }
    return ok_status();
}

} // namespace

Status FrameValidator::validate_output(const syntax::StreamParameters& stream,
                                       MutableFrameView output) const
{
    Status status = validate_stream_shape(stream);
    if (!status.ok()) {
        return status;
    }

    const std::size_t required_planes = syntax::coded_plane_count(stream);
    if (output.plane_count < required_planes) {
        return make_error(ErrorCode::InvalidArgument, "output frame does not have enough planes");
    }
    if (required_planes != 0 && output.planes == nullptr) {
        return make_error(ErrorCode::InvalidArgument, "output plane array is null");
    }

    for (std::size_t i = 0; i < required_planes; ++i) {
        if (output.planes[i].data == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "output plane data pointer is null");
        }
        status = validate_plane_info(stream, output.planes[i].info, i);
        if (!status.ok()) {
            return status;
        }
    }

    return ok_status();
}

Status FrameValidator::validate_input(const syntax::StreamParameters& stream,
                                      FrameView input) const
{
    Status status = validate_stream_shape(stream);
    if (!status.ok()) {
        return status;
    }

    const std::size_t required_planes = syntax::coded_plane_count(stream);
    if (input.plane_count < required_planes) {
        return make_error(ErrorCode::InvalidArgument, "input frame does not have enough planes");
    }
    if (required_planes != 0 && input.planes == nullptr) {
        return make_error(ErrorCode::InvalidArgument, "input plane array is null");
    }

    for (std::size_t i = 0; i < required_planes; ++i) {
        if (input.planes[i].data == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "input plane data pointer is null");
        }
        status = validate_plane_info(stream, input.planes[i].info, i);
        if (!status.ok()) {
            return status;
        }
    }

    return ok_status();
}

} // namespace ffv1::codec
