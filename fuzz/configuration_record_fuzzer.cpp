#include "mffv1/codec.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    mffv1::DecoderOptions options;
    options.frame_width = 32;
    options.frame_height = 24;
    options.cpu.auto_detect = false;
    options.cpu.allowed = 0;

    auto decoder = mffv1::create_decoder(options);
    if (!decoder.status.ok() || decoder.decoder == nullptr) {
        return 0;
    }

    const mffv1::ByteSpan record{reinterpret_cast<const std::byte*>(data), size};
    (void)decoder.decoder->configure(record);
    return 0;
}
