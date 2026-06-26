#include "codec/frame_validator.hpp"

#include "codec/frame_info_builder.hpp"
#include "codec/plane_window_math.hpp"
#include "mffv1/profile_constraints.hpp"

#include <cstddef>
#include <cstdint>

namespace mffv1::codec {

namespace {

Status validate_stream_shape(const syntax::StreamParameters& stream)
{
    if (stream.width == 0 || stream.height == 0) {
        return make_error(ErrorCode::InvalidArgument, "stream dimensions must be non-zero");
    }
    if (!constraints::is_supported_decoder_bit_depth(stream.bits_per_raw_sample)) {
        return make_error(ErrorCode::UnsupportedFeature, "only 1-16 bit samples are supported");
    }
    if (!constraints::is_supported_syntax_colorspace(stream.colorspace_type)) {
        return make_error(ErrorCode::UnsupportedFeature, "unsupported colorspace_type");
    }
    if (constraints::has_invalid_rgb_geometry(
            stream.colorspace_type == 1,
            stream.chroma_planes,
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
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

    std::ptrdiff_t unused_offset = 0;
    Status status = checked_plane_window_offset(
        info,
        0,
        0,
        expected.width,
        expected.height,
        unused_offset,
        "input plane last row address is not representable",
        "input plane last row address is not representable",
        "input plane last row address is not representable",
        "input plane last row address is not representable");
    if (!status.ok()) {
        return status;
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
