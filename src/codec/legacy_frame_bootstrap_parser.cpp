#include "codec/legacy_frame_bootstrap_parser.hpp"

#include "entropy/range_coder.hpp"
#include "mffv1/configuration_parser.hpp"
#include "util/status.hpp"

#include <utility>

namespace mffv1::codec {

Status LegacyFrameBootstrapParser::parse(
    ByteSpan frame_payload,
    std::uint32_t frame_width,
    std::uint32_t frame_height,
    LegacyFrameBootstrap& out_bootstrap) const
{
    if (frame_payload.empty()) {
        return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
    }

    entropy::RangeCoder reader;
    Status status = reader.reset(frame_payload);
    if (!status.ok()) {
        return status;
    }

    bool keyframe = false;
    status = reader.read_bool(keyframe);
    if (!status.ok()) {
        return status;
    }

    LegacyFrameBootstrap bootstrap;
    bootstrap.keyframe = keyframe;
    bootstrap.content_byte_offset = reader.byte_position();

    if (!keyframe) {
        out_bootstrap = std::move(bootstrap);
        return ok_status();
    }

    syntax::StreamParameters stream;
    const syntax::ConfigurationParser parser;
    status = parser.parse(reader, stream);
    if (!status.ok()) {
        return status;
    }

    stream.width = frame_width;
    stream.height = frame_height;
    bootstrap.has_embedded_parameters = true;
    bootstrap.content_byte_offset = reader.byte_position();
    bootstrap.stream = std::move(stream);
    out_bootstrap = std::move(bootstrap);
    return ok_status();
}

} // namespace mffv1::codec
