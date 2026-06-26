#include "mffv1/codec.hpp"

#include "codec/configuration_record_writer.hpp"
#include "codec/slice_encode_executor.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace mffv1 {

namespace {

constexpr std::uint64_t kCifPixelCount = 352u * 288u;

Status normalize_initial_profile(const EncoderOptions& options,
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
    if (info.bits_per_raw_sample < 8
        || info.bits_per_raw_sample > 16) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder supports only 8-16 bit planar YCbCr or RGB streams, with an optional extra plane");
    }
    if (info.color_space != ColorSpace::YCbCr
        && info.color_space != ColorSpace::Rgb) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "encoder color space is unsupported");
    }
    if (info.color_space == ColorSpace::Rgb
        && (!info.has_chroma_planes
            || info.log2_h_chroma_subsample != 0
            || info.log2_v_chroma_subsample != 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "RGB streams require three full-resolution color planes");
    }
    if (options.entropy_mode == EntropyMode::GolombRice
        && (info.bits_per_raw_sample < 8
            || info.bits_per_raw_sample > 16)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice encoding supports only 8-16 bit streams");
    }
    if (!info.has_chroma_planes
        && (info.log2_h_chroma_subsample != 0
            || info.log2_v_chroma_subsample != 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (info.log2_h_chroma_subsample > 1
        || info.log2_v_chroma_subsample > 1
        || info.log2_v_chroma_subsample
            > info.log2_h_chroma_subsample) {
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
    const auto frame_pixel_count =
        static_cast<std::uint64_t>(stream.width)
        * static_cast<std::uint64_t>(stream.height);
    if (frame_pixel_count > kCifPixelCount && slice_count < 4) {
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

class Encoder final : public IEncoder {
public:
    explicit Encoder(EncoderOptions options)
        : options_(options)
        , kernels_(simd::make_codec_kernels(options.cpu))
    {
    }

    Status configure(const StreamInfo& stream, ConfigurationRecord& out_record) override
    {
        syntax::StreamParameters normalized;
        Status status = normalize_initial_profile(options_, stream, normalized);
        if (!status.ok()) {
            return status;
        }

        std::vector<std::byte> record_bytes;
        const codec::ConfigurationRecordWriter writer;
        status = writer.write(normalized, record_bytes);
        if (!status.ok()) {
            return status;
        }

        stream_ = std::move(normalized);
        executor_ = std::make_unique<codec::SliceEncodeExecutor>(
            *stream_, options_.thread_count, kernels_);
        next_frame_index_ = 0;
        out_record.bytes = std::move(record_bytes);
        return ok_status();
    }

    Status encode_frame(FrameView input, EncodedFrame& out_frame) override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "encoder is not configured");
        }
        std::vector<std::byte> frame_bytes;
        if (!executor_) {
            return make_error(ErrorCode::InvalidState, "encoder executor is not configured");
        }
        const bool keyframe =
            options_.keyframe_interval == 1
            || next_frame_index_ % options_.keyframe_interval == 0;
        Status status = executor_->encode(input, keyframe, frame_bytes);
        if (!status.ok()) {
            return status;
        }
        out_frame.bytes = std::move(frame_bytes);
        ++next_frame_index_;
        return ok_status();
    }

private:
    EncoderOptions options_;
    simd::CodecKernels kernels_;
    std::optional<syntax::StreamParameters> stream_;
    std::unique_ptr<codec::SliceEncodeExecutor> executor_;
    std::uint64_t next_frame_index_ = 0;
};

} // namespace

EncoderFactoryResult create_encoder(const EncoderOptions& options)
{
    EncoderFactoryResult result;
    if (options.thread_count < 0) {
        result.status = make_error(
            ErrorCode::InvalidArgument,
            "encoder thread count must not be negative");
        return result;
    }
    if (options.keyframe_interval == 0) {
        result.status = make_error(
            ErrorCode::InvalidArgument,
            "encoder keyframe interval must be non-zero");
        return result;
    }
    result.status = ok_status();
    result.encoder = std::make_unique<Encoder>(options);
    return result;
}

} // namespace mffv1
