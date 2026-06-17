#pragma once

#include <cstdint>

#include "ffv1/result.hpp"

namespace ffv1::entropy {

using ContextId = std::uint32_t;

class SymbolReader {
public:
    virtual ~SymbolReader() = default;

    virtual Status read_bool(bool& out_value) = 0;
    virtual Status read_unsigned(std::uint64_t& out_value) = 0;
    virtual Status read_signed(std::int64_t& out_value) = 0;
};

} // namespace ffv1::entropy

