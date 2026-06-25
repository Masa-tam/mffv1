#include "mffv1/codec.hpp"

#include "codec/configuration_record_writer.hpp"
#include "codec/frame_validator.hpp"
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
    if (options.entropy_mode != EntropyMode::Range) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder supports only range coding");
    }
    if (info.bits_per_raw_sample != 8
        || info.has_chroma_planes
        || info.has_extra_plane
        || info.log2_h_chroma_subsample != 0
        || info.log2_v_chroma_subsample != 0) {
        return make_error(ErrorCode::UnsupportedFeature,
                          "encoder supports only 8-bit Y-only streams");
    }

    syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.entropy_mode = EntropyMode::Range;
    stream.width = info.width;
    stream.height = info.height;
    stream.bits_per_raw_sample = 8;
    stream.colorspace_type = 0;
    stream.chroma_planes = false;
    stream.extra_plane = false;
    stream.log2_h_chroma_subsample = 0;
    stream.log2_v_chroma_subsample = 0;
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
        const codec::FrameValidator validator;
        Status status = validator.validate_input(*stream_, input);
        if (!status.ok()) {
            return status;
        }
        (void)out_frame;
        return make_error(ErrorCode::NotImplemented, "frame encoding is not implemented yet");
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
