#pragma once

#include <span>

#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

Status validate_slice_raster_coverage(const syntax::StreamParameters& stream,
                                      std::span<const syntax::SliceDescriptor> slices);

[[nodiscard]] bool is_incomplete_slice_raster_coverage_status(const Status& status) noexcept;

} // namespace mffv1::codec
