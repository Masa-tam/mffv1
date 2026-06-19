#include "mffv1/result.hpp"

#include <utility>

namespace mffv1 {

bool Status::ok() const noexcept
{
    return code == ErrorCode::Ok;
}

Status ok_status()
{
    return {};
}

Status make_error(ErrorCode code, std::string message)
{
    Status status;
    status.code = code;
    status.message = std::move(message);
    return status;
}

} // namespace mffv1

