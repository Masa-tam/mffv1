#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
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

Status SliceExecutor::decode(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const
{
    Status status = validate_slices(output, slices);
    if (!status.ok()) {
        return status;
    }
    if (thread_count_ <= 1 || slices.size() < 2) {
        return decode_serial(output, slices);
    }
    return decode_parallel(output, slices);
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

Status SliceExecutor::decode_serial(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const
{
    for (const auto& slice : slices) {
        Status status = decode_slice(output, slice);
        if (!status.ok()) {
            set_slice_location_if_missing(status, slice.index);
            return status;
        }
    }
    return ok_status();
}

Status SliceExecutor::decode_parallel(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const
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
            futures.push_back(std::async(std::launch::async, [this, output, slice]() {
                return decode_slice(output, *slice);
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

Status SliceExecutor::decode_slice(MutableFrameView output, const syntax::SliceDescriptor& slice) const
{
    SliceOutputWindow window;
    Status status = window.validate(stream_, output, slice);
    if (!status.ok()) {
        return status;
    }

    SliceState state;
    status = state.reset(window);
    if (!status.ok()) {
        return status;
    }

    const SliceDecoder decoder(stream_);
    return decoder.decode(slice, window, state);
}

} // namespace mffv1::codec
