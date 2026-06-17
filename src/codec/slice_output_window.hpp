#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SliceOutputWindow {
public:
    Status validate(const syntax::StreamParameters& stream,
                    MutableFrameView frame,
                    const syntax::SliceDescriptor& slice);

    [[nodiscard]] std::size_t plane_count() const noexcept;
    [[nodiscard]] std::uint32_t plane_width(std::size_t plane_index) const noexcept;
    [[nodiscard]] std::uint32_t plane_height(std::size_t plane_index) const noexcept;

    std::uint8_t* row_u8(std::size_t plane_index, std::uint32_t y) const noexcept;
    std::uint16_t* row_u16(std::size_t plane_index, std::uint32_t y) const noexcept;

private:
    struct PlaneWindow {
        void* data = nullptr;
        std::ptrdiff_t stride_bytes = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        SampleFormat sample_format = SampleFormat::UInt8;
    };

    std::vector<PlaneWindow> planes_;
};

} // namespace ffv1::codec

