#pragma once

#include <cstdint>
#include <memory>

#include "mffv1/config.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/options.hpp"
#include "mffv1/result.hpp"

namespace mffv1 {

enum class LegacyBootstrapState : std::uint8_t {
    NoEmbeddedParameters,
    Configured,
    MatchesCurrentConfiguration,
    DiffersFromCurrentConfiguration,
};

struct LegacyBootstrapInfo {
    LegacyBootstrapState state = LegacyBootstrapState::NoEmbeddedParameters;
    FrameInfo frame_info;
};

struct LegacyBootstrapResult {
    Status status;
    LegacyBootstrapInfo info;
};

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual Status configure(ByteSpan configuration_record) = 0;
    virtual LegacyBootstrapResult bootstrap_legacy_frame(ByteSpan frame_payload) = 0;
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

} // namespace mffv1
