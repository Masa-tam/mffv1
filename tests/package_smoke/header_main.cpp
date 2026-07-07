#include <mffv1/codec.hpp>
#include <mffv1/config.hpp>
#include <mffv1/frame.hpp>
#include <mffv1/options.hpp>
#include <mffv1/result.hpp>
#include <mffv1/build_config.hpp>

#ifndef MFFV1_ENABLE_STATUS_MESSAGES
#error "mffv1/build_config.hpp must define MFFV1_ENABLE_STATUS_MESSAGES"
#endif

#ifndef MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES
#error "package smoke must define MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES"
#endif

static_assert(
    MFFV1_ENABLE_STATUS_MESSAGES == MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES);

int main()
{
    const mffv1::DecoderOptions decoder_options{};
    const mffv1::EncoderOptions encoder_options{};
    const mffv1::StreamInfo stream{};
    const mffv1::FrameInfo frame{};
    const mffv1::ConfigurationRecord configuration{};
    const mffv1::EncodedFrame encoded{};
    const mffv1::Status status{};

    const bool ok = decoder_options.strict
        && encoder_options.version == 3
        && stream.version == 3
        && frame.plane_count == 0
        && configuration.bytes.empty()
        && encoded.bytes.empty()
        && status.code == mffv1::ErrorCode::Ok;

    return ok ? 0 : 1;
}
