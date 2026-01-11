/*
 * Unit tests for usmart_ptr
 */

#include "usmart_ptr_test_config.h"

/* TrackedObject static members */
int TrackedObject::construct_count = 0;
int TrackedObject::destruct_count = 0;
int TrackedObject::copy_count = 0;
int TrackedObject::move_count = 0;

/*============================================================================
 * Basic Construction Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, DefaultConstructor_CreatesNullPointer) {
    usmart_ptr<int> p;
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_FALSE(p);
}

TEST_F(UsmartPtrTestFixture, DefaultConstructor_HasHeapSet) {
    usmart_ptr<int> p;
    EXPECT_EQ(p.heap(), dalloc_get_default_heap());
}

TEST_F(UsmartPtrTestFixture, BoolConversion_FalseWhenNull) {
    usmart_ptr<int> p;
    EXPECT_FALSE(p);
    EXPECT_TRUE(p == nullptr);
    EXPECT_FALSE(p != nullptr);
}

TEST_F(UsmartPtrTestFixture, BoolConversion_TrueWhenAllocated) {
    usmart_ptr<int> p;
    p.allocate(1);
    EXPECT_TRUE(p);
    EXPECT_FALSE(p == nullptr);
    EXPECT_TRUE(p != nullptr);
}

/*============================================================================
 * Allocation Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Allocate_SingleElement_Succeeds) {
    usmart_ptr<int> p;
    EXPECT_TRUE(p.allocate(1));
    EXPECT_NE(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Allocate_Array_Succeeds) {
    usmart_ptr<int> p;
    EXPECT_TRUE(p.allocate(100));
    EXPECT_NE(p.get(), nullptr);

    /* Write to array to verify it's accessible */
    for (int i = 0; i < 100; i++) {
        p[i] = i * 2;
    }
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(p[i], i * 2);
    }
}

TEST_F(UsmartPtrTestFixture, Allocate_ZeroElements_Fails) {
    usmart_ptr<int> p;
    EXPECT_FALSE(p.allocate(0));
    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Allocate_TwiceFails) {
    usmart_ptr<int> p;
    EXPECT_TRUE(p.allocate(1));
    EXPECT_FALSE(p.allocate(1));  /* Already allocated */
}

TEST_F(UsmartPtrTestFixture, Allocate_OverflowProtection) {
    /* Test that allocate() rejects sizes that would overflow */
    usmart_ptr<uint64_t> p;  /* sizeof(uint64_t) = 8 */

    /* This would overflow: (SIZE_MAX / 8 + 1) * 8 > SIZE_MAX */
    uint32_t overflow_count = static_cast<uint32_t>((SIZE_MAX / sizeof(uint64_t)) + 1);

    /* On 32-bit systems SIZE_MAX/8 fits in uint32_t, on 64-bit it doesn't */
    /* But the overflow check should still catch it */
    if (overflow_count > 0) {  /* Didn't wrap around in uint32_t */
        EXPECT_FALSE(p.allocate(overflow_count));
        EXPECT_EQ(p.get(), nullptr);
    }
}

TEST_F(UsmartPtrTestFixture, Allocate_PODStruct_Succeeds) {
    usmart_ptr<SimplePOD> p;
    EXPECT_TRUE(p.allocate(1));
    p->x = 42;
    p->y = 3.14f;
    p->c = 'A';
    EXPECT_EQ(p->x, 42);
    EXPECT_FLOAT_EQ(p->y, 3.14f);
    EXPECT_EQ(p->c, 'A');
}

/*============================================================================
 * Create/Destroy Tests (for non-POD types)
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Create_CallsDefaultConstructor) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    EXPECT_TRUE(p.create());
    EXPECT_EQ(TrackedObject::construct_count, 1);
    EXPECT_EQ(p->value, 0);
}

TEST_F(UsmartPtrTestFixture, Create_WithArguments_CallsConstructor) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    EXPECT_TRUE(p.create(42));
    EXPECT_EQ(TrackedObject::construct_count, 1);
    EXPECT_EQ(p->value, 42);
}

TEST_F(UsmartPtrTestFixture, Destroy_CallsDestructor) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    p.create(123);
    EXPECT_EQ(TrackedObject::destruct_count, 0);

    p.destroy();
    EXPECT_EQ(TrackedObject::destruct_count, 1);
    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Destructor_AutomaticallyFreesMemory) {
    TrackedObject::resetCounters();

    {
        usmart_ptr<TrackedObject> p;
        p.create(42);
        EXPECT_EQ(TrackedObject::destruct_count, 0);
    }
    /* usmart_ptr destructor calls reset() which:
       1. Calls destructors for all constructed_ elements
       2. Frees memory via dfree() */
    EXPECT_EQ(TrackedObject::destruct_count, 1);
}

