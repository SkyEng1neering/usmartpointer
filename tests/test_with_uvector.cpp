/**
 * @file test_with_uvector.cpp
 * @brief Tests for usmart_ptr combined with uvector
 *
 * Covers:
 *   uvector<usmart_ptr<T>>          — the production pattern (CompositeModifier::chain_)
 *   usmart_ptr<uvector<T>>          — heap-allocated vector owned by a smart pointer
 *   uvector<usmart_ptr<uvector<T>>> — smart-pointer-owned vectors stored in an outer vector
 *
 * All tests use single-heap mode (USE_SINGLE_HEAP_MEMORY).
 *
 * The critical scenario: when uvector grows (reserve), it move-assigns each
 * usmart_ptr element via replace_pointers, then frees the old outer buffer →
 * defrag.  Bug #1 in defrag_memory() caused alloc_info entries at k < freed_index
 * whose .ptr was inside the new buffer (relocated by replace_pointers) to not be
 * address-adjusted.  Both bugs are fixed in dalloc; these tests verify that fix.
 */

#include "usmart_ptr_test_config.h"

/* ---- uvector include with debug output suppressed ---- */
#ifdef uvector_debug
#undef uvector_debug
#endif
#include "uvector.h"
#ifdef uvector_debug
#undef uvector_debug
#endif
#define uvector_debug(...) ((void)0)

// ==================== Integrity-checking helper ====================

/**
 * Minimal struct with magic numbers to detect memory corruption.
 * Designed to fit inside a usmart_ptr allocation and survive heap defrag.
 */
struct IntegrityBox {
    static constexpr uint32_t ALIVE = 0xBEEFCAFEu;
    static constexpr uint32_t DEAD  = 0xDEAD0000u;

    uint32_t magic;
    int      value;
    uint32_t tail;

    explicit IntegrityBox(int v = 0) : magic(ALIVE), value(v), tail(ALIVE) {}

    ~IntegrityBox() {
        if (magic != ALIVE || tail != ALIVE) {
            ADD_FAILURE() << "IntegrityBox destroyed in corrupted state"
                          << " magic=0x" << std::hex << magic
                          << " tail=0x"  << tail << std::dec
                          << " value="   << value;
        }
        magic = DEAD;
        tail  = DEAD;
    }

    bool ok() const { return magic == ALIVE && tail == ALIVE; }
};

// ==================== Large-heap fixture (single-heap mode) ====================

static constexpr size_t  kLargeHeapSize  = 16384;
static constexpr size_t  kLargeCanarySz  = 64;
static constexpr uint8_t kLargeCanaryPat = 0xCD;

class LargeHeapFixture : public ::testing::Test {
protected:
    uint8_t             canary_before[kLargeCanarySz];
    alignas(4) uint8_t  heap_memory[kLargeHeapSize];
    uint8_t             canary_after[kLargeCanarySz];

    void SetUp() override {
        std::memset(canary_before, kLargeCanaryPat, kLargeCanarySz);
        std::memset(canary_after,  kLargeCanaryPat, kLargeCanarySz);
        dalloc_unregister_heap(true);
        dalloc_register_heap(heap_memory, kLargeHeapSize);
        ASSERT_NE(dalloc_get_default_heap(), nullptr);
    }

    void TearDown() override {
        dalloc_unregister_heap(true);
        for (size_t i = 0; i < kLargeCanarySz; i++) {
            EXPECT_EQ(canary_before[i], kLargeCanaryPat) << "underflow at " << i;
            EXPECT_EQ(canary_after[i],  kLargeCanaryPat) << "overflow  at " << i;
        }
    }
};

// ==================== uvector<usmart_ptr<T>> ====================

// 12 push_backs → ~9 outer reallocations.  Each realloc move-assigns usmart_ptrs
// (replace_pointers) then frees old outer buffer → defrag.  Bug #1 path exercised
// because usmart_ptr payload tracking entries have k < freed_index for old outer buf.
TEST_F(LargeHeapFixture, SmartPtrsInVector_IntegerValues) {
    uvector<usmart_ptr<int>> vec;

    for (int i = 0; i < 12; i++) {
        usmart_ptr<int> p;
        ASSERT_TRUE(p.create(i * 7));
        ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<int>&&>(p)));
    }

    ASSERT_EQ(vec.size(), 12u);
    for (int i = 0; i < 12; i++) {
        ASSERT_TRUE(bool(vec.at((uint32_t)i))) << "ptr[" << i << "] null";
        EXPECT_EQ(*vec.at((uint32_t)i), i * 7) << "value[" << i << "]";
    }
}

