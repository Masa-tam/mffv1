#pragma once

#include <cstdint>
#include <memory>

#include "mffv1/config.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/options.hpp"
#include "mffv1/result.hpp"

namespace mffv1 {

enum class LegacyBootstrapState : std::uint8_t {
    NoEmbeddedParameters = 0,
    Configured = 1,
    MatchesCurrentConfiguration = 2,
    DiffersFromCurrentConfiguration = 3,
};

struct LegacyBootstrapInfo {
    LegacyBootstrapState state = LegacyBootstrapState::NoEmbeddedParameters;
    FrameInfo frame_info;
};

struct [[nodiscard]] LegacyBootstrapResult {
    Status status;
    LegacyBootstrapInfo info;
};

class IDecoder {
public:
    IDecoder() = default;
    IDecoder(const IDecoder&) = delete;
    IDecoder& operator=(const IDecoder&) = delete;
    IDecoder(IDecoder&&) = delete;
    IDecoder& operator=(IDecoder&&) = delete;
    virtual ~IDecoder() = default;

    [[nodiscard]] virtual Status configure(ByteSpan configuration_record) = 0;
    [[nodiscard]] virtual LegacyBootstrapResult bootstrap_legacy_frame(
        ByteSpan frame_payload) = 0;
    [[nodiscard]] virtual Status inspect_frame(
        ByteSpan frame_payload, FrameInfo& out_info) const = 0;
    [[nodiscard]] virtual Status decode_frame(
        ByteSpan frame_payload, MutableFrameView output) = 0;
};

class IEncoder {
public:
    IEncoder() = default;
    IEncoder(const IEncoder&) = delete;
    IEncoder& operator=(const IEncoder&) = delete;
    IEncoder(IEncoder&&) = delete;
    IEncoder& operator=(IEncoder&&) = delete;
    virtual ~IEncoder() = default;

    [[nodiscard]] virtual Status configure(
        const StreamInfo& stream, ConfigurationRecord& out_record) = 0;
    [[nodiscard]] virtual Status encode_frame(
        FrameView input, EncodedFrame& out_frame) = 0;
};

struct [[nodiscard]] DecoderFactoryResult {
    Status status;
    std::unique_ptr<IDecoder> decoder;
};

struct [[nodiscard]] EncoderFactoryResult {
    Status status;
    std::unique_ptr<IEncoder> encoder;
};

[[nodiscard]] DecoderFactoryResult create_decoder(const DecoderOptions& options);
[[nodiscard]] EncoderFactoryResult create_encoder(const EncoderOptions& options);

} // namespace mffv1