/*============================================================================
 * Move Semantics Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, MoveConstructor_TransfersOwnership) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 42;
    int* raw = p1.get();

    usmart_ptr<int> p2(static_cast<usmart_ptr<int>&&>(p1));

    EXPECT_EQ(p1.get(), nullptr);  /* p1 is now empty */
    EXPECT_EQ(p2.get(), raw);      /* p2 owns the pointer */
    EXPECT_EQ(*p2, 42);
}

TEST_F(UsmartPtrTestFixture, MoveAssignment_TransfersOwnership) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 42;
    int* raw = p1.get();

    usmart_ptr<int> p2;
    p2 = static_cast<usmart_ptr<int>&&>(p1);

    EXPECT_EQ(p1.get(), nullptr);
    EXPECT_EQ(p2.get(), raw);
    EXPECT_EQ(*p2, 42);
}

TEST_F(UsmartPtrTestFixture, MoveAssignment_FreesExistingPointer) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 42;

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 100;

    p2 = static_cast<usmart_ptr<int>&&>(p1);

    /* p2's original pointer should have been freed */
    EXPECT_EQ(*p2, 42);
}

TEST_F(UsmartPtrTestFixture, MoveAssignment_SelfAssignment_Safe) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;

    p = static_cast<usmart_ptr<int>&&>(p);

    /* Should be safe (no-op for self-assignment) */
    EXPECT_EQ(*p, 42);
}

TEST_F(UsmartPtrTestFixture, MoveAssignment_CallsDestructorsOnReplaced) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p1;
    p1.create_array(2);  /* 2 objects */

    usmart_ptr<TrackedObject> p2;
    p2.create_array(3);  /* 3 objects */

    EXPECT_EQ(TrackedObject::construct_count, 5);
    EXPECT_EQ(TrackedObject::destruct_count, 0);

    /* Move p1 into p2 - p2's 3 objects should be destroyed */
    p2 = static_cast<usmart_ptr<TrackedObject>&&>(p1);

    EXPECT_EQ(TrackedObject::destruct_count, 3);  /* p2's original 3 objects */
    EXPECT_EQ(p2.constructed_count(), 2u);        /* Now has p1's 2 objects */
    EXPECT_EQ(p1.get(), nullptr);
}

/*============================================================================
 * Release/Reset Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Release_ReturnsPointerAndClears) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;
    int* raw = p.get();

    int* released = p.release();

    EXPECT_EQ(released, raw);
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_EQ(*released, 42);

    /* Manually free since we released ownership */
    dfree(dalloc_get_default_heap(), (void**)&released, USING_PTR_ADDRESS);
}

TEST_F(UsmartPtrTestFixture, Release_OnNullPointer_ReturnsNull) {
    usmart_ptr<int> p;
    int* released = p.release();
    EXPECT_EQ(released, nullptr);
}

TEST_F(UsmartPtrTestFixture, Release_ResetsConstructedCount) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    p.create_array(3);
    EXPECT_EQ(p.constructed_count(), 3u);

    TrackedObject* raw = p.release();

    /* release() should reset constructed_count to 0 */
    EXPECT_EQ(p.constructed_count(), 0u);
    EXPECT_EQ(p.get(), nullptr);

    /* No destructors called - caller is responsible */
    EXPECT_EQ(TrackedObject::destruct_count, 0);

    /* Manual cleanup: call destructors and free */
    for (int i = 0; i < 3; i++) {
        raw[i].~TrackedObject();
    }
    EXPECT_EQ(TrackedObject::destruct_count, 3);
    dfree(dalloc_get_default_heap(), (void**)&raw, USING_PTR_ADDRESS);
}

