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
#if MFFV1_ENABLE_STATUS_MESSAGES
    status.message = std::move(message);
#else
    (void)message;
#endif
    return status;
}

} // namespace mffv1
