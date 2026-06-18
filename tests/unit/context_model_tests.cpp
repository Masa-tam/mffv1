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

TEST(ContextModelTest, MapsRfcGradientsToQuantizationTables)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 32;
    tables.tables[0][1] = 1;
    tables.tables[1][2] = 2;
    tables.tables[2][3] = 4;
    tables.tables[3][4] = 8;
    tables.tables[4][5] = 16;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({17, 13, 10, 12, 7, 15}, decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 31u);
    EXPECT_FALSE(decision.invert_difference);
}

TEST(ContextModelTest, NegativeFoldUsesMagnitude)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[0][255] = -3;
    tables.tables[1][2] = -4;
    const ffv1::syntax::ContextModel model(tables);
    ffv1::syntax::ContextDecision decision;

    const auto status = model.derive_context({0, 10, 9, 11, 9, 9}, decision);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decision.context, 7u);
    EXPECT_TRUE(decision.invert_difference);
}

TEST(ContextModelTest, RejectsFoldedContextOutsideConfiguredRange)
{
    ffv1::syntax::QuantTableSet tables;
    tables.context_count = 8;
    tables.tables[3][1] = 8;
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
