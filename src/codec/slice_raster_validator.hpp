#pragma once

#include <span>

#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

Status validate_slice_raster_coverage(const syntax::StreamParameters& stream,
                                      std::span<const syntax::SliceDescriptor> slices);

} // namespace ffv1::codec