TEST_F(UsmartPtrTestFixture, Reset_FreesPointer) {
    usmart_ptr<int> p;
    p.allocate(1);
    EXPECT_NE(p.get(), nullptr);

    p.reset();
    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Reset_OnNullPointer_Safe) {
    usmart_ptr<int> p;
    p.reset();  /* Should be no-op */
    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, NullptrAssignment_Resets) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;

    p = nullptr;
    EXPECT_EQ(p.get(), nullptr);
}

/*============================================================================
 * Swap Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Swap_ExchangesPointers) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 42;
    int* raw1 = p1.get();

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 100;
    int* raw2 = p2.get();

    p1.swap(p2);

    EXPECT_EQ(p1.get(), raw2);
    EXPECT_EQ(p2.get(), raw1);
    EXPECT_EQ(*p1, 100);
    EXPECT_EQ(*p2, 42);
}

TEST_F(UsmartPtrTestFixture, SwapFunction_Works) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 1;

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 2;

    swap(p1, p2);

    EXPECT_EQ(*p1, 2);
    EXPECT_EQ(*p2, 1);
}

TEST_F(UsmartPtrTestFixture, Swap_WithOneNull) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 42;
    int* raw1 = p1.get();

    usmart_ptr<int> p2;  /* null */

    p1.swap(p2);

    EXPECT_EQ(p1.get(), nullptr);
    EXPECT_EQ(p2.get(), raw1);
    EXPECT_EQ(*p2, 42);
}

TEST_F(UsmartPtrTestFixture, Swap_WithOtherNull) {
    usmart_ptr<int> p1;  /* null */

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 99;
    int* raw2 = p2.get();

    p1.swap(p2);

    EXPECT_EQ(p1.get(), raw2);
    EXPECT_EQ(p2.get(), nullptr);
    EXPECT_EQ(*p1, 99);
}

TEST_F(UsmartPtrTestFixture, Swap_BothNull) {
    usmart_ptr<int> p1;
    usmart_ptr<int> p2;

    p1.swap(p2);  /* Should be no-op */

    EXPECT_EQ(p1.get(), nullptr);
    EXPECT_EQ(p2.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Swap_SelfSwap) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;
    int* raw = p.get();

    p.swap(p);  /* Self swap should be no-op */

    EXPECT_EQ(p.get(), raw);
    EXPECT_EQ(*p, 42);
}

TEST_F(UsmartPtrTestFixture, Swap_PreservesAllocatedSize) {
    usmart_ptr<int> p1;
    p1.allocate(5);

    usmart_ptr<int> p2;
    p2.allocate(10);

    p1.swap(p2);

    EXPECT_EQ(p1.allocated_size(), 10u);
    EXPECT_EQ(p2.allocated_size(), 5u);
}

TEST_F(UsmartPtrTestFixture, Swap_ThenFree_NoCorruption) {
    /* This test verifies that after swap, freeing works correctly */
    usmart_ptr<int> p1;
    p1.allocate(3);
    p1[0] = 1; p1[1] = 2; p1[2] = 3;

    usmart_ptr<int> p2;
    p2.allocate(2);
    p2[0] = 10; p2[1] = 20;

    p1.swap(p2);

    /* Now p1 has p2's old allocation, p2 has p1's old allocation */
    EXPECT_EQ(p1.allocated_size(), 2u);
    EXPECT_EQ(p2.allocated_size(), 3u);

    /* Force free p1 first */
    p1.reset();
    EXPECT_EQ(p1.get(), nullptr);

    /* p2 should still be valid */
    EXPECT_EQ(p2[0], 1);
    EXPECT_EQ(p2[1], 2);
    EXPECT_EQ(p2[2], 3);

    /* p2 will be freed in destructor - if heap tracking is wrong, this will crash */
}

