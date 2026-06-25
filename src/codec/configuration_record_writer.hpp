#pragma once

#include <cstddef>
#include <vector>

#include "entropy/symbol_writer.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class ConfigurationRecordWriter {
public:
    Status write(const syntax::StreamParameters& stream,
                 std::vector<std::byte>& out_record) const;
    Status write_parameters(const syntax::StreamParameters& stream,
                            entropy::SymbolWriter& writer) const;

private:
    Status validate_initial_profile(
        const syntax::StreamParameters& stream) const;
};

} // namespace mffv1::codec