// IntegrityBox: magic numbers detect if the payload was corrupted during defrag.
TEST_F(LargeHeapFixture, SmartPtrsInVector_IntegrityBoxesAfterRealloc) {
    {
        uvector<usmart_ptr<IntegrityBox>> vec;

        for (int i = 0; i < 12; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i * 3));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        ASSERT_EQ(vec.size(), 12u);
        for (int i = 0; i < 12; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i))) << "ptr[" << i << "] null";
            EXPECT_TRUE(vec.at((uint32_t)i)->ok())  << "obj[" << i << "] corrupted";
            EXPECT_EQ(vec.at((uint32_t)i)->value, i * 3);
        }
    }
    // Destructor of vec → each usmart_ptr frees IntegrityBox; IntegrityBox::~IntegrityBox
    // fires ADD_FAILURE() on bad magic, so test fails if any corruption occurred.
}

// Push in 3 batches, verify all elements after each batch to catch corruption early.
TEST_F(LargeHeapFixture, SmartPtrsInVector_VerifyAfterEachBatch) {
    uvector<usmart_ptr<IntegrityBox>> vec;

    for (int batch = 0; batch < 3; batch++) {
        for (int i = 0; i < 5; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(batch * 100 + i));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        uint32_t sz = (uint32_t)((batch + 1) * 5);
        ASSERT_EQ(vec.size(), sz) << "after batch " << batch;
        for (uint32_t k = 0; k < sz; k++) {
            ASSERT_TRUE(bool(vec.at(k)))
                << "batch=" << batch << " ptr[" << k << "] null";
            EXPECT_TRUE(vec.at(k)->ok())
                << "batch=" << batch << " obj[" << k << "] corrupted";
            int expected = (int)(k / 5) * 100 + (int)(k % 5);
            EXPECT_EQ(vec.at(k)->value, expected)
                << "batch=" << batch << " obj[" << k << "] value";
        }
    }
}

// pop() shifts elements left via move-assign (replace_pointers chain),
// then push more to trigger realloc in the modified tracking state.
TEST_F(LargeHeapFixture, SmartPtrsInVector_PopThenGrow) {
    {
        uvector<usmart_ptr<IntegrityBox>> vec;

        for (int i = 0; i < 8; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        ASSERT_TRUE(vec.pop(2));
        ASSERT_EQ(vec.size(), 7u);

        // Remaining: [0,1,3,4,5,6,7]
        int after_pop[] = {0, 1, 3, 4, 5, 6, 7};
        for (int i = 0; i < 7; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i)));
            EXPECT_TRUE(vec.at((uint32_t)i)->ok());
            EXPECT_EQ(vec.at((uint32_t)i)->value, after_pop[i]);
        }

        // Push 4 more → realloc after the modified element layout
        for (int i = 8; i < 12; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        int final_vals[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        ASSERT_EQ(vec.size(), 11u);
        for (int i = 0; i < 11; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i)));
            EXPECT_TRUE(vec.at((uint32_t)i)->ok());
            EXPECT_EQ(vec.at((uint32_t)i)->value, final_vals[i]);
        }
    }
}

// Pre-reserve large capacity, push few elements, shrink_to_fit.
// shrink moves usmart_ptrs to exact-size buffer (replace_pointers) then frees
// oversized buffer → defrag.
TEST_F(LargeHeapFixture, SmartPtrsInVector_ShrinkToFit) {
    {
        uvector<usmart_ptr<IntegrityBox>> vec(20);

        for (int i = 0; i < 5; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i * 3));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        EXPECT_GE(vec.capacity(), 20u);
        ASSERT_TRUE(vec.shrink_to_fit());

        ASSERT_EQ(vec.size(), 5u);
        for (int i = 0; i < 5; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i)));
            EXPECT_TRUE(vec.at((uint32_t)i)->ok());
            EXPECT_EQ(vec.at((uint32_t)i)->value, i * 3);
        }
    }
}

// swap() does a three-way replace_pointers; subsequent realloc touches the
// swapped tracking entries.  Verifies tracking stays consistent end-to-end.
TEST_F(LargeHeapFixture, SmartPtrsInVector_SwapThenGrow) {
    {
        uvector<usmart_ptr<IntegrityBox>> vec;

        for (int i = 0; i < 6; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        vec.at(1).swap(vec.at(4));

        // After swap: [0,4,2,3,1,5]
        int after_swap[] = {0, 4, 2, 3, 1, 5};
        for (int i = 0; i < 6; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i)));
            EXPECT_TRUE(vec.at((uint32_t)i)->ok());
            EXPECT_EQ(vec.at((uint32_t)i)->value, after_swap[i]);
        }

        // Push 4 more to trigger realloc on the swapped tracking layout
        for (int i = 6; i < 10; i++) {
            usmart_ptr<IntegrityBox> p;
            ASSERT_TRUE(p.create(i));
            ASSERT_TRUE(vec.push_back(static_cast<usmart_ptr<IntegrityBox>&&>(p)));
        }

        int final_vals[] = {0, 4, 2, 3, 1, 5, 6, 7, 8, 9};
        ASSERT_EQ(vec.size(), 10u);
        for (int i = 0; i < 10; i++) {
            ASSERT_TRUE(bool(vec.at((uint32_t)i)));
            EXPECT_TRUE(vec.at((uint32_t)i)->ok());
            EXPECT_EQ(vec.at((uint32_t)i)->value, final_vals[i]);
        }
    }
}

