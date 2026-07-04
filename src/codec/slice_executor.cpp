#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
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

bool can_try_golomb_rice_read_ahead_boundary(
    const syntax::StreamParameters& stream,
    const syntax::SliceDescriptor& slice) noexcept
{
    return stream.version >= 3
        && stream.entropy_mode == EntropyMode::GolombRice
        && slice.content_byte_offset > slice.payload_byte_offset;
}

syntax::SliceDescriptor make_golomb_rice_read_ahead_boundary(
    const syntax::SliceDescriptor& slice) noexcept
{
    auto candidate = slice;
    --candidate.content_byte_offset;
    return candidate;
}

Status make_temporary_frame(MutableFrameView output,
                            std::vector<std::vector<std::byte>>& storage,
                            std::vector<MutablePlaneView>& planes,
                            MutableFrameView& out_frame)
{
    storage.clear();
    planes.clear();
    storage.reserve(output.plane_count);
    planes.reserve(output.plane_count);

    for (std::size_t i = 0; i < output.plane_count; ++i) {
        const auto& info = output.planes[i].info;
        if (info.stride_bytes < 0) {
            return make_error(ErrorCode::InvalidArgument,
                              "output plane stride is negative");
        }
        const auto stride = static_cast<std::uint64_t>(info.stride_bytes);
        const auto height = static_cast<std::uint64_t>(info.height);
        if (height != 0
            && stride > std::numeric_limits<std::size_t>::max() / height) {
            return make_error(ErrorCode::ResourceExhausted,
                              "temporary output plane size overflows size_t");
        }
        storage.emplace_back(static_cast<std::size_t>(stride * height));
        MutablePlaneView plane;
        plane.data = storage.back().data();
        plane.info = info;
        planes.push_back(plane);
    }

    out_frame = MutableFrameView{planes.data(), planes.size()};
    return ok_status();
}

Status copy_decoded_slice(const SliceOutputWindow& source,
                          const SliceOutputWindow& destination)
{
    if (source.plane_count() != destination.plane_count()) {
        return make_error(ErrorCode::InternalError,
                          "temporary slice plane count does not match output");
    }

    for (std::size_t plane = 0; plane < source.plane_count(); ++plane) {
        const auto width = source.plane_width(plane);
        const auto height = source.plane_height(plane);
        if (width != destination.plane_width(plane)
            || height != destination.plane_height(plane)) {
            return make_error(ErrorCode::InternalError,
                              "temporary slice geometry does not match output");
        }
        for (std::uint32_t y = 0; y < height; ++y) {
            if (const auto* src = source.row_u8(plane, y)) {
                auto* dst = destination.row_u8(plane, y);
                if (dst == nullptr) {
                    return make_error(ErrorCode::InternalError,
                                      "temporary uint8 slice row does not match output");
                }
                std::memcpy(dst, src, width);
            } else if (const auto* src16 = source.row_u16(plane, y)) {
                auto* dst16 = destination.row_u16(plane, y);
                if (dst16 == nullptr) {
                    return make_error(ErrorCode::InternalError,
                                      "temporary uint16 slice row does not match output");
                }
                std::memcpy(dst16, src16, static_cast<std::size_t>(width) * sizeof(std::uint16_t));
            } else {
                return make_error(ErrorCode::InternalError,
                                  "temporary slice row is not accessible");
            }
        }
    }

    return ok_status();
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

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream,
                             int thread_count,
                             const CpuFeatures& cpu) noexcept
    : stream_(stream)
    , kernels_(simd::make_codec_kernels(cpu))
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
    const SliceDecoder decoder(stream_, kernels_);
    for (const auto& slice : slices) {
        SliceOutputWindow window;
        Status status = window.validate(stream_, output, slice);
        if (status.ok()) {
            status = decoder.validate(slice, window);
        }
        if (!status.ok()
            && can_try_golomb_rice_read_ahead_boundary(stream_, slice)) {
            const auto candidate =
                make_golomb_rice_read_ahead_boundary(slice);
            SliceOutputWindow candidate_window;
            Status candidate_status =
                candidate_window.validate(stream_, output, candidate);
            if (candidate_status.ok()) {
                candidate_status = decoder.validate(candidate, candidate_window);
            }
            if (candidate_status.ok()) {
                continue;
            }
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

    const SliceDecoder decoder(stream_, kernels_);
    if (!can_try_golomb_rice_read_ahead_boundary(stream_, slice)) {
        return decoder.decode(slice, window, state);
    }

    std::vector<std::vector<std::byte>> temporary_storage;
    std::vector<MutablePlaneView> temporary_planes;
    MutableFrameView temporary_frame;
    status = make_temporary_frame(
        output, temporary_storage, temporary_planes, temporary_frame);
    if (!status.ok()) {
        return status;
    }

    const std::array candidates{
        slice,
        make_golomb_rice_read_ahead_boundary(slice),
    };
    Status last_status;
    for (const auto& candidate : candidates) {
        SliceOutputWindow candidate_window;
        status = candidate_window.validate(stream_, temporary_frame, candidate);
        if (!status.ok()) {
            last_status = std::move(status);
            continue;
        }
        SliceState candidate_state = state;
        status = decoder.decode(candidate, candidate_window, candidate_state);
        if (!status.ok()) {
            last_status = std::move(status);
            continue;
        }

        status = copy_decoded_slice(candidate_window, window);
        if (!status.ok()) {
            return status;
        }
        state = std::move(candidate_state);
        return ok_status();
    }

    return last_status;
}

} // namespace mffv1::codec
