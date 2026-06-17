#pragma once

#include <cstdint>
#include <vector>

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
};

class SliceHeaderParser {
public:
    Status apply(const syntax::StreamParameters& stream,
                 const SliceHeaderValues& values,
                 syntax::SliceDescriptor& descriptor) const;
};

} // namespace ffv1::codec

