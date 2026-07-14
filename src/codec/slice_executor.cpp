#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "codec/slice_raster_validator.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <numeric>
#include <string>
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
    if (stream.entropy_mode != EntropyMode::GolombRice
        || slice.content_byte_offset <= slice.payload_byte_offset) {
        return false;
    }
    if ((slice.footer_byte_offset != 0 || slice.slice_size != 0)
        && slice.footer_byte_offset <= slice.content_byte_offset - 1) {
        return false;
    }
    if (stream.version >= 3) {
        return true;
    }
    return stream.version <= 1 && slice.content_bit_offset == 0;
}

bool should_prefer_golomb_rice_read_ahead_boundary(
    const syntax::StreamParameters& stream) noexcept
{
    // FFmpeg GR multi-slice streams with an extra plane can share one byte between the
    // range-coded slice header and the Golomb-Rice body even when the primary
    // boundary decodes without a syntax error.
    return stream.version >= 3
        && stream.entropy_mode == EntropyMode::GolombRice
        && stream.extra_plane
        && (stream.num_h_slices > 1 || stream.num_v_slices > 1);
}

syntax::SliceDescriptor make_golomb_rice_read_ahead_boundary(
    const syntax::SliceDescriptor& slice) noexcept
{
    auto candidate = slice;
    --candidate.content_byte_offset;
    return candidate;
}

std::vector<syntax::SliceDescriptor> make_golomb_rice_content_candidates(
    const syntax::StreamParameters& stream,
    const syntax::SliceDescriptor& slice)
{
    std::vector<syntax::SliceDescriptor> candidates;
    if (can_try_golomb_rice_read_ahead_boundary(stream, slice)) {
        const auto read_ahead = make_golomb_rice_read_ahead_boundary(slice);
        if (should_prefer_golomb_rice_read_ahead_boundary(stream)) {
            candidates.push_back(read_ahead);
            candidates.push_back(slice);
        } else {
            candidates.push_back(slice);
            candidates.push_back(read_ahead);
        }
    } else {
        candidates.push_back(slice);
    }
    return candidates;
}

bool slices_have_raster_layout(std::span<const syntax::SliceDescriptor> slices) noexcept
{
    return std::all_of(
        slices.begin(),
        slices.end(),
        [](const syntax::SliceDescriptor& slice) {
            return slice.raster_width != 0 && slice.raster_height != 0;
        });
}

Status validate_parallel_slice_layout(const syntax::StreamParameters& stream,
                                      std::span<const syntax::SliceDescriptor> slices)
{
    if (slices.empty() || !slices_have_raster_layout(slices)) {
        return ok_status();
    }
    return validate_slice_raster_coverage(stream, slices);
}

bool can_decode_parallel_safely(const syntax::StreamParameters& stream,
                                std::span<const syntax::SliceDescriptor> slices)
{
    if (slices.empty() || !slices_have_raster_layout(slices)) {
        return false;
    }
    return validate_slice_raster_coverage(stream, slices).ok();
}

Status make_temporary_frame(const SliceOutputWindow& window,
                            MutableFrameView output,
                            std::vector<std::vector<std::byte>>& storage,
                            std::vector<MutablePlaneView>& planes,
                            MutableFrameView& out_frame)
{
    storage.clear();
    planes.clear();
    const auto required_planes = window.plane_count();
    storage.reserve(required_planes);
    planes.reserve(required_planes);

    for (std::size_t i = 0; i < required_planes; ++i) {
        const auto& info = output.planes[i].info;
        const auto width = window.plane_width(i);
        const auto height = window.plane_height(i);
        if (info.stride_bytes < 0) {
            return make_error(ErrorCode::InvalidArgument,
                              "output plane stride is negative");
        }
        std::uint64_t row_bytes = width;
        if (info.sample_format == SampleFormat::UInt16) {
            if (row_bytes > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint16_t)) {
                return make_error(ErrorCode::ResourceExhausted,
                                  "temporary output plane row size overflows uint64_t");
            }
            row_bytes *= sizeof(std::uint16_t);
        }
        if (row_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
            return make_error(ErrorCode::ResourceExhausted,
                              "temporary output plane row size exceeds ptrdiff_t");
        }
        const auto stride = static_cast<std::ptrdiff_t>(row_bytes);
        if (height != 0 && row_bytes > std::numeric_limits<std::size_t>::max() / height) {
            return make_error(ErrorCode::ResourceExhausted,
                              "temporary output plane size overflows size_t");
        }
        storage.emplace_back(static_cast<std::size_t>(row_bytes * height));
        MutablePlaneView plane;
        plane.data = storage.back().data();
        plane.info = info;
        plane.info.width = width;
        plane.info.height = height;
        plane.info.stride_bytes = stride;
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

void append_golomb_rice_candidate_context(
    Status& status,
    const syntax::SliceDescriptor& candidate)
{
    if constexpr (status_messages_enabled) {
        append_status_message(
            status,
            " while decoding Golomb-Rice content candidate at byte offset "
                + std::to_string(candidate.content_byte_offset));
        if (candidate.content_bit_offset != 0) {
            append_status_message(
                status,
                " bit offset " + std::to_string(candidate.content_bit_offset));
        }
    }
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
    const bool parallel_safe =
        thread_count_ > 1
        && slices.size() >= 2
        && can_decode_parallel_safely(stream_, slices);

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

    if (!parallel_safe) {
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
    if (thread_count_ > 1 && slices.size() >= 2) {
        Status status = validate_parallel_slice_layout(stream_, slices);
        if (!status.ok()) {
            return status;
        }
    }

    const SliceDecoder decoder(stream_, kernels_);
    for (const auto& slice : slices) {
        SliceOutputWindow window;
        Status status = window.validate(stream_, output, slice);
        if (status.ok()) {
            status = decoder.validate(slice, window);
        }
        if (!status.ok()) {
            const auto candidates =
                make_golomb_rice_content_candidates(stream_, slice);
            bool accepted_candidate = false;
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const auto& candidate = candidates[i];
                SliceOutputWindow candidate_window;
                Status candidate_status =
                    candidate_window.validate(stream_, output, candidate);
                if (candidate_status.ok()) {
                    candidate_status = decoder.validate(candidate, candidate_window);
                }
                if (candidate_status.ok()) {
                    accepted_candidate = true;
                    break;
                }
            }
            if (accepted_candidate) {
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

    status = state.reset(stream_, window);
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
        window, output, temporary_storage, temporary_planes, temporary_frame);
    if (!status.ok()) {
        return status;
    }
    SliceOutputWindow temporary_window;
    status = temporary_window.reset_to_contiguous_frame(temporary_frame);
    if (!status.ok()) {
        return status;
    }

    const auto candidates = make_golomb_rice_content_candidates(stream_, slice);
    const SliceDecoder read_ahead_decoder(
        stream_, kernels_, true);
    bool has_first_status = false;
    Status first_status;
    Status last_status;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        const auto& candidate_decoder = read_ahead_decoder;
        SliceState candidate_state = state;
        status = candidate_decoder.decode(candidate, temporary_window, candidate_state);
        if (!status.ok()) {
            append_golomb_rice_candidate_context(status, candidate);
            if (!has_first_status) {
                first_status = status;
                has_first_status = true;
            }
            last_status = std::move(status);
            continue;
        }

        status = copy_decoded_slice(temporary_window, window);
        if (!status.ok()) {
            return status;
        }
        state = std::move(candidate_state);
        return ok_status();
    }

    if (has_first_status) {
        return first_status;
    }
    return last_status;
}

} // namespace mffv1::codec
