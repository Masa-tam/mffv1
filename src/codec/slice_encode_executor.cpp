#include "codec/slice_encode_executor.hpp"

#include "codec/frame_validator.hpp"
#include "codec/slice_encoder.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
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

Status checked_slice_count(const syntax::StreamParameters& stream,
                           std::size_t& out_count)
{
    const auto count =
        static_cast<std::uint64_t>(stream.num_h_slices)
        * static_cast<std::uint64_t>(stream.num_v_slices);
    if (count == 0
        || count
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "encoder slice count is not representable");
    }
    out_count = static_cast<std::size_t>(count);
    return ok_status();
}

} // namespace

SliceEncodeExecutor::SliceEncodeExecutor(
    const syntax::StreamParameters& stream) noexcept
    : SliceEncodeExecutor(stream, 1)
{
}

SliceEncodeExecutor::SliceEncodeExecutor(
    const syntax::StreamParameters& stream,
    int thread_count) noexcept
    : stream_(stream)
    , thread_count_(normalize_thread_count(thread_count))
{
}

Status SliceEncodeExecutor::encode(
    FrameView input,
    std::vector<std::byte>& out_frame) const
{
    const FrameValidator validator;
    Status status = validator.validate_input(stream_, input);
    if (!status.ok()) {
        return status;
    }

    std::size_t slice_count = 0;
    status = checked_slice_count(stream_, slice_count);
    if (!status.ok()) {
        return status;
    }
    const std::vector<std::vector<std::byte>> empty_slices;
    const std::vector<Status> empty_statuses;
    if (slice_count > empty_slices.max_size()
        || slice_count > empty_statuses.max_size()) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "encoder slice count exceeds container capacity");
    }
    std::vector<std::vector<std::byte>> slices(slice_count);
    std::vector<Status> statuses(slice_count);

    const auto worker_count = worker_count_for(slice_count);
    if (worker_count <= 1) {
        for (std::size_t index = 0; index < slice_count; ++index) {
            statuses[index] = encode_slice(input, index, slices[index]);
        }
    } else {
        std::vector<std::future<Status>> futures;
        futures.reserve(worker_count);
        for (std::size_t offset = 0;
             offset < slice_count;
             offset += worker_count) {
            const auto batch_size =
                std::min(worker_count, slice_count - offset);
            futures.clear();
            for (std::size_t index = 0; index < batch_size; ++index) {
                const auto slice_index = offset + index;
                futures.push_back(std::async(
                    std::launch::async,
                    [this, input, slice_index, &slices]() {
                        return encode_slice(
                            input,
                            slice_index,
                            slices[slice_index]);
                    }));
            }
            for (std::size_t index = 0; index < batch_size; ++index) {
                statuses[offset + index] = futures[index].get();
            }
        }
    }

    for (std::size_t index = 0; index < slice_count; ++index) {
        if (!statuses[index].ok()) {
            set_slice_location_if_missing(
                statuses[index],
                static_cast<std::uint32_t>(index));
            return statuses[index];
        }
    }
    return append_slices(slices, out_frame);
}

std::uint32_t SliceEncodeExecutor::thread_count() const noexcept
{
    return thread_count_;
}

std::size_t SliceEncodeExecutor::worker_count_for(
    std::size_t slice_count) const noexcept
{
    if (slice_count == 0) {
        return 0;
    }
    return std::min<std::size_t>(thread_count_, slice_count);
}

Status SliceEncodeExecutor::encode_slice(
    FrameView input,
    std::size_t slice_index,
    std::vector<std::byte>& out_slice) const
{
    SliceHeaderValues header;
    header.x = static_cast<std::uint32_t>(
        slice_index % stream_.num_h_slices);
    header.y = static_cast<std::uint32_t>(
        slice_index / stream_.num_h_slices);
    header.width = 1;
    header.height = 1;
    header.quant_table_set_indexes.assign(
        syntax::quant_table_set_index_count(stream_), 0);

    const SliceEncoder encoder(stream_);
    return encoder.encode_slice(
        input,
        header,
        slice_index == 0,
        true,
        out_slice);
}

Status SliceEncodeExecutor::append_slices(
    const std::vector<std::vector<std::byte>>& slices,
    std::vector<std::byte>& out_frame) const
{
    std::size_t total_size = 0;
    for (const auto& slice : slices) {
        if (slice.size()
            > std::numeric_limits<std::size_t>::max() - total_size) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "encoded frame size is not representable");
        }
        total_size += slice.size();
    }

    std::vector<std::byte> frame;
    if (total_size > frame.max_size()) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "encoded frame exceeds vector capacity");
    }
    frame.reserve(total_size);
    for (const auto& slice : slices) {
        frame.insert(frame.end(), slice.begin(), slice.end());
    }
    out_frame = std::move(frame);
    return ok_status();
}

} // namespace mffv1::codec
