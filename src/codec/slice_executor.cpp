#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstdint>
#include <future>
#include <utility>
#include <vector>

namespace ffv1::codec {

namespace {

std::uint32_t normalize_thread_count(int thread_count) noexcept
{
    if (thread_count <= 1) {
        return 1;
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
    if (thread_count_ <= 1 || slices.size() < 2) {
        return decode_serial(output, slices);
    }
    return decode_parallel(output, slices);
}

std::uint32_t SliceExecutor::thread_count() const noexcept
{
    return thread_count_;
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
    const auto worker_count = std::min<std::size_t>(thread_count_, slices.size());
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
    status = state.reset(stream_);
    if (!status.ok()) {
        return status;
    }

    const SliceDecoder decoder(stream_);
    return decoder.decode(slice, window, state);
}

} // namespace ffv1::codec
