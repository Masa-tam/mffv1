#pragma once

#include <cstdint>

#include "mffv1/result.hpp"

namespace mffv1::entropy {

class SymbolWriter {
public:
    virtual ~SymbolWriter() = default;

    virtual Status write_bool(bool value) = 0;
    virtual Status write_unsigned(std::uint64_t value) = 0;
    virtual Status write_signed(std::int64_t value) = 0;
};

} // namespace mffv1::entropy