// ==================== usmart_ptr<uvector<T>> ====================

// The uvector struct itself is on the heap; its container_ptr tracking lives
// inside the struct.  Each inner push triggers reserve → dfree old inner buf →
// defrag that must update both the usmart_ptr value (ptr_ → uvector struct) and
// the container_ptr tracking address (inside the shifted struct).
TEST_F(LargeHeapFixture, VectorInSmartPtr_GrowthAndVerify) {
    usmart_ptr<uvector<int>> p;
    ASSERT_TRUE(p.create());  // creates uvector<int>() with default heap

    for (int i = 0; i < 20; i++) {
        ASSERT_TRUE(p->push_back(i)) << "push " << i;
    }

    ASSERT_EQ(p->size(), 20u);
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(p->at((uint32_t)i), i);
    }
}

// Move ownership to a second smart pointer after initial growth, then keep growing.
// replace_pointers transfers tracking of the uvector struct to p2.ptr_; subsequent
// inner vector growth must still find tracking correct.
TEST_F(LargeHeapFixture, VectorInSmartPtr_MoveOwnershipThenGrow) {
    usmart_ptr<uvector<int>> p1;
    ASSERT_TRUE(p1.create());

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(p1->push_back(i));
    }

    usmart_ptr<uvector<int>> p2;
    p2 = static_cast<usmart_ptr<uvector<int>>&&>(p1);

    EXPECT_FALSE(bool(p1));
    ASSERT_TRUE(bool(p2));
    ASSERT_EQ(p2->size(), 10u);

    for (int i = 10; i < 25; i++) {
        ASSERT_TRUE(p2->push_back(i));
    }
    ASSERT_EQ(p2->size(), 25u);
    for (int i = 0; i < 25; i++) {
        EXPECT_EQ(p2->at((uint32_t)i), i);
    }
}

// Ballast allocations flank the heap-allocated vector.  Freeing ballast1 causes
// defrag that shifts the uvector struct on the heap; the usmart_ptr's ptr_ value
// must be decremented (Bug #2 fix).  Growing the vector afterwards verifies this.
TEST_F(LargeHeapFixture, VectorInSmartPtr_FlankedByAllocsDefragThenGrow) {
    usmart_ptr<int> ballast1;
    ASSERT_TRUE(ballast1.create(111));

    usmart_ptr<uvector<int>> p;
    ASSERT_TRUE(p.create());

    usmart_ptr<int> ballast2;
    ASSERT_TRUE(ballast2.create(222));

    for (int i = 0; i < 15; i++) {
        ASSERT_TRUE(p->push_back(i));
    }

    // Free ballast1 → defrag shifts uvector struct and its content buffer
    ballast1 = nullptr;

    // Grow vector after defrag: must use updated tracking
    for (int i = 15; i < 25; i++) {
        ASSERT_TRUE(p->push_back(i)) << "push " << i << " after defrag";
    }

    ASSERT_EQ(p->size(), 25u);
    for (int i = 0; i < 25; i++) {
        EXPECT_EQ(p->at((uint32_t)i), i);
    }
    EXPECT_EQ(*ballast2, 222);
}

// ==================== uvector<usmart_ptr<uvector<T>>> ====================

