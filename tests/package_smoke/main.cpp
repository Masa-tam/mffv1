#include <mffv1/codec.hpp>

int main()
{
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
