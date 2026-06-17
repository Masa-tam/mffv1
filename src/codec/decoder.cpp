#include "ffv1/codec.hpp"

#include <memory>

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
        if (configuration_record.empty()) {
            return make_error(ErrorCode::InvalidArgument, "configuration record is empty");
        }
        configured_ = true;
        return ok_status();
    }

    Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const override
    {
        if (!configured_) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        out_info = {};
        return make_error(ErrorCode::NotImplemented, "frame inspection is not implemented yet");
    }

    Status decode_frame(ByteSpan frame_payload, MutableFrameView output) override
    {
        if (!configured_) {
            return make_error(ErrorCode::InvalidState, "decoder is not configured");
        }
        if (frame_payload.empty()) {
            return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
        }
        if (output.planes == nullptr && output.plane_count != 0) {
            return make_error(ErrorCode::InvalidArgument, "output plane pointer is null");
        }
        return make_error(ErrorCode::NotImplemented, "frame decoding is not implemented yet");
    }

private:
    DecoderOptions options_;
    bool configured_ = false;
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

