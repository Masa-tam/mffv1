#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

namespace mffv1::codec {

namespace {

std::uint32_t normalize_thread_count(int thread_count) noexcept
{
    if (thread_count < 0) {
        return 1;
    }
    if (thread_count == 0) {
        const auto hardware_threads = std::thread::hardware_concurrency();
        return hardware_threads == 0 ? 1 : hardware_threads;
    }
    return static_cast<std::uint32_t>(thread_count);
}

std::array<std::uint32_t, 4> slice_layout(const syntax::SliceDescriptor& slice) noexcept
{
    return {slice.raster_x, slice.raster_y, slice.raster_width, slice.raster_height};
}

} // namespace

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream) noexcept
    : SliceExecutor(stream, 1)
{
}

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream, int thread_count) noexcept
    : stream_(stream)
    , thread_count_(normalize_thread_count(thread_count))
{
}

Status SliceExecutor::decode(MutableFrameView output,
                             std::span<const syntax::SliceDescriptor> slices,
                             bool keyframe)
{
    Status status = validate_slices(output, slices);
    if (!status.ok()) {
        return status;
    }

    std::vector<SliceState> working_states;
    std::vector<SliceLayout> working_layouts;
    working_layouts.reserve(slices.size());
    for (const auto& slice : slices) {
        working_layouts.push_back(slice_layout(slice));
    }
    if (keyframe) {
        working_states.resize(slices.size());
    } else {
        if (slice_states_.empty()) {
            return make_error(ErrorCode::InvalidState,
                              "non-keyframe requires reference slice states");
        }
        if (slice_states_.size() != slices.size()) {
            return make_error(ErrorCode::SyntaxError,
                              "non-keyframe slice count differs from the reference frame");
        }
        if (slice_layouts_.size() != slice_states_.size()) {
            return make_error(ErrorCode::InvalidState,
                              "reference slice layouts do not match reference states");
        }

        std::vector<std::size_t> reference_order(slice_layouts_.size());
        std::vector<std::size_t> current_order(working_layouts.size());
        std::iota(reference_order.begin(), reference_order.end(), 0);
        std::iota(current_order.begin(), current_order.end(), 0);
        std::sort(reference_order.begin(),
                  reference_order.end(),
                  [&](std::size_t lhs, std::size_t rhs) {
                      return slice_layouts_[lhs] < slice_layouts_[rhs];
                  });
        std::sort(current_order.begin(),
                  current_order.end(),
                  [&](std::size_t lhs, std::size_t rhs) {
                      return working_layouts[lhs] < working_layouts[rhs];
                  });

        working_states.resize(slices.size());
        for (std::size_t rank = 0; rank < current_order.size(); ++rank) {
            const auto current = current_order[rank];
            const auto reference = reference_order[rank];
            if (working_layouts[current] != slice_layouts_[reference]) {
                Status mismatch = make_error(
                    ErrorCode::SyntaxError,
                    "non-keyframe slice layout differs from the reference frame");
                set_slice_location_if_missing(mismatch, slices[current].index);
                return mismatch;
            }
            working_states[current] = slice_states_[reference];
        }
    }

    if (thread_count_ <= 1 || slices.size() < 2) {
        status = decode_serial(output, slices, working_states);
    } else {
        status = decode_parallel(output, slices, working_states);
    }
    if (status.ok()) {
        slice_states_ = std::move(working_states);
        slice_layouts_ = std::move(working_layouts);
    }
    return status;
}

bool SliceExecutor::has_reference_state() const noexcept
{
    return !slice_states_.empty();
}

std::uint32_t SliceExecutor::thread_count() const noexcept
{
    return thread_count_;
}

std::size_t SliceExecutor::worker_count_for(std::size_t slice_count) const noexcept
{
    if (slice_count == 0) {
        return 0;
    }
    return std::min<std::size_t>(thread_count_, slice_count);
}

Status SliceExecutor::validate_slices(MutableFrameView output,
                                      std::span<const syntax::SliceDescriptor> slices) const
{
    const SliceDecoder decoder(stream_);
    for (const auto& slice : slices) {
        SliceOutputWindow window;
        Status status = window.validate(stream_, output, slice);
        if (status.ok()) {
            status = decoder.validate(slice, window);
        }
        if (!status.ok()) {
            set_slice_location_if_missing(status, slice.index);
            return status;
        }
    }
    return ok_status();
}

Status SliceExecutor::decode_serial(MutableFrameView output,
                                    std::span<const syntax::SliceDescriptor> slices,
                                    std::vector<SliceState>& states) const
{
    for (std::size_t i = 0; i < slices.size(); ++i) {
        Status status = decode_slice(output, slices[i], states[i]);
        if (!status.ok()) {
            set_slice_location_if_missing(status, slices[i].index);
            return status;
        }
    }
    return ok_status();
}

Status SliceExecutor::decode_parallel(MutableFrameView output,
                                      std::span<const syntax::SliceDescriptor> slices,
                                      std::vector<SliceState>& states) const
{
    const auto worker_count = worker_count_for(slices.size());
    std::vector<std::future<Status>> futures;
    futures.reserve(worker_count);
    std::vector<Status> statuses;
    statuses.reserve(slices.size());

    for (std::size_t offset = 0; offset < slices.size(); offset += worker_count) {
        const auto batch_size = std::min(worker_count, slices.size() - offset);
        futures.clear();
        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto* slice = &slices[offset + i];
            auto* state = &states[offset + i];
            futures.push_back(std::async(std::launch::async, [this, output, slice, state]() {
                return decode_slice(output, *slice, *state);
            }));
        }

        for (std::size_t i = 0; i < batch_size; ++i) {
            Status status = futures[i].get();
            if (!status.ok()) {
                set_slice_location_if_missing(status, slices[offset + i].index);
            }
            statuses.push_back(std::move(status));
        }
    }

    for (auto& status : statuses) {
        if (!status.ok()) {
            return status;
        }
    }
    return ok_status();
}

Status SliceExecutor::decode_slice(MutableFrameView output,
                                   const syntax::SliceDescriptor& slice,
                                   SliceState& state) const
{
    SliceOutputWindow window;
    Status status = window.validate(stream_, output, slice);
    if (!status.ok()) {
        return status;
    }

    status = state.reset(window);
    if (!status.ok()) {
        return status;
    }

    const SliceDecoder decoder(stream_);
    return decoder.decode(slice, window, state);
}

} // namespace mffv1::codec
