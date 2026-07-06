#include "mffv1/codec.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
#include "codec/frame_validator.hpp"
#include "codec/slice_executor.hpp"
#include "mffv1/stream_parameters.hpp"
#include "util/status.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace mffv1 {

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

        if (options_.frame_width != 0 && options_.frame_height != 0) {
            stream.width = options_.frame_width;
            stream.height = options_.frame_height;
        }

        stream_ = std::move(stream);
        slice_executor_ = std::make_unique<codec::SliceExecutor>(
            *stream_, options_.thread_count, options_.cpu);
        return ok_status();
    }

    Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        Status status = ensure_dimensions_known();
        if (!status.ok()) {
            return status;
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        codec::FrameDecodeContext frame;
        codec::FrameParser parser(*stream_, options_.verify_crc);
        status = parse_frame(parser, frame_payload, frame);
        if (!status.ok()) {
            return status;
        }
        out_info = frame.frame_info;
        return ok_status();
    }

    Status decode_frame(ByteSpan frame_payload, MutableFrameView output) override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        Status status = ensure_dimensions_known();
        if (!status.ok()) {
            return status;
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        codec::FrameDecodeContext frame;
        frame.output = output;
        codec::FrameParser parser(*stream_, options_.verify_crc);
        status = parse_frame(parser, frame_payload, frame);
        if (!status.ok()) {
            return status;
        }
        const codec::FrameValidator validator;
        status = validator.validate_output(*stream_, output);
        if (!status.ok()) {
            return status;
        }
        return decode_slices(frame, output);
    }

private:
    Status ensure_dimensions_known() const
    {
        if (stream_->width == 0 || stream_->height == 0) {
            return make_error(ErrorCode::InvalidState, "decoder frame dimensions are not configured");
        }
        return ok_status();
    }

    Status parse_frame(const codec::FrameParser& parser,
                       ByteSpan frame_payload,
                       codec::FrameDecodeContext& frame) const
    {
        if (stream_->version >= 3) {
            return parser.parse_with_range_header(frame_payload, frame);
        }
        return parser.parse(frame_payload, frame);
    }

    Status decode_slices(const codec::FrameDecodeContext& frame, MutableFrameView output)
    {
        if (!slice_executor_) {
            return make_error(ErrorCode::InvalidState, "slice executor is not configured");
        }
        return slice_executor_->decode(output, frame.slices, frame.keyframe);
    }

    DecoderOptions options_;
    std::optional<syntax::StreamParameters> stream_;
    std::unique_ptr<codec::SliceExecutor> slice_executor_;
};

} // namespace

DecoderFactoryResult create_decoder(const DecoderOptions& options)
{
    DecoderFactoryResult result;
    if (options.thread_count < 0) {
        result.status = make_error(ErrorCode::InvalidArgument, "decoder thread count must not be negative");
        return result;
    }
    if (!options.strict) {
        result.status = make_error(ErrorCode::UnsupportedFeature,
                                   "decoder relaxed parsing is not implemented");
        return result;
    }
    if ((options.frame_width == 0) != (options.frame_height == 0)) {
        result.status = make_error(ErrorCode::InvalidArgument,
                                   "decoder frame dimensions must be both set or both zero");
        return result;
    }
    result.status = ok_status();
    result.decoder = std::make_unique<Decoder>(options);
    return result;
}

} // namespace mffv1
