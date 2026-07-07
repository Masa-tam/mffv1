#include <mffv1/codec.hpp>
#include <mffv1/build_config.hpp>

#ifndef MFFV1_ENABLE_STATUS_MESSAGES
#error "mffv1/build_config.hpp must define MFFV1_ENABLE_STATUS_MESSAGES"
#endif

#ifndef MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES
#error "package smoke must define MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES"
#endif

int main()
{
    static_assert(
        MFFV1_ENABLE_STATUS_MESSAGES == MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES);

    const mffv1::DecoderOptions decoder_options{};
    auto decoder_result = mffv1::create_decoder(decoder_options);
    if (!decoder_result.status.ok() || decoder_result.decoder == nullptr) {
        return 1;
    }

    const mffv1::EncoderOptions encoder_options{};
    auto encoder_result = mffv1::create_encoder(encoder_options);
    if (!encoder_result.status.ok() || encoder_result.encoder == nullptr) {
        return 1;
    }

    return 0;
}
