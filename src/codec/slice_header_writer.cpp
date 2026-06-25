#include "codec/slice_header_writer.hpp"

#include <cstddef>

namespace mffv1::codec {

Status SliceHeaderWriter::write(
    entropy::SymbolWriter& writer,
    const syntax::StreamParameters& stream,
    const SliceHeaderValues& values) const
{
    syntax::SliceDescriptor descriptor;
    const SliceHeaderParser parser;
    Status status = parser.apply_raster(stream, values, descriptor);
    if (!status.ok()) {
        return status;
    }

    const auto required_index_count =
        syntax::quant_table_set_index_count(stream);
    if (values.quant_table_set_indexes.size() != required_index_count) {
        return make_error(
            ErrorCode::InvalidArgument,
            "slice header quantization table index count does not match the stream");
    }
    if ((values.sar_num == 0) != (values.sar_den == 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "slice header sample aspect ratio must be fully specified or absent");
    }

    status = writer.write_unsigned(values.x);
    if (!status.ok()) {
        return status;
    }
    status = writer.write_unsigned(values.y);
    if (!status.ok()) {
        return status;
    }
    status = writer.write_unsigned(values.width - 1);
    if (!status.ok()) {
        return status;
    }
    status = writer.write_unsigned(values.height - 1);
    if (!status.ok()) {
        return status;
    }
    for (const auto index : values.quant_table_set_indexes) {
        status = writer.write_unsigned(index);
        if (!status.ok()) {
            return status;
        }
    }
    status = writer.write_unsigned(values.picture_structure);
    if (!status.ok()) {
        return status;
    }
    status = writer.write_unsigned(values.sar_num);
    if (!status.ok()) {
        return status;
    }
    return writer.write_unsigned(values.sar_den);
}

} // namespace mffv1::codec
