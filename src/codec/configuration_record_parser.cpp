#include "codec/configuration_record_parser.hpp"

#include "entropy/range_coder.hpp"
#include "ffv1/configuration_parser.hpp"
#include "util/crc32.hpp"
#include "util/status.hpp"

#include <cstdint>
#include <utility>

namespace ffv1::codec {

Status ConfigurationRecordParser::parse(ByteSpan record,
                                        syntax::StreamParameters& out_stream) const
{
    if (record.empty()) {
        return make_error(ErrorCode::InvalidArgument, "configuration record is empty");
    }

    // Only the version is needed to determine whether the final four bytes are CRC parity.
    entropy::RangeCoder probe_reader;
    Status status = probe_reader.reset(record);
    if (!status.ok()) {
        return status;
    }

    std::uint64_t version = 0;
    status = probe_reader.read_unsigned(version);
    if (!status.ok()) {
        return status;
    }

    constexpr std::size_t crc_size = 4;
    auto parameter_payload = record;
    if (version >= 3) {
        if (record.size() < crc_size) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "configuration record is too small for CRC parity",
                                   0);
        }
        if (util::crc32_ieee_msb(record) != 0) {
            return make_byte_error(ErrorCode::CrcMismatch,
                                   "configuration record CRC remainder is non-zero",
                                   record.size() - crc_size);
        }
        parameter_payload = record.first(record.size() - crc_size);
    }

    entropy::RangeCoder parameter_reader;
    status = parameter_reader.reset(parameter_payload);
    if (!status.ok()) {
        return status;
    }
    syntax::ConfigurationParser parser;
    syntax::StreamParameters stream;
    status = parser.parse(parameter_reader, stream);
    if (!status.ok()) {
        return status;
    }

    out_stream = std::move(stream);
    return ok_status();
}

} // namespace ffv1::codec
