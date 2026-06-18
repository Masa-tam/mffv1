#pragma once

#include <cstdint>
#include <vector>

#include "entropy/symbol_reader.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

struct SliceHeaderValues {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> quant_table_set_indexes;
    std::uint64_t picture_structure = 0;
    std::uint64_t sar_num = 0;
    std::uint64_t sar_den = 0;
};

class SliceHeaderParser {
public:
    Status read(entropy::SymbolReader& reader,
                const syntax::StreamParameters& stream,
                SliceHeaderValues& out_values) const;

    Status read_descriptor(entropy::SymbolReader& reader,
                           const syntax::StreamParameters& stream,
                           syntax::SliceDescriptor& descriptor) const;

    Status apply(const syntax::StreamParameters& stream,
                 const SliceHeaderValues& values,
                 syntax::SliceDescriptor& descriptor) const;

    Status apply_raster(const syntax::StreamParameters& stream,
                        const SliceHeaderValues& values,
                        syntax::SliceDescriptor& descriptor) const;
};

} // namespace ffv1::codec
