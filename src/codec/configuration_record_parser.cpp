#include "codec/configuration_record_parser.hpp"

#include "entropy/range_coder.hpp"
#include "ffv1/configuration_parser.hpp"

namespace ffv1::codec {

Status ConfigurationRecordParser::parse(ByteSpan record,
                                        syntax::StreamParameters& out_stream) const
{
    if (record.empty()) {
        return make_error(ErrorCode::InvalidArgument, "configuration record is empty");
    }

    entropy::RangeCoder reader;
    Status status = reader.reset(record);
    if (!status.ok()) {
        return status;
    }

    syntax::ConfigurationParser parser;
    return parser.parse(reader, out_stream);
}

} // namespace ffv1::codec

