#pragma once

#include <memory>

#include "ffv1/config.hpp"
#include "ffv1/frame.hpp"
#include "ffv1/options.hpp"
#include "ffv1/result.hpp"

namespace ffv1 {

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual Status configure(ByteSpan configuration_record) = 0;
    virtual Status inspect_frame(ByteSpan frame_payload, FrameInfo& out_info) const = 0;
    virtual Status decode_frame(ByteSpan frame_payload, MutableFrameView output) = 0;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual Status configure(const StreamInfo& stream, ConfigurationRecord& out_record) = 0;
    virtual Status encode_frame(FrameView input, EncodedFrame& out_frame) = 0;
};

struct DecoderFactoryResult {
    Status status;
    std::unique_ptr<IDecoder> decoder;
};

struct EncoderFactoryResult {
    Status status;
    std::unique_ptr<IEncoder> encoder;
};

DecoderFactoryResult create_decoder(const DecoderOptions& options);
EncoderFactoryResult create_encoder(const EncoderOptions& options);

} // namespace ffv1

