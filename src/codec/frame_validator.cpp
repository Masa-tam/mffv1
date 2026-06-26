#include "codec/frame_validator.hpp"

#include "codec/frame_info_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace mffv1::codec {

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

Status validate_plane_info(const PlaneInfo& expected,
                           const PlaneInfo& info)
{
    if (info.role != expected.role) {
        return make_error(ErrorCode::InvalidArgument, "plane role does not match stream plane order");
    }
    if (info.sample_format != expected.sample_format) {
        return make_error(ErrorCode::InvalidArgument, "plane sample format does not match stream bit depth");
    }
    if (info.width < expected.width || info.height < expected.height) {
        return make_error(ErrorCode::InvalidArgument, "plane dimensions are smaller than the stream requires");
    }
    if (info.stride_bytes < expected.stride_bytes) {
        return make_error(ErrorCode::InvalidArgument, "plane stride is smaller than the stream requires");
    }
    return ok_status();
}

Status validate_input_plane_info(const PlaneInfo& expected,
                                 const PlaneInfo& info)
{
    if (info.role != expected.role) {
        return make_error(ErrorCode::InvalidArgument, "plane role does not match stream plane order");
    }
    if (info.sample_format != expected.sample_format) {
        return make_error(ErrorCode::InvalidArgument, "plane sample format does not match stream bit depth");
    }

    if (info.width != expected.width || info.height != expected.height) {
        return make_error(ErrorCode::InvalidArgument, "input plane dimensions do not match the stream");
    }
    if (info.stride_bytes < 0) {
        return make_error(ErrorCode::UnsupportedFeature, "negative input plane stride is not supported");
    }

    if (info.stride_bytes < expected.stride_bytes) {
        return make_error(ErrorCode::InvalidArgument, "plane stride is smaller than the stream requires");
    }

    const auto last_row = static_cast<std::uint64_t>(expected.height - 1);
    const auto stride = static_cast<std::uint64_t>(info.stride_bytes);
    const auto row_bytes = static_cast<std::uint64_t>(expected.stride_bytes);
    const auto maximum_offset =
        static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (row_bytes > maximum_offset
        || (last_row != 0 && stride > (maximum_offset - row_bytes) / last_row)) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "input plane last row address is not representable");
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

    const FrameInfo expected = make_frame_info(stream);
    const std::size_t required_planes = expected.plane_count;
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
        status = validate_plane_info(expected.planes[i], output.planes[i].info);
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

    const FrameInfo expected = make_frame_info(stream);
    const std::size_t required_planes = expected.plane_count;
    if (input.plane_count != required_planes) {
        return make_error(ErrorCode::InvalidArgument, "input frame plane count does not match the stream");
    }
    if (required_planes != 0 && input.planes == nullptr) {
        return make_error(ErrorCode::InvalidArgument, "input plane array is null");
    }

    for (std::size_t i = 0; i < required_planes; ++i) {
        if (input.planes[i].data == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "input plane data pointer is null");
        }
        status = validate_input_plane_info(expected.planes[i], input.planes[i].info);
        if (!status.ok()) {
            return status;
        }
    }

    return ok_status();
}

} // namespace mffv1::codec
