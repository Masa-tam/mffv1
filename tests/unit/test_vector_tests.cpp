#include "test_vector_data.hpp"

#include <gtest/gtest.h>

TEST(TestVectorTest, GeneratedVectorsAreAvailable)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    SUCCEED() << "external FFV1 test vectors are available";
#endif
}