/*============================================================================
 * Accessor Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Dereference_Works) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;
    EXPECT_EQ(*p, 42);
}

TEST_F(UsmartPtrTestFixture, ArrowOperator_Works) {
    usmart_ptr<SimplePOD> p;
    p.allocate(1);
    p->x = 42;
    p->y = 3.14f;
    EXPECT_EQ(p->x, 42);
    EXPECT_FLOAT_EQ(p->y, 3.14f);
}

TEST_F(UsmartPtrTestFixture, ArraySubscript_Works) {
    usmart_ptr<int> p;
    p.allocate(5);
    for (int i = 0; i < 5; i++) {
        p[i] = i * 10;
    }
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(p[i], i * 10);
    }
}

/*============================================================================
 * Comparison Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, Comparison_NullptrEquals) {
    usmart_ptr<int> p;
    EXPECT_TRUE(p == nullptr);
    EXPECT_TRUE(nullptr == p);
    EXPECT_FALSE(p != nullptr);
    EXPECT_FALSE(nullptr != p);

    p.allocate(1);
    EXPECT_FALSE(p == nullptr);
    EXPECT_FALSE(nullptr == p);
    EXPECT_TRUE(p != nullptr);
    EXPECT_TRUE(nullptr != p);
}

TEST_F(UsmartPtrTestFixture, Comparison_TwoPointers) {
    usmart_ptr<int> p1;
    usmart_ptr<int> p2;

    EXPECT_TRUE(p1 == p2);  /* Both null */

    p1.allocate(1);
    EXPECT_FALSE(p1 == p2);
    EXPECT_TRUE(p1 != p2);
}

/*============================================================================
 * Factory Function Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, MakeUsmart_CreatesObject) {
    TrackedObject::resetCounters();

    auto p = make_usmart<TrackedObject>(42);

    EXPECT_TRUE(p);
    EXPECT_EQ(p->value, 42);
    EXPECT_EQ(TrackedObject::construct_count, 1);
}

TEST_F(UsmartPtrTestFixture, MakeUsmart_DefaultConstructor) {
    TrackedObject::resetCounters();

    auto p = make_usmart<TrackedObject>();

    EXPECT_TRUE(p);
    EXPECT_EQ(p->value, 0);
}

/*============================================================================
 * Return from Function Tests
 *============================================================================*/

static usmart_ptr<int> createIntPointer(int value) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = value;
    return p;
}

TEST_F(UsmartPtrTestFixture, ReturnFromFunction_MovesOwnership) {
    usmart_ptr<int> p = createIntPointer(42);
    EXPECT_TRUE(p);
    EXPECT_EQ(*p, 42);
}

static usmart_ptr<TrackedObject> createTrackedObject(int value) {
    return make_usmart<TrackedObject>(value);
}

TEST_F(UsmartPtrTestFixture, ReturnMakeUsmart_Works) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p = createTrackedObject(123);
    EXPECT_TRUE(p);
    EXPECT_EQ(p->value, 123);
}

/*============================================================================
 * Stress Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, StressTest_ManyAllocations) {
    for (int i = 0; i < 20; i++) {
        usmart_ptr<int> p;
        ASSERT_TRUE(p.allocate(1));
        *p = i;
        EXPECT_EQ(*p, i);
        /* Automatically freed at end of loop */
    }
}

TEST_F(UsmartPtrTestFixture, StressTest_ManyTrackedObjects) {
    TrackedObject::resetCounters();

    for (int i = 0; i < 20; i++) {
        usmart_ptr<TrackedObject> p;
        p.create(i);
        EXPECT_EQ(p->value, i);
        p.destroy();
    }

    EXPECT_EQ(TrackedObject::construct_count, 20);
    EXPECT_EQ(TrackedObject::destruct_count, 20);
}

