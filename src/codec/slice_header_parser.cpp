#include "codec/slice_header_parser.hpp"

#include <cstddef>

namespace ffv1::codec {

Status SliceHeaderParser::apply(const syntax::StreamParameters& stream,
                                const SliceHeaderValues& values,
                                syntax::SliceDescriptor& descriptor) const
{
    if (values.width == 0 || values.height == 0) {
        return make_error(ErrorCode::SyntaxError, "slice header dimensions must be non-zero");
    }
    if (values.x > stream.width || values.y > stream.height
        || values.width > stream.width - values.x
        || values.height > stream.height - values.y) {
        return make_error(ErrorCode::SyntaxError, "slice header rectangle is outside the frame");
    }
    if (values.quant_table_set_indexes.empty()) {
        return make_error(ErrorCode::SyntaxError, "slice header has no quantization table set indexes");
    }
    for (const auto index : values.quant_table_set_indexes) {
        if (index >= stream.quant_table_sets.size()) {
            return make_error(ErrorCode::SyntaxError, "slice header quantization table set index is out of range");
        }
    }

    descriptor.x = values.x;
    descriptor.y = values.y;
    descriptor.width = values.width;
    descriptor.height = values.height;
    descriptor.quant_table_set_indexes = values.quant_table_set_indexes;
    return ok_status();
}

} // namespace ffv1::codec

