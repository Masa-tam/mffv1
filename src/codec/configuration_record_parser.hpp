#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class ConfigurationRecordParser {
public:
    Status parse(ByteSpan record, syntax::StreamParameters& out_stream) const;
};

} // namespace ffv1::codec

