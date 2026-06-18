#include "codec/slice_header_parser.hpp"

#include "util/status.hpp"

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
    if (value > stream.num_h_slices || value > std::numeric_limits<std::uint32_t>::max()) {
        return make_byte_error(ErrorCode::SyntaxError, "slice_x is outside the slice raster", reader.byte_position());
    }
    out_values.x = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value > stream.num_v_slices || value > std::numeric_limits<std::uint32_t>::max()) {
        return make_byte_error(ErrorCode::SyntaxError, "slice_y is outside the slice raster", reader.byte_position());
    }
    out_values.y = static_cast<std::uint32_t>(value);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value >= std::numeric_limits<std::uint32_t>::max()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice_width_minus_one is too large",
                               reader.byte_position());
    }
    out_values.width = static_cast<std::uint32_t>(value + 1);

    status = reader.read_unsigned(value);
    if (!status.ok()) {
        return status;
    }
    if (value >= std::numeric_limits<std::uint32_t>::max()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice_height_minus_one is too large",
                               reader.byte_position());
    }
    out_values.height = static_cast<std::uint32_t>(value + 1);

    if (out_values.width > stream.num_h_slices - out_values.x
        || out_values.height > stream.num_v_slices - out_values.y) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice header rectangle is outside the slice raster",
                               reader.byte_position());
    }

    const auto index_count = syntax::quant_table_set_index_count(stream);
    out_values.quant_table_set_indexes.clear();
    out_values.quant_table_set_indexes.reserve(index_count);
    for (std::size_t i = 0; i < index_count; ++i) {
        std::uint64_t index = 0;
        status = reader.read_unsigned(index);
        if (!status.ok()) {
            return status;
        }
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "quant_table_set_index is too large",
                                   reader.byte_position());
        }
        if (index >= static_cast<std::uint64_t>(stream.quant_table_sets.size())) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice header quantization table set index is out of range",
                                   reader.byte_position());
        }
        out_values.quant_table_set_indexes.push_back(static_cast<std::uint32_t>(index));
    }

    status = reader.read_unsigned(out_values.picture_structure);
    if (!status.ok()) {
        return status;
    }
    status = reader.read_unsigned(out_values.sar_num);
    if (!status.ok()) {
        return status;
    }
    status = reader.read_unsigned(out_values.sar_den);
    if (!status.ok()) {
        return status;
    }
    if (out_values.sar_num == 0 || out_values.sar_den == 0) {
        out_values.sar_num = 0;
        out_values.sar_den = 0;
    }

    return ok_status();
}

Status SliceHeaderParser::read_descriptor(entropy::SymbolReader& reader,
                                          const syntax::StreamParameters& stream,
                                          syntax::SliceDescriptor& descriptor) const
{
    SliceHeaderValues values;
    const auto header_offset = reader.byte_position();
    Status status = read(reader, stream, values);
    if (!status.ok()) {
        return status;
    }

    status = apply_raster(stream, values, descriptor);
    if (!status.ok()) {
        return status;
    }

    descriptor.header_byte_offset = header_offset;
    descriptor.content_byte_offset = reader.byte_position();
    descriptor.payload_byte_offset = header_offset;
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
    descriptor.picture_structure = values.picture_structure;
    descriptor.sar_num = values.sar_num;
    descriptor.sar_den = values.sar_den;
    return ok_status();
}

Status SliceHeaderParser::apply_raster(const syntax::StreamParameters& stream,
                                       const SliceHeaderValues& values,
                                       syntax::SliceDescriptor& descriptor) const
{
    if (values.width == 0 || values.height == 0) {
        return make_error(ErrorCode::SyntaxError, "slice header dimensions must be non-zero");
    }
    if (values.x > stream.num_h_slices || values.y > stream.num_v_slices
        || values.width > stream.num_h_slices - values.x
        || values.height > stream.num_v_slices - values.y) {
        return make_error(ErrorCode::SyntaxError, "slice header rectangle is outside the slice raster");
    }
    if (values.quant_table_set_indexes.empty()) {
        return make_error(ErrorCode::SyntaxError, "slice header has no quantization table set indexes");
    }
    for (const auto index : values.quant_table_set_indexes) {
        if (index >= stream.quant_table_sets.size()) {
            return make_error(ErrorCode::SyntaxError, "slice header quantization table set index is out of range");
        }
    }

    descriptor.x = syntax::slice_pixel_x(stream, values.x);
    descriptor.y = syntax::slice_pixel_y(stream, values.y);
    descriptor.width = syntax::slice_pixel_width(stream, values.x, values.width);
    descriptor.height = syntax::slice_pixel_height(stream, values.y, values.height);
    descriptor.raster_x = values.x;
    descriptor.raster_y = values.y;
    descriptor.raster_width = values.width;
    descriptor.raster_height = values.height;
    descriptor.quant_table_set_indexes = values.quant_table_set_indexes;
    descriptor.picture_structure = values.picture_structure;
    descriptor.sar_num = values.sar_num;
    descriptor.sar_den = values.sar_den;
    return ok_status();
}

} // namespace ffv1::codec