// Outer vector holds smart pointers, each owning an inner vector.
// Build phase: each outer push triggers outer realloc (usmart_ptr replace_pointers)
// while each inner vector also grows separately.
// Extend phase: grow each inner vector from 5→10 elements; each push triggers
// inner defrag that may shift the usmart_ptr-owned uvector struct on the heap,
// requiring correct Bug #2 / Bug #1 handling.
TEST_F(LargeHeapFixture, SmartPtrOfVectors_BuildAndExtend) {
    uvector<usmart_ptr<uvector<int>>> outer;

    for (int i = 0; i < 6; i++) {
        usmart_ptr<uvector<int>> p;
        ASSERT_TRUE(p.create());
        for (int j = 0; j < 5; j++) {
            ASSERT_TRUE(p->push_back(i * 10 + j));
        }
        ASSERT_TRUE(outer.push_back(static_cast<usmart_ptr<uvector<int>>&&>(p)));
    }

    ASSERT_EQ(outer.size(), 6u);
    for (int i = 0; i < 6; i++) {
        ASSERT_TRUE(bool(outer.at((uint32_t)i)));
        ASSERT_EQ(outer.at((uint32_t)i)->size(), 5u);
        for (int j = 0; j < 5; j++) {
            EXPECT_EQ(outer.at((uint32_t)i)->at((uint32_t)j), i * 10 + j);
        }
    }

    // Extend each inner vector
    for (uint32_t i = 0; i < outer.size(); i++) {
        for (int j = 5; j < 10; j++) {
            ASSERT_TRUE(outer.at(i)->push_back((int)i * 10 + j))
                << "outer[" << i << "].push_back(" << j << ")";
        }
    }

    for (int i = 0; i < 6; i++) {
        ASSERT_EQ(outer.at((uint32_t)i)->size(), 10u) << "inner[" << i << "] size";
        for (int j = 0; j < 10; j++) {
            EXPECT_EQ(outer.at((uint32_t)i)->at((uint32_t)j), i * 10 + j)
                << "outer[" << i << "][" << j << "]";
        }
    }
}

// Pop one element from the outer vector after the extend phase.
// pop() shifts elements via move-assign (replace_pointers) for each surviving
// smart pointer; verify inner vector contents remain correct after shifting.
TEST_F(LargeHeapFixture, SmartPtrOfVectors_PopAfterExtend) {
    uvector<usmart_ptr<uvector<int>>> outer;

    for (int i = 0; i < 5; i++) {
        usmart_ptr<uvector<int>> p;
        ASSERT_TRUE(p.create());
        for (int j = 0; j < 6; j++) {
            ASSERT_TRUE(p->push_back(i * 10 + j));
        }
        ASSERT_TRUE(outer.push_back(static_cast<usmart_ptr<uvector<int>>&&>(p)));
    }

    ASSERT_EQ(outer.size(), 5u);

    // Remove index 1 → shifts [2,3,4] left
    ASSERT_TRUE(outer.pop(1));
    ASSERT_EQ(outer.size(), 4u);

    // Remaining: originally [0,2,3,4]
    int ids[] = {0, 2, 3, 4};
    for (int k = 0; k < 4; k++) {
        int i = ids[k];
        ASSERT_TRUE(bool(outer.at((uint32_t)k))) << "ptr[" << k << "] null";
        ASSERT_EQ(outer.at((uint32_t)k)->size(), 6u) << "inner[" << k << "] size";
        for (int j = 0; j < 6; j++) {
            EXPECT_EQ(outer.at((uint32_t)k)->at((uint32_t)j), i * 10 + j)
                << "outer[" << k << "][" << j << "]";
        }
    }
}

// Push more elements to the outer vector after a pop to force realloc.
TEST_F(LargeHeapFixture, SmartPtrOfVectors_GrowAfterPop) {
    uvector<usmart_ptr<uvector<int>>> outer;

    for (int i = 0; i < 5; i++) {
        usmart_ptr<uvector<int>> p;
        ASSERT_TRUE(p.create());
        for (int j = 0; j < 4; j++) {
            ASSERT_TRUE(p->push_back(i * 10 + j));
        }
        ASSERT_TRUE(outer.push_back(static_cast<usmart_ptr<uvector<int>>&&>(p)));
    }

    ASSERT_TRUE(outer.pop(2));
    ASSERT_EQ(outer.size(), 4u);

    // Push 4 more to force realloc on the post-pop layout
    for (int i = 5; i < 9; i++) {
        usmart_ptr<uvector<int>> p;
        ASSERT_TRUE(p.create());
        for (int j = 0; j < 4; j++) {
            ASSERT_TRUE(p->push_back(i * 10 + j));
        }
        ASSERT_TRUE(outer.push_back(static_cast<usmart_ptr<uvector<int>>&&>(p)));
    }

    ASSERT_EQ(outer.size(), 8u);

    // Verify all 8 inner vectors: original [0,1,3,4] then [5,6,7,8]
    int ids[] = {0, 1, 3, 4, 5, 6, 7, 8};
    for (int k = 0; k < 8; k++) {
        int i = ids[k];
        ASSERT_TRUE(bool(outer.at((uint32_t)k))) << "ptr[" << k << "] null";
        ASSERT_EQ(outer.at((uint32_t)k)->size(), 4u) << "inner[" << k << "] size";
        for (int j = 0; j < 4; j++) {
            EXPECT_EQ(outer.at((uint32_t)k)->at((uint32_t)j), i * 10 + j)
                << "outer[" << k << "][" << j << "]";
        }
    }
}
