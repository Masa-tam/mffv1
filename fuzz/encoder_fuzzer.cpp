#include "mffv1/codec.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    mffv1::EncoderOptions options;
    options.cpu.auto_detect = false;
    options.cpu.allowed = 0;
    options.keyframe_interval = size > 0 && (data[0] & 1u) != 0 ? 2u : 1u;
    options.entropy_mode = size > 1 && (data[1] & 1u) != 0
        ? mffv1::EntropyMode::GolombRice
        : mffv1::EntropyMode::Range;

    auto encoder = mffv1::create_encoder(options);
    if (!encoder.status.ok() || encoder.encoder == nullptr) {
        return 0;
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
    if (!encoder.encoder->configure(stream, record).ok()) {
        return 0;
    }

    std::array<std::uint8_t, 16 * 16> pixels{};
    const auto copy_size = std::min<std::size_t>(pixels.size(), size);
    std::copy_n(data, copy_size, pixels.begin());

    mffv1::PlaneView plane;
    plane.data = pixels.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 16;
    plane.info.height = 16;
    plane.info.stride_bytes = 16;

    mffv1::FrameView input{&plane, 1};
    mffv1::EncodedFrame frame;
    (void)encoder.encoder->encode_frame(input, frame);
    return 0;
}
