#pragma once

#include "codec/slice_header_parser.hpp"
#include "entropy/symbol_writer.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceHeaderWriter {
public:
    Status write(entropy::SymbolWriter& writer,
                 const syntax::StreamParameters& stream,
                 const SliceHeaderValues& values) const;
};

} // namespace mffv1::codec