/*============================================================================
 * Allocated Size Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, AllocatedSize_InitiallyZero) {
    usmart_ptr<int> p;
    EXPECT_EQ(p.allocated_size(), 0u);
}

TEST_F(UsmartPtrTestFixture, AllocatedSize_AfterAllocate) {
    usmart_ptr<int> p;
    p.allocate(10);
    EXPECT_EQ(p.allocated_size(), 10u);
}

TEST_F(UsmartPtrTestFixture, AllocatedSize_AfterCreate) {
    usmart_ptr<TrackedObject> p;
    p.create(42);
    EXPECT_EQ(p.allocated_size(), 1u);
}

TEST_F(UsmartPtrTestFixture, AllocatedSize_AfterReset) {
    usmart_ptr<int> p;
    p.allocate(5);
    p.reset();
    EXPECT_EQ(p.allocated_size(), 0u);
}

TEST_F(UsmartPtrTestFixture, AllocatedSize_AfterMove) {
    usmart_ptr<int> p1;
    p1.allocate(7);

    usmart_ptr<int> p2(static_cast<usmart_ptr<int>&&>(p1));

    EXPECT_EQ(p1.allocated_size(), 0u);
    EXPECT_EQ(p2.allocated_size(), 7u);
}

/*============================================================================
 * Array Specialization Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, ArraySpec_DefaultConstructor) {
    usmart_ptr<int[]> arr;
    EXPECT_EQ(arr.get(), nullptr);
    EXPECT_FALSE(arr);
    EXPECT_EQ(arr.allocated_size(), 0u);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Allocate) {
    usmart_ptr<int[]> arr;
    EXPECT_TRUE(arr.allocate(100));
    EXPECT_NE(arr.get(), nullptr);
    EXPECT_EQ(arr.allocated_size(), 100u);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Subscript) {
    usmart_ptr<int[]> arr;
    arr.allocate(10);
    for (uint32_t i = 0; i < 10; i++) {
        arr[i] = static_cast<int>(i * 3);
    }
    for (uint32_t i = 0; i < 10; i++) {
        EXPECT_EQ(arr[i], static_cast<int>(i * 3));
    }
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Move) {
    usmart_ptr<int[]> arr1;
    arr1.allocate(5);
    arr1[0] = 42;
    int* raw = arr1.get();

    usmart_ptr<int[]> arr2(static_cast<usmart_ptr<int[]>&&>(arr1));

    EXPECT_EQ(arr1.get(), nullptr);
    EXPECT_EQ(arr2.get(), raw);
    EXPECT_EQ(arr2[0], 42);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_MoveAssignment) {
    usmart_ptr<int[]> arr1;
    arr1.allocate(5);
    arr1[0] = 100;

    usmart_ptr<int[]> arr2;
    arr2 = static_cast<usmart_ptr<int[]>&&>(arr1);

    EXPECT_EQ(arr1.get(), nullptr);
    EXPECT_EQ(arr2[0], 100);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_NullptrAssignment) {
    usmart_ptr<int[]> arr;
    arr.allocate(5);
    arr[0] = 42;

    arr = nullptr;

    EXPECT_EQ(arr.get(), nullptr);
    EXPECT_EQ(arr.allocated_size(), 0u);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Swap) {
    usmart_ptr<int[]> arr1;
    arr1.allocate(3);
    arr1[0] = 1; arr1[1] = 2; arr1[2] = 3;

    usmart_ptr<int[]> arr2;
    arr2.allocate(2);
    arr2[0] = 10; arr2[1] = 20;

    arr1.swap(arr2);

    EXPECT_EQ(arr1.allocated_size(), 2u);
    EXPECT_EQ(arr2.allocated_size(), 3u);
    EXPECT_EQ(arr1[0], 10);
    EXPECT_EQ(arr2[0], 1);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_SwapWithNull) {
    usmart_ptr<int[]> arr1;
    arr1.allocate(3);
    arr1[0] = 42;

    usmart_ptr<int[]> arr2;

    arr1.swap(arr2);

    EXPECT_EQ(arr1.get(), nullptr);
    EXPECT_TRUE(arr2);
    EXPECT_EQ(arr2[0], 42);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Release) {
    usmart_ptr<int[]> arr;
    arr.allocate(3);
    arr[0] = 42;
    int* raw = arr.get();

    int* released = arr.release();

    EXPECT_EQ(released, raw);
    EXPECT_EQ(arr.get(), nullptr);
    EXPECT_EQ(arr.allocated_size(), 0u);

    /* Manual cleanup */
    dfree(dalloc_get_default_heap(), (void**)&released, USING_PTR_ADDRESS);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_Reset) {
    usmart_ptr<int[]> arr;
    arr.allocate(5);
    EXPECT_TRUE(arr);

    arr.reset();

    EXPECT_FALSE(arr);
    EXPECT_EQ(arr.allocated_size(), 0u);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_AllocateZeroFails) {
    usmart_ptr<int[]> arr;
    EXPECT_FALSE(arr.allocate(0));
    EXPECT_EQ(arr.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, ArraySpec_AllocateTwiceFails) {
    usmart_ptr<int[]> arr;
    EXPECT_TRUE(arr.allocate(5));
    EXPECT_FALSE(arr.allocate(10));
}

/*============================================================================
 * make_usmart_array Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, MakeUsmartArray_Creates) {
    auto arr = make_usmart_array<int>(50);
    EXPECT_TRUE(arr);
    EXPECT_EQ(arr.allocated_size(), 50u);
}

TEST_F(UsmartPtrTestFixture, MakeUsmartArray_UsableArray) {
    auto arr = make_usmart_array<float>(10);
    for (uint32_t i = 0; i < 10; i++) {
        arr[i] = static_cast<float>(i) * 1.5f;
    }
    for (uint32_t i = 0; i < 10; i++) {
        EXPECT_FLOAT_EQ(arr[i], static_cast<float>(i) * 1.5f);
    }
}

/*============================================================================
 * observer_ptr Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, ObserverPtr_DefaultConstructor) {
    observer_ptr<int> obs;
    EXPECT_EQ(obs.get(), nullptr);
    EXPECT_FALSE(obs);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_FromRawPointer) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;

    observer_ptr<int> obs(p.get());
    EXPECT_EQ(obs.get(), p.get());
    EXPECT_EQ(*obs, 42);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_FromUsmartPtr) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 99;

    observer_ptr<int> obs(p);
    EXPECT_EQ(obs.get(), p.get());
    EXPECT_EQ(*obs, 99);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_GetObserver) {
    usmart_ptr<SimplePOD> p;
    p.allocate(1);
    p->x = 123;

    auto obs = p.get_observer();
    EXPECT_EQ(obs.get(), p.get());
    EXPECT_EQ(obs->x, 123);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_CopyConstructor) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 77;

    observer_ptr<int> obs1(p);
    observer_ptr<int> obs2(obs1);

    EXPECT_EQ(obs1.get(), obs2.get());
    EXPECT_EQ(*obs2, 77);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_Assignment) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 11;

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 22;

    observer_ptr<int> obs(p1);
    EXPECT_EQ(*obs, 11);

    obs = observer_ptr<int>(p2);
    EXPECT_EQ(*obs, 22);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_Reset) {
    usmart_ptr<int> p;
    p.allocate(1);

    observer_ptr<int> obs(p);
    EXPECT_TRUE(obs);

    obs.reset();
    EXPECT_FALSE(obs);
}

TEST_F(UsmartPtrTestFixture, MakeObserver_Works) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 55;

    auto obs = make_observer(p.get());
    EXPECT_EQ(*obs, 55);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_Swap) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 11;

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 22;

    observer_ptr<int> obs1(p1);
    observer_ptr<int> obs2(p2);

    obs1.swap(obs2);

    EXPECT_EQ(*obs1, 22);
    EXPECT_EQ(*obs2, 11);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_SwapFunction) {
    usmart_ptr<int> p1;
    p1.allocate(1);
    *p1 = 100;

    usmart_ptr<int> p2;
    p2.allocate(1);
    *p2 = 200;

    observer_ptr<int> obs1(p1);
    observer_ptr<int> obs2(p2);

    swap(obs1, obs2);

    EXPECT_EQ(*obs1, 200);
    EXPECT_EQ(*obs2, 100);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_Comparison) {
    usmart_ptr<int> p;
    p.allocate(1);

    observer_ptr<int> obs1(p);
    observer_ptr<int> obs2(p);
    observer_ptr<int> obs3;

    EXPECT_TRUE(obs1 == obs2);
    EXPECT_FALSE(obs1 != obs2);
    EXPECT_FALSE(obs1 == obs3);
    EXPECT_TRUE(obs1 != obs3);
    EXPECT_TRUE(obs3 == nullptr);
    EXPECT_FALSE(obs1 == nullptr);
}

TEST_F(UsmartPtrTestFixture, ObserverPtr_Release) {
    usmart_ptr<int> p;
    p.allocate(1);
    *p = 42;

    observer_ptr<int> obs(p);
    int* raw = obs.release();

    EXPECT_EQ(obs.get(), nullptr);
    EXPECT_EQ(*raw, 42);
}

/*============================================================================
 * Destroy and Constructed Count Tests
 *============================================================================*/

TEST_F(UsmartPtrTestFixture, ConstructedCount_InitiallyZero) {
    usmart_ptr<TrackedObject> p;
    EXPECT_EQ(p.constructed_count(), 0u);
}

TEST_F(UsmartPtrTestFixture, ConstructedCount_AfterCreate) {
    usmart_ptr<TrackedObject> p;
    p.create(42);
    EXPECT_EQ(p.constructed_count(), 1u);
}

TEST_F(UsmartPtrTestFixture, ConstructedCount_AfterAllocate) {
    usmart_ptr<TrackedObject> p;
    p.allocate(10);
    EXPECT_EQ(p.constructed_count(), 0u);  /* allocate doesn't construct */
}

TEST_F(UsmartPtrTestFixture, ConstructedCount_AfterCreateArray) {
    usmart_ptr<TrackedObject> p;
    p.create_array(5);
    EXPECT_EQ(p.constructed_count(), 5u);
}

TEST_F(UsmartPtrTestFixture, Destroy_SingleObject) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    p.create(42);
    EXPECT_EQ(TrackedObject::construct_count, 1);
    EXPECT_EQ(TrackedObject::destruct_count, 0);

    p.destroy();

    EXPECT_EQ(TrackedObject::destruct_count, 1);
    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Destroy_OnNullPointer_Safe) {
    usmart_ptr<TrackedObject> p;
    TrackedObject::resetCounters();

    p.destroy();  /* Should be no-op */

    EXPECT_EQ(TrackedObject::destruct_count, 0);
}

