#pragma once

#include <cstddef>
#include <cstdint>

#include "entropy/symbol_reader.hpp"
#include "mffv1/result.hpp"

namespace mffv1::entropy {

class SymbolWriter {
public:
    virtual ~SymbolWriter() = default;

    virtual Status write_bool(bool value) = 0;
    virtual Status write_unsigned(std::uint64_t value) = 0;
    virtual Status write_signed(std::int64_t value) = 0;
    virtual Status write_unsigned(ContextId context, std::uint64_t value)
    {
        (void)context;
        return write_unsigned(value);
    }
    virtual Status write_signed(ContextId context, std::int64_t value)
    {
        (void)context;
        return write_signed(value);
    }
    virtual Status begin_independent_scalar_contexts(std::size_t scalar_context_count)
    {
        (void)scalar_context_count;
        return ok_status();
    }
    virtual Status end_independent_scalar_contexts()
    {
        return ok_status();
    }
};

} // namespace mffv1::entropy
