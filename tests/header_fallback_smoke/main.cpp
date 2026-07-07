#include <mffv1/codec.hpp>
#include <mffv1/config.hpp>
#include <mffv1/frame.hpp>
#include <mffv1/options.hpp>
#include <mffv1/result.hpp>

#ifndef MFFV1_ENABLE_STATUS_MESSAGES
#error "result.hpp must provide a fallback MFFV1_ENABLE_STATUS_MESSAGES value"
#endif

static_assert(MFFV1_ENABLE_STATUS_MESSAGES == 1);

int main()
{
    mffv1::DecoderOptions decoder_options;
    mffv1::EncoderOptions encoder_options;
    mffv1::StreamInfo stream;
    mffv1::FrameInfo frame;
    mffv1::ConfigurationRecord configuration;
    mffv1::EncodedFrame encoded;
    mffv1::Status status;

    const bool ok = decoder_options.strict
        && encoder_options.version == 3
        && stream.version == 3
        && frame.plane_count == 0
        && configuration.bytes.empty()
        && encoded.bytes.empty()
        && status.code == mffv1::ErrorCode::Ok;

    return ok ? 0 : 1;
}
