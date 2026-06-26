#include "codec/frame_info_builder.hpp"

#include <cstddef>
#include <cstdint>

namespace mffv1::codec {

FrameInfo make_frame_info(const syntax::StreamParameters& stream) noexcept
{
    FrameInfo info;
    info.width = stream.width;
    info.height = stream.height;
    info.version = static_cast<std::uint8_t>(stream.version);
    info.micro_version = static_cast<std::uint16_t>(stream.micro_version);
    info.entropy_mode = stream.entropy_mode;
    info.bits_per_raw_sample = stream.bits_per_raw_sample;
    info.plane_count = syntax::coded_plane_count(stream);

    const auto sample_format = stream.bits_per_raw_sample <= 8
        ? SampleFormat::UInt8
        : SampleFormat::UInt16;
    const std::ptrdiff_t bytes_per_sample =
        sample_format == SampleFormat::UInt16 ? 2 : 1;

    for (std::size_t plane_index = 0; plane_index < info.plane_count;
         ++plane_index) {
        PlaneInfo& plane = info.planes[plane_index];
        plane.role = syntax::expected_plane_role(stream, plane_index);
        plane.sample_format = sample_format;
        plane.width = syntax::plane_width(stream, plane_index);
        plane.height = syntax::plane_height(stream, plane_index);
        plane.stride_bytes =
            static_cast<std::ptrdiff_t>(plane.width) * bytes_per_sample;
    }

    info.color_space =
        stream.colorspace_type == 1 ? ColorSpace::Rgb : ColorSpace::YCbCr;
    info.has_chroma_planes = stream.chroma_planes;
    info.has_extra_plane = stream.extra_plane;
    info.log2_h_chroma_subsample = stream.log2_h_chroma_subsample;
    info.log2_v_chroma_subsample = stream.log2_v_chroma_subsample;
    info.error_status_enabled = stream.error_status_enabled;
    info.intra_only = stream.intra_only;
    return info;
}

} // namespace mffv1::codec