TEST_F(UsmartPtrTestFixture, CreateArray_AllConstructorsCalled) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    EXPECT_TRUE(p.create_array(5));

    EXPECT_EQ(TrackedObject::construct_count, 5);
    EXPECT_EQ(p.allocated_size(), 5u);
    EXPECT_EQ(p.constructed_count(), 5u);
}

TEST_F(UsmartPtrTestFixture, CreateArray_WithValue) {
    TrackedObject::resetCounters();

    TrackedObject template_obj(99);
    TrackedObject::resetCounters();  /* Reset after creating template */

    usmart_ptr<TrackedObject> p;
    EXPECT_TRUE(p.create_array(3, template_obj));

    EXPECT_EQ(TrackedObject::copy_count, 3);  /* 3 copies made */
    EXPECT_EQ(p[0].value, 99);
    EXPECT_EQ(p[1].value, 99);
    EXPECT_EQ(p[2].value, 99);
}

TEST_F(UsmartPtrTestFixture, Reset_CallsDestructorsForConstructedOnly) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    p.create_array(5);
    EXPECT_EQ(TrackedObject::construct_count, 5);
    EXPECT_EQ(TrackedObject::destruct_count, 0);

    p.reset();

    /* All 5 destructors should have been called */
    EXPECT_EQ(TrackedObject::destruct_count, 5);
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_EQ(p.constructed_count(), 0u);
}

