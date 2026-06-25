#include "mffv1/codec.hpp"

#include "codec/configuration_record_writer.hpp"
#include "codec/slice_encoder.hpp"
#include "mffv1/stream_parameters.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace mffv1 {

namespace {

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
        && (info.color_space != ColorSpace::YCbCr
            || info.bits_per_raw_sample != 8
            || info.has_chroma_planes
            || info.has_extra_plane)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice encoding currently supports only 8-bit Y-only streams");
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
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;
    stream.quant_table_sets.push_back(syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    out_stream = std::move(stream);
    return ok_status();
}

class Encoder final : public IEncoder {
public:
    explicit Encoder(EncoderOptions options)
        : options_(options)
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
        out_record.bytes = std::move(record_bytes);
        return ok_status();
    }

    Status encode_frame(FrameView input, EncodedFrame& out_frame) override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "encoder is not configured");
        }
        std::vector<std::byte> frame_bytes;
        const codec::SliceEncoder encoder(*stream_);
        Status status = encoder.encode_slice(input, true, frame_bytes);
        if (!status.ok()) {
            return status;
        }
        out_frame.bytes = std::move(frame_bytes);
        return ok_status();
    }

private:
    EncoderOptions options_;
    std::optional<syntax::StreamParameters> stream_;
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
    result.status = ok_status();
    result.encoder = std::make_unique<Encoder>(options);
    return result;
}

} // namespace mffv1
