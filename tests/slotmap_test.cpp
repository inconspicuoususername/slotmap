#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

import slotmap;

namespace {
    using inco::SparseSlotMap;

    // wrapper type with static lifetime tracking
    struct Tracked {
        static inline int live = 0;
        int value = 0;

        Tracked() { ++live; }
        explicit Tracked(int v) : value(v) { ++live; }
        Tracked(const Tracked &o) : value(o.value) { ++live; }
        Tracked(Tracked &&o) noexcept : value(o.value) { ++live; }

        Tracked &operator=(const Tracked &) = default;

        Tracked &operator=(Tracked &&) noexcept = default;

        ~Tracked() { --live; }
    };

    TEST(SlotMap, DefaultConstructedIsEmpty) {
        const SparseSlotMap<int> map;
        EXPECT_TRUE(map.empty());
        EXPECT_EQ(map.size(), 0u);
    }

    TEST(SlotMap, EmplaceReturnsValidKeyAndFinds) {
        SparseSlotMap<int> map;
        const auto k = map.emplace_back(42);

        ASSERT_TRUE(k.valid());
        EXPECT_FALSE(map.empty());
        EXPECT_EQ(map.size(), 1u);
        EXPECT_TRUE(map.contains(k));

        int *p = map.find(k);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, 42);
    }

    TEST(SlotMap, FindWithNullKeyReturnsNullptr) {
        SparseSlotMap<int> map;
        const inco::Key<int> null_key{}; // version 0 -> invalid
        EXPECT_FALSE(null_key.valid());
        EXPECT_EQ(map.find(null_key), nullptr);
        EXPECT_FALSE(map.contains(null_key));
    }

    TEST(SlotMap, MultipleElementsEachFindable) {
        SparseSlotMap<int> map;
        std::vector<inco::Key<int> > keys;
        for (int i = 0; i < 256; ++i) {
            keys.push_back(map.emplace_back(i * 10));
        }

        EXPECT_EQ(map.size(), 256u);
        for (int i = 0; i < 256; ++i) {
            int *p = map.find(keys[i]);
            ASSERT_NE(p, nullptr) << "missing element " << i;
            EXPECT_EQ(*p, i * 10);
        }
    }

    TEST(SlotMap, EraseRemovesElement) {
        SparseSlotMap<int> map;
        const auto k = map.emplace_back(7);

        EXPECT_TRUE(map.erase(k));
        EXPECT_EQ(map.size(), 0u);
        EXPECT_TRUE(map.empty());
        EXPECT_FALSE(map.contains(k));
        EXPECT_EQ(map.find(k), nullptr);
    }

    TEST(SlotMap, EraseTwiceReturnsFalse) {
        SparseSlotMap<int> map;
        const auto k = map.emplace_back(7);

        EXPECT_TRUE(map.erase(k));
        EXPECT_FALSE(map.erase(k));
    }

    TEST(SlotMap, StaleKeyIsInvalidatedAfterErase) {
        SparseSlotMap<int> map;
        const auto k = map.emplace_back(1);
        ASSERT_TRUE(map.erase(k));

        // The slot may get reused, but the old key must never resolve again.
        const auto k2 = map.emplace_back(2);
        EXPECT_EQ(map.find(k), nullptr);

        int *p = map.find(k2);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, 2);
    }

    TEST(SlotMap, ReusedSlotGetsFreshVersion) {
        SparseSlotMap<int> map;
        const auto k1 = map.emplace_back(1);
        ASSERT_TRUE(map.erase(k1));
        const auto k2 = map.emplace_back(2);

        // Reuse of the same index should bump the version so keys differ.
        EXPECT_EQ(k1.index(), k2.index());
        EXPECT_NE(k1.version(), k2.version());
        EXPECT_NE(k1, k2);
    }

    TEST(SlotMap, ReserveDoesNotChangeSize) {
        SparseSlotMap<int> map;
        map.reserve(1000);
        EXPECT_EQ(map.size(), 0u);
        EXPECT_TRUE(map.empty());

        const auto k = map.emplace_back(5);
        EXPECT_EQ(map.size(), 1u);
        ASSERT_NE(map.find(k), nullptr);
        EXPECT_EQ(*map.find(k), 5);
    }

    TEST(SlotMap, TryEmplaceReturnsValue) {
        SparseSlotMap<int> map;
        const auto result = map.try_emplace_back(99);
        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result->valid());
        ASSERT_NE(map.find(*result), nullptr);
        EXPECT_EQ(*map.find(*result), 99);
    }

    TEST(SlotMap, RangeForVisitsAllLiveElements) {
        SparseSlotMap<int> map;
        std::vector<inco::Key<int> > keys;
        for (int i = 0; i < 100; ++i) {
            keys.push_back(map.emplace_back(i));
        }
        // Erase the even-indexed ones.
        for (int i = 0; i < 100; i += 2) {
            ASSERT_TRUE(map.erase(keys[i]));
        }

        long long sum = 0;
        std::size_t count = 0;
        for (const auto &[idx, value]: map) {
            sum += value;
            ++count;
        }

        // Remaining are the odd values 1,3,...,99.
        EXPECT_EQ(count, 50u);
        EXPECT_EQ(map.size(), 50u);
        long long expected = 0;
        for (int i = 1; i < 100; i += 2) expected += i;
        EXPECT_EQ(sum, expected);
    }

    TEST(SlotMap, UnrolledPipeVisitsAllLiveElements) {
        SparseSlotMap<int> map;
        for (int i = 0; i < 200; ++i) map.emplace_back(1);

        std::size_t count = 0;
        map | inco::unrolled([&](auto /*idx*/, auto &value) {
            count += static_cast<std::size_t>(value);
        });
        EXPECT_EQ(count, 200u);
    }

    TEST(SlotMap, RunsElementLifetimes) {
        ASSERT_EQ(Tracked::live, 0);
        {
            SparseSlotMap<Tracked> map;
            const auto k = map.emplace_back(11);
            EXPECT_EQ(Tracked::live, 1);
            ASSERT_NE(map.find(k), nullptr);
            EXPECT_EQ(map.find(k)->value, 11);

            map.emplace_back(22);
            EXPECT_EQ(Tracked::live, 2);

            EXPECT_TRUE(map.erase(k));
            EXPECT_EQ(Tracked::live, 1);
        }
        // Destroying the map must destroy remaining elements.
        EXPECT_EQ(Tracked::live, 0);
    }

    TEST(SlotMap, AtReturnsValueCopy) {
        SparseSlotMap<int> map;
        const auto k = map.emplace_back(55);

        const auto v = map.at(k);
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, 55);

        constexpr inco::Key<int> missing{};
        EXPECT_FALSE(map.at(missing).has_value());

        ASSERT_TRUE(map.erase(k));
        EXPECT_FALSE(map.at(k).has_value());
    }

    TEST(SlotMap, MoveConstructTransfersElements) {
        SparseSlotMap<int> a;
        auto k1 = a.emplace_back(10);
        auto k2 = a.emplace_back(20);

        SparseSlotMap<int> b(std::move(a));

        EXPECT_EQ(b.size(), 2u);
        ASSERT_NE(b.find(k1), nullptr);
        EXPECT_EQ(*b.find(k1), 10);
        ASSERT_NE(b.find(k2), nullptr);
        EXPECT_EQ(*b.find(k2), 20);
    }

    TEST(SlotMap, MoveAssignTransfersElements) {
        SparseSlotMap<int> a;
        auto k = a.emplace_back(7);

        SparseSlotMap<int> b;
        b.emplace_back(99); // b's own element must be dropped by the assignment

        b = std::move(a);

        EXPECT_EQ(b.size(), 1u);
        ASSERT_NE(b.find(k), nullptr);
        EXPECT_EQ(*b.find(k), 7);
    }

    TEST(SlotMap, MovedFromMapCanBeReassigned) {
        SparseSlotMap<int> a;
        a.emplace_back(1);
        SparseSlotMap<int> b(std::move(a));

        // A moved-from map is "valid but unspecified": destroying or assigning into
        // it must work. Assign a fresh map in, then confirm it is fully usable.
        a = SparseSlotMap<int>{};
        auto k = a.emplace_back(2);
        ASSERT_NE(a.find(k), nullptr);
        EXPECT_EQ(*a.find(k), 2);
        EXPECT_EQ(a.size(), 1u);
    }

    TEST(SlotMap, MovedFromMapIsReusableDirectly) {
        SparseSlotMap<int> a;
        a.emplace_back(1);
        a.emplace_back(2);

        SparseSlotMap<int> b(std::move(a));

        // After the move, a is empty and can be used again without reassignment.
        EXPECT_EQ(a.size(), 0u);
        EXPECT_TRUE(a.empty());

        auto k1 = a.emplace_back(10);
        auto k2 = a.emplace_back(20);
        EXPECT_EQ(a.size(), 2u);
        ASSERT_NE(a.find(k1), nullptr);
        EXPECT_EQ(*a.find(k1), 10);
        ASSERT_NE(a.find(k2), nullptr);
        EXPECT_EQ(*a.find(k2), 20);

        // b keeps the original contents intact.
        EXPECT_EQ(b.size(), 2u);
    }

    TEST(SlotMap, ReuseAfterMoveHandlesLifetimes) {
        ASSERT_EQ(Tracked::live, 0);
        {
            SparseSlotMap<Tracked> a;
            a.emplace_back(1);
            SparseSlotMap<Tracked> b(std::move(a));
            EXPECT_EQ(Tracked::live, 1);

            // Reusing the moved-from map allocates fresh, tracked elements.
            a.emplace_back(2);
            a.emplace_back(3);
            EXPECT_EQ(Tracked::live, 3);
        }
        EXPECT_EQ(Tracked::live, 0);
    }

    TEST(SlotMap, MoveConstructPreservesLifetimes) {
        ASSERT_EQ(Tracked::live, 0);
        {
            SparseSlotMap<Tracked> a;
            a.emplace_back(1);
            a.emplace_back(2);
            EXPECT_EQ(Tracked::live, 2);

            SparseSlotMap<Tracked> b(std::move(a));
            // Storage pages move wholesale, so no elements are copied or leaked.
            EXPECT_EQ(Tracked::live, 2);
        }
        // Neither the moved-from a nor b may double-destroy or leak.
        EXPECT_EQ(Tracked::live, 0);
    }

    TEST(SlotMap, MoveAssignReleasesOldElements) {
        ASSERT_EQ(Tracked::live, 0);
        {
            SparseSlotMap<Tracked> a;
            a.emplace_back(1);

            SparseSlotMap<Tracked> b;
            b.emplace_back(2);
            b.emplace_back(3);
            EXPECT_EQ(Tracked::live, 3);

            b = std::move(a); // b's two elements destroyed, a's one transferred
            EXPECT_EQ(Tracked::live, 1);
        }
        EXPECT_EQ(Tracked::live, 0);
    }

    TEST(SlotMap, WorksWithStructValues) {
        struct Point {
            int x;
            int y;
        };
        SparseSlotMap<Point> map;
        const auto k = map.emplace_back(Point{.x = 3, .y = 4});
        auto *p = map.find(k);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->x, 3);
        EXPECT_EQ(p->y, 4);
    }

    template<class Map>
    class SlotMapBackend : public ::testing::Test {
    };

    using BackendConfigs = ::testing::Types<
        // Finders (default SplitStore storage)
        SparseSlotMap<int, int, inco::HierarchicalBitmap>,
        SparseSlotMap<int, int, inco::BoundedDeferBitmap>,
        SparseSlotMap<int, int, inco::UnboundedDeferBitmap>,
        SparseSlotMap<int, int, inco::LiveAllocBitmap>,
        SparseSlotMap<int, int, inco::FreeList>,
        // Storage backends (default LiveAllocBitmap finder)
        SparseSlotMap<int, int, inco::LiveAllocBitmap, inco::SplitStore<int> >,
        SparseSlotMap<int, int, inco::LiveAllocBitmap, inco::SoAStore<int> >
    >;

    TYPED_TEST_SUITE(SlotMapBackend, BackendConfigs);

    TYPED_TEST(SlotMapBackend, EmplaceFindErase) {
        using Map = TypeParam;
        using Key = typename Map::key_type;

        Map map;
        std::vector<Key> keys;
        for (int i = 0; i < 300; ++i) keys.push_back(map.emplace_back(i));
        EXPECT_EQ(map.size(), 300u);

        for (int i = 0; i < 300; ++i) {
            auto *p = map.find(keys[i]);
            ASSERT_NE(p, nullptr) << "missing " << i;
            EXPECT_EQ(*p, i);
        }

        for (int i = 0; i < 300; i += 2)
            EXPECT_TRUE(map.erase(keys[i]));
        EXPECT_EQ(map.size(), 150u);

        for (int i = 0; i < 300; ++i) {
            if (i % 2 == 0) {
                EXPECT_EQ(map.find(keys[i]), nullptr);
            } else {
                auto *p = map.find(keys[i]);
                ASSERT_NE(p, nullptr);
                EXPECT_EQ(*p, i);
            }
        }
    }

    TYPED_TEST(SlotMapBackend, IterationVisitsLiveElements) {
        using Map = TypeParam;
        using Key = typename Map::key_type;

        Map map;
        std::vector<Key> keys;
        for (int i = 0; i < 300; ++i) keys.push_back(map.emplace_back(i));
        for (int i = 0; i < 300; i += 2)
            ASSERT_TRUE(map.erase(keys[i]));

        long long sum = 0;
        std::size_t count = 0;
        for (const auto &[idx, value]: map) {
            sum += value;
            ++count;
        }

        long long expected = 0;
        for (int i = 1; i < 300; i += 2) expected += i;
        EXPECT_EQ(count, 150u);
        EXPECT_EQ(sum, expected);
    }

    TYPED_TEST(SlotMapBackend, StaleKeyIsRejected) {
        using Map = TypeParam;

        Map map;
        auto k = map.emplace_back(5);
        ASSERT_TRUE(map.erase(k));
        EXPECT_EQ(map.find(k), nullptr);

        // Whether or not the slot is reused, the new key resolves and the old one
        // stays dead.
        auto k2 = map.emplace_back(6);
        EXPECT_EQ(map.find(k), nullptr);
        auto *p = map.find(k2);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, 6);
    }

    // FreeList has no bitmap, so its destructor destroys live elements through the
    // FreeListIter fallback path. Make sure that runs T's destructor exactly once.
    TEST(SlotMapFreeList, DestroysElementLifetimes) {
        using FreeListMap =
                SparseSlotMap<Tracked, Tracked, inco::FreeList>;

        ASSERT_EQ(Tracked::live, 0);
        {
            FreeListMap map;
            const auto k = map.emplace_back(1);
            map.emplace_back(2);
            map.emplace_back(3);
            EXPECT_EQ(Tracked::live, 3);

            EXPECT_TRUE(map.erase(k));
            EXPECT_EQ(Tracked::live, 2);
        }
        EXPECT_EQ(Tracked::live, 0);
    }
}
