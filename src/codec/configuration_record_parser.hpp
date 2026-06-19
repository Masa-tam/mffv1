#pragma once

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class ConfigurationRecordParser {
public:
    Status parse(ByteSpan record, syntax::StreamParameters& out_stream) const;
};

} // namespace mffv1::codec

