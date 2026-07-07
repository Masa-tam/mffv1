#include <mffv1/result.hpp>

#ifndef MFFV1_ENABLE_STATUS_MESSAGES
#error "result.hpp must provide a fallback MFFV1_ENABLE_STATUS_MESSAGES value"
#endif

static_assert(MFFV1_ENABLE_STATUS_MESSAGES == 1);

int main()
{
    mffv1::Status status;
    return status.code == mffv1::ErrorCode::Ok ? 0 : 1;
}
