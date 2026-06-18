#include "ffv1/context_model.hpp"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

TEST(ContextModelTest, RejectsMissingContextCount)
{
    ffv1::syntax::QuantTableSet tables;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({}, decision);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(ContextModelTest, DerivesZeroContextForZeroTables)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 4;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({0, 10, 10, 10, 10, 10}, decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 0u);
    EXPECT_FALSE(decision.invert_difference);
}

TEST(ContextModelTest, FoldsQuantizedGradientsIntoContextRange)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][1] = 3;
    tables.tables[1][1] = 4;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({12, 11, 10, 10, 10, 10}, decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 7u);
    EXPECT_FALSE(decision.invert_difference);
}

TEST(ContextModelTest, NegativeFoldUsesMagnitude)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][255] = -3;
    tables.tables[1][1] = -4;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({10, 11, 10, 10, 10, 10}, decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 7u);
    EXPECT_TRUE(decision.invert_difference);
}

TEST(ContextModelTest, RejectsFoldedContextOutsideConfiguredRange)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][1] = 8;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({1, 0, 0, 0, 0, 0}, decision);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(ContextModelTest, ComputesExtremeGradientsWithoutSignedOverflow)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 1;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;
    const auto minimum = std::numeric_limits<std::int32_t>::min();
    const auto maximum = std::numeric_limits<std::int32_t>::max();

    const auto status = model.derive_context({maximum, minimum, maximum, minimum, maximum, minimum},
                                             decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 0u);
}

} // namespace
