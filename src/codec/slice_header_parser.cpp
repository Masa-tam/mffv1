#include "codec/slice_header_parser.hpp"

#include <cstddef>
#include <limits>

namespace ffv1::codec {

Status SliceHeaderParser::read(entropy::SymbolReader& reader,
                               const syntax::StreamParameters& stream,
                               SliceHeaderValues& out_values) const
{
    std::uint64_t value = 0;
    Status status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value > stream.width) {
        return make_error(ErrorCode::SyntaxError, "slice_x is outside the frame");
    }
    out_values.x = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value > stream.height) {
        return make_error(ErrorCode::SyntaxError, "slice_y is outside the frame");
    }
    out_values.y = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::SyntaxError, "slice_width must be non-zero and fit uint32");
    }
    out_values.width = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::SyntaxError, "slice_height must be non-zero and fit uint32");
    }
    out_values.height = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value == 0 || value > 3) {
        return make_error(ErrorCode::UnsupportedFeature, "unsupported quant_table_set_index_count");
    }

    out_values.quant_table_set_indexes.clear();
    out_values.quant_table_set_indexes.reserve(static_cast<std::size_t>(value));
    for (std::uint64_t i = 0; i < value; ++i) {
        std::uint64_t index = 0;
        status = reader.read_unsigned(index);
        if (!status.ok()) {
            return status;
        }
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return make_error(ErrorCode::SyntaxError, "quant_table_set_index is too large");
        }
        out_values.quant_table_set_indexes.push_back(static_cast<std::uint32_t>(index));
    }

    return ok_status();
}

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
