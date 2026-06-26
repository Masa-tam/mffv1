#include "codec/encoder_profile.hpp"

#include "codec/profile_constraints.hpp"
#include "codec/version3_constraints.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace mffv1::codec {

Status normalize_encoder_profile(const EncoderOptions& options,
                                 const StreamInfo& info,
                                 syntax::StreamParameters& out_stream)
{
    if (info.width == 0 || info.height == 0) {
        return make_error(ErrorCode::InvalidArgument,
                          "stream dimensions must be non-zero");
    }
    if (options.version != info.version) {
        return make_error(ErrorCode::InvalidArgument,
                          "encoder option version does not match stream version");
    }
    if (options.version != 3) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder supports only FFV1 version 3");
    }
    if (options.entropy_mode != EntropyMode::Range
        && options.entropy_mode != EntropyMode::GolombRice) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder entropy mode is unsupported");
    }
    if (!is_supported_encoder_bit_depth(info.bits_per_raw_sample)) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder supports only 8-16 bit planar YCbCr or RGB streams, with an optional extra plane");
    }
    if (!is_supported_public_color_space(info.color_space)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "encoder color space is unsupported");
    }
    if (has_invalid_rgb_geometry(
            info.color_space == ColorSpace::Rgb,
            info.has_chroma_planes,
            info.log2_h_chroma_subsample,
            info.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "RGB streams require three full-resolution color planes");
    }
    if (options.entropy_mode == EntropyMode::GolombRice
        && !is_supported_encoder_bit_depth(info.bits_per_raw_sample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice encoding supports only 8-16 bit streams");
    }
    if (has_subsampling_without_chroma(
            info.has_chroma_planes,
            info.log2_h_chroma_subsample,
            info.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (!is_supported_chroma_subsampling(
            info.log2_h_chroma_subsample,
            info.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "encoder supports only 4:4:4, 4:2:2, and 4:2:0 chroma geometry");
    }
    if (info.num_h_slices == 0 || info.num_v_slices == 0) {
        return make_error(
            ErrorCode::InvalidArgument,
            "encoder slice grid dimensions must be non-zero");
    }
    syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.entropy_mode = options.entropy_mode;
    stream.width = info.width;
    stream.height = info.height;
    stream.bits_per_raw_sample = info.bits_per_raw_sample;
    stream.colorspace_type = static_cast<int>(info.color_space);
    stream.chroma_planes = info.has_chroma_planes;
    stream.extra_plane = info.has_extra_plane;
    stream.log2_h_chroma_subsample = info.log2_h_chroma_subsample;
    stream.log2_v_chroma_subsample = info.log2_v_chroma_subsample;
    stream.num_h_slices = info.num_h_slices;
    stream.num_v_slices = info.num_v_slices;
    const auto slice_count =
        static_cast<std::uint64_t>(stream.num_h_slices)
        * static_cast<std::uint64_t>(stream.num_v_slices);
    if (requires_version3_parallel_slice_limit(
            stream.version, stream.width, stream.height)
        && slice_count < 4) {
        return make_error(
            ErrorCode::InvalidArgument,
            "version 3 frames larger than CIF require at least four slices");
    }
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream));
    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        if (stream.num_h_slices > syntax::plane_width(stream, plane_index)
            || stream.num_v_slices
                > syntax::plane_height(stream, plane_index)) {
            return make_error(
                ErrorCode::InvalidArgument,
                "encoder slice grid would create an empty plane region");
        }
    }
    stream.quant_table_sets.push_back(syntax::make_zero_quant_table_set());
    stream.intra_only = options.keyframe_interval == 1;
    out_stream = std::move(stream);
    return ok_status();
}

} // namespace mffv1::codec
