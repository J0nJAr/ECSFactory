#include "TestHarness.hpp"

#include "ECS/SparseSet.hpp"

TEST_CASE(SparseSet, EmplaceAndGetReturnsStoredValue) {
    EF::SparseSet<unsigned int, int> set;

    set.Emplace(3u, 42);

    EXPECT_TRUE(set.Contains(3u));
    EXPECT_EQ(*set.Get(3u), 42);
}

TEST_CASE(SparseSet, RemoveMaintainsAccessToRemainingElements) {
    EF::SparseSet<unsigned int, int> set;

    set.Emplace(1u, 10);
    set.Emplace(2u, 20);
    set.Emplace(3u, 30);

    set.Remove(2u);

    EXPECT_FALSE(set.Contains(2u));
    EXPECT_TRUE(set.Contains(1u));
    EXPECT_TRUE(set.Contains(3u));
    EXPECT_EQ(*set.Get(1u), 10);
    EXPECT_EQ(*set.Get(3u), 30);
    EXPECT_EQ(set.Size(), std::size_t{2});
}
