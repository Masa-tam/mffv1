#include "ffv1/codec.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
#include "codec/frame_validator.hpp"
#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
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
        codec::FrameDecodeContext frame;
        codec::FrameParser parser(*stream_);
        Status status = parser.parse(frame_payload, frame);
        if (!status.ok()) {
            return status;
        }
        out_info = frame.frame_info;
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
        codec::FrameDecodeContext frame;
        frame.output = output;
        codec::FrameParser parser(*stream_);
        Status status = parser.parse(frame_payload, frame);
        if (!status.ok()) {
            return status;
        }
        const codec::FrameValidator validator;
        status = validator.validate_output(*stream_, output);
        if (!status.ok()) {
            return status;
        }
        for (const auto& slice : frame.slices) {
            codec::SliceOutputWindow window;
            status = window.validate(*stream_, output, slice);
            if (!status.ok()) {
                return status;
            }
            codec::SliceState state;
            status = state.reset(*stream_);
            if (!status.ok()) {
                return status;
            }
            const codec::SliceDecoder decoder(*stream_);
            status = decoder.decode(slice, window, state);
            if (!status.ok()) {
                return status;
            }
        }
        return ok_status();
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
