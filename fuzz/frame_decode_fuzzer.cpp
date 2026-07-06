#include "mffv1/codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

bool configure_decoder(mffv1::IDecoder& decoder)
{
    mffv1::EncoderOptions encoder_options;
    encoder_options.cpu.auto_detect = false;
    encoder_options.cpu.allowed = 0;
    auto encoder = mffv1::create_encoder(encoder_options);
    if (!encoder.status.ok() || encoder.encoder == nullptr) {
        return false;
    }

    mffv1::StreamInfo stream;
    stream.width = 16;
    stream.height = 16;
    stream.version = 3;
    stream.bits_per_raw_sample = 8;
    stream.has_chroma_planes = false;
    stream.has_extra_plane = false;
    stream.color_space = mffv1::ColorSpace::YCbCr;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;

    mffv1::ConfigurationRecord record;
    const auto status = encoder.encoder->configure(stream, record);
    if (!status.ok()) {
        return false;
    }
    return decoder.configure(record.bytes).ok();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    mffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 16;
    options.cpu.auto_detect = false;
    options.cpu.allowed = 0;

    auto decoder = mffv1::create_decoder(options);
    if (!decoder.status.ok() || decoder.decoder == nullptr) {
        return 0;
    }
    if (!configure_decoder(*decoder.decoder)) {
        return 0;
    }

    std::array<std::uint8_t, 16 * 16> pixels{};
    mffv1::MutablePlaneView plane;
    plane.data = pixels.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 16;
    plane.info.height = 16;
    plane.info.stride_bytes = 16;

    mffv1::MutableFrameView output{&plane, 1};
    const mffv1::ByteSpan payload{reinterpret_cast<const std::byte*>(data), size};
    mffv1::FrameInfo info;
    (void)decoder.decoder->inspect_frame(payload, info);
    (void)decoder.decoder->decode_frame(payload, output);
    return 0;
}