TEST_F(UsmartPtrTestFixture, Reset_AllocateOnly_NoDestructorsCalled) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p;
    p.allocate(5);  /* No constructors called */
    EXPECT_EQ(TrackedObject::construct_count, 0);

    p.reset();

    /* No destructors should be called - nothing was constructed */
    EXPECT_EQ(TrackedObject::destruct_count, 0);
}

TEST_F(UsmartPtrTestFixture, Destructor_AutoCallsDestructors) {
    TrackedObject::resetCounters();

    {
        usmart_ptr<TrackedObject> p;
        p.create_array(3);
        EXPECT_EQ(TrackedObject::construct_count, 3);
        EXPECT_EQ(TrackedObject::destruct_count, 0);
    }
    /* Destructor called - should destroy all 3 */

    EXPECT_EQ(TrackedObject::destruct_count, 3);
}

TEST_F(UsmartPtrTestFixture, Reset_POD_NoDestructorNeeded) {
    /* For POD types, allocate + reset is fine */
    usmart_ptr<int> p;
    p.allocate(10);
    for (uint32_t i = 0; i < 10; i++) {
        p[i] = static_cast<int>(i);
    }

    p.reset();

    EXPECT_EQ(p.get(), nullptr);
}

TEST_F(UsmartPtrTestFixture, Move_TransfersConstructedCount) {
    TrackedObject::resetCounters();

    usmart_ptr<TrackedObject> p1;
    p1.create_array(3);
    EXPECT_EQ(p1.constructed_count(), 3u);

    usmart_ptr<TrackedObject> p2(static_cast<usmart_ptr<TrackedObject>&&>(p1));

    EXPECT_EQ(p1.constructed_count(), 0u);
    EXPECT_EQ(p2.constructed_count(), 3u);

    /* No destructors called yet - just moved */
    EXPECT_EQ(TrackedObject::destruct_count, 0);
}

