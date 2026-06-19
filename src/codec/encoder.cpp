#include "mffv1/codec.hpp"

#include <memory>

namespace mffv1 {

namespace {

class Encoder final : public IEncoder {
public:
    explicit Encoder(EncoderOptions options)
        : options_(options)
    {
    }

    Status configure(const StreamInfo& stream, ConfigurationRecord& out_record) override
    {
        if (stream.width == 0 || stream.height == 0) {
            return make_error(ErrorCode::InvalidArgument, "stream dimensions must be non-zero");
        }
        if (options_.version != 0 && options_.version != 1 && options_.version != 3) {
            return make_error(ErrorCode::UnsupportedFeature, "unsupported FFV1 version");
        }
        out_record.bytes.clear();
        configured_ = true;
        return make_error(ErrorCode::NotImplemented, "configuration record writing is not implemented yet");
    }

    Status encode_frame(FrameView input, EncodedFrame& out_frame) override
    {
        if (!configured_) {
            return make_error(ErrorCode::InvalidState, "encoder is not configured");
        }
        if (input.planes == nullptr && input.plane_count != 0) {
            return make_error(ErrorCode::InvalidArgument, "input plane pointer is null");
        }
        out_frame.bytes.clear();
        return make_error(ErrorCode::NotImplemented, "frame encoding is not implemented yet");
    }

private:
    EncoderOptions options_;
    bool configured_ = false;
};

} // namespace

EncoderFactoryResult create_encoder(const EncoderOptions& options)
{
    EncoderFactoryResult result;
    result.status = ok_status();
    result.encoder = std::make_unique<Encoder>(options);
    return result;
}

} // namespace mffv1

