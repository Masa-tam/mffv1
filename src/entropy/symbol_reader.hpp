#pragma once

#include <cstdint>

#include "mffv1/result.hpp"

namespace mffv1::entropy {

using ContextId = std::uint32_t;

class SymbolReader {
public:
    virtual ~SymbolReader() = default;

    virtual Status read_bool(bool& out_value) = 0;
    virtual Status read_unsigned(std::uint64_t& out_value) = 0;
    virtual Status read_signed(std::int64_t& out_value) = 0;
    virtual Status read_signed(ContextId context, std::int64_t& out_value)
    {
        (void)context;
        return read_signed(out_value);
    }
    [[nodiscard]] virtual std::uint64_t byte_position() const noexcept;
};

} // namespace mffv1::entropy
