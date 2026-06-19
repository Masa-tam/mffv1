#pragma once

#include <cstdint>

#include "entropy/symbol_reader.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::syntax {

class ConfigurationParser {
public:
    Status parse(entropy::SymbolReader& reader, StreamParameters& out_stream) const;

private:
    Status parse_quant_table_set(entropy::SymbolReader& reader,
                                 QuantTableSet& out_set) const;
    Status parse_quant_table(entropy::SymbolReader& reader,
                             QuantTableSet& table_set,
                             std::size_t table_index,
                             std::int64_t scale,
                             std::int64_t& out_len_count) const;
};

} // namespace mffv1::syntax