TEST_F(UsmartPtrTestFixture, Swap_ExchangesConstructedCount) {
    usmart_ptr<TrackedObject> p1;
    p1.create_array(2);

    usmart_ptr<TrackedObject> p2;
    p2.create_array(5);

    p1.swap(p2);

    EXPECT_EQ(p1.constructed_count(), 5u);
    EXPECT_EQ(p2.constructed_count(), 2u);
}

TEST_F(UsmartPtrTestFixture, CreateArray_ZeroElements_Fails) {
    usmart_ptr<TrackedObject> p;
    EXPECT_FALSE(p.create_array(0));
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_EQ(p.constructed_count(), 0u);
}

TEST_F(UsmartPtrTestFixture, CreateArray_AlreadyAllocated_Fails) {
    usmart_ptr<TrackedObject> p;
    EXPECT_TRUE(p.create_array(3));

    /* Second create_array should fail */
    EXPECT_FALSE(p.create_array(2));
    /* Original allocation should still be intact */
    EXPECT_EQ(p.allocated_size(), 3u);
    EXPECT_EQ(p.constructed_count(), 3u);
}

/*============================================================================
 * Polymorphism Tests
 *============================================================================*/

class Base {
public:
    virtual ~Base() = default;
    virtual int getValue() const { return 10; }
    int base_value = 100;
};

class Derived : public Base {
public:
    int getValue() const override { return 20; }
    int derived_value = 200;
};

/* Tracked versions for destructor verification */
class TrackedBase {
public:
    static int base_destruct_count;
    virtual ~TrackedBase() { base_destruct_count++; }
    static void resetCounters() { base_destruct_count = 0; TrackedDerivedDestructCount = 0; }
    static int TrackedDerivedDestructCount;
};

int TrackedBase::base_destruct_count = 0;
int TrackedBase::TrackedDerivedDestructCount = 0;

class TrackedDerived : public TrackedBase {
public:
    ~TrackedDerived() override { TrackedDerivedDestructCount++; }
};

TEST_F(UsmartPtrTestFixture, Polymorphism_DerivedToBase_Move) {
    usmart_ptr<Derived> derived;
    ASSERT_TRUE(derived.create());
    derived->base_value = 111;
    derived->derived_value = 222;

    usmart_ptr<Base> base(static_cast<usmart_ptr<Derived>&&>(derived));

    EXPECT_EQ(derived.get(), nullptr);
    EXPECT_TRUE(base);
    EXPECT_EQ(base->getValue(), 20);  /* Virtual call to Derived */
    EXPECT_EQ(base->base_value, 111);
}

TEST_F(UsmartPtrTestFixture, Polymorphism_DerivedToBase_MoveAssign) {
    usmart_ptr<Derived> derived;
    ASSERT_TRUE(derived.create());
    derived->base_value = 333;

    usmart_ptr<Base> base;
    base = static_cast<usmart_ptr<Derived>&&>(derived);

    EXPECT_EQ(derived.get(), nullptr);
    EXPECT_TRUE(base);
    EXPECT_EQ(base->getValue(), 20);
    EXPECT_EQ(base->base_value, 333);
}

TEST_F(UsmartPtrTestFixture, Polymorphism_ObserverFromDerived) {
    usmart_ptr<Derived> derived;
    ASSERT_TRUE(derived.create());

    observer_ptr<Base> obs(derived.get());
    EXPECT_EQ(obs->getValue(), 20);
}

TEST_F(UsmartPtrTestFixture, Polymorphism_DerivedDestructor_CalledThroughBase) {
    TrackedBase::resetCounters();

    {
        usmart_ptr<TrackedDerived> derived;
        ASSERT_TRUE(derived.create());

        /* Move to base pointer */
        usmart_ptr<TrackedBase> base(static_cast<usmart_ptr<TrackedDerived>&&>(derived));

        EXPECT_EQ(TrackedBase::base_destruct_count, 0);
        EXPECT_EQ(TrackedBase::TrackedDerivedDestructCount, 0);
    }
    /* Both destructors should be called due to virtual destructor */
    EXPECT_EQ(TrackedBase::TrackedDerivedDestructCount, 1);
    EXPECT_EQ(TrackedBase::base_destruct_count, 1);
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
