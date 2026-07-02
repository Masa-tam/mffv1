#pragma once

#include <cstddef>
#include <cstdint>

#include "mffv1/result.hpp"
#include "mffv1/state_transition.hpp"

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
    virtual Status begin_independent_scalar_contexts(std::size_t scalar_context_count)
    {
        (void)scalar_context_count;
        return ok_status();
    }
    virtual Status end_independent_scalar_contexts()
    {
        return ok_status();
    }
    virtual Status set_state_transition(
        const syntax::StateTransitionTable& state_transition)
    {
        (void)state_transition;
        return ok_status();
    }
    [[nodiscard]] virtual std::uint64_t byte_position() const noexcept;
};

} // namespace mffv1::entropy
