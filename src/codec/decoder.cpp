#include "ffv1/codec.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_validator.hpp"
#include "ffv1/stream_parameters.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace ffv1 {

namespace {

class Decoder final : public IDecoder {
public:
    explicit Decoder(DecoderOptions options)
        : options_(options)
    {
    }

    Status configure(ByteSpan configuration_record) override
    {
        syntax::StreamParameters stream;
        const codec::ConfigurationRecordParser parser;
        Status status = parser.parse(configuration_record, stream);
        if (!status.ok()) {
            return status;
        }

        stream_ = std::move(stream);
        return ok_status();
    }

    Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        out_info = {};
        out_info.width = stream_->width;
        out_info.height = stream_->height;
        out_info.version = static_cast<std::uint8_t>(stream_->version);
        out_info.bits_per_raw_sample = stream_->bits_per_raw_sample;
        out_info.plane_count = syntax::coded_plane_count(*stream_);
        return make_error(ErrorCode::NotImplemented, "frame inspection is not implemented yet");
    }

    Status decode_frame(ByteSpan frame_payload, MutableFrameView output) override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        const codec::FrameValidator validator;
        Status status = validator.validate_output(*stream_, output);
        if (!status.ok()) {
            return status;
        }
        return make_error(ErrorCode::NotImplemented, "frame decoding is not implemented yet");
    }

private:
    DecoderOptions options_;
    std::optional<syntax::StreamParameters> stream_;
};

} // namespace

DecoderFactoryResult create_decoder(const DecoderOptions& options)
{
    DecoderFactoryResult result;
    result.status = ok_status();
    result.decoder = std::make_unique<Decoder>(options);
    return result;
}

} // namespace ffv1
