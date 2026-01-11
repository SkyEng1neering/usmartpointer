/*
 * Test configuration and fixtures for usmart_ptr tests
 */

#ifndef USMART_PTR_TEST_CONFIG_H
#define USMART_PTR_TEST_CONFIG_H

#include <gtest/gtest.h>
#include <cstring>
#include "dalloc.h"
#include "usmart_ptr.h"

/* Heap size for tests */
static constexpr size_t TEST_HEAP_SIZE = 4096;

/* Canary configuration */
static constexpr size_t CANARY_SIZE = 64;
static constexpr uint8_t CANARY_PATTERN = 0xCD;

/*
 * Base test fixture with fresh heap for each test
 *
 * Key: heap is re-registered in SetUp() and unregistered in TearDown()
 * to ensure each test starts with a clean heap state.
 */
class UsmartPtrTestFixture : public ::testing::Test {
protected:
    uint8_t canary_before[CANARY_SIZE];
    alignas(4) uint8_t heap_memory[TEST_HEAP_SIZE];
    uint8_t canary_after[CANARY_SIZE];

    void SetUp() override {
        /* Initialize canaries */
        std::memset(canary_before, CANARY_PATTERN, CANARY_SIZE);
        std::memset(canary_after, CANARY_PATTERN, CANARY_SIZE);

        /* Unregister any existing heap */
        dalloc_unregister_heap(true);

        /* Register fresh heap for this test */
        dalloc_register_heap(heap_memory, TEST_HEAP_SIZE);
        ASSERT_NE(dalloc_get_default_heap(), nullptr);
    }

    void TearDown() override {
        /* Unregister heap to clean up */
        dalloc_unregister_heap(true);

        /* Verify canaries weren't corrupted */
        for (size_t i = 0; i < CANARY_SIZE; i++) {
            EXPECT_EQ(canary_before[i], CANARY_PATTERN) << "Canary before corrupted at " << i;
        }
        for (size_t i = 0; i < CANARY_SIZE; i++) {
            EXPECT_EQ(canary_after[i], CANARY_PATTERN) << "Canary after corrupted at " << i;
        }
    }
};

/*
 * Simple POD struct for testing
 */
struct SimplePOD {
    int x;
    float y;
    char c;
};

/*
 * Class with constructor/destructor tracking
 */
class TrackedObject {
public:
    static int construct_count;
    static int destruct_count;
    static int copy_count;
    static int move_count;

    int value;

    TrackedObject() : value(0) {
        construct_count++;
    }

    explicit TrackedObject(int v) : value(v) {
        construct_count++;
    }

    TrackedObject(const TrackedObject& other) : value(other.value) {
        copy_count++;
    }

    TrackedObject(TrackedObject&& other) noexcept : value(other.value) {
        other.value = -1;
        move_count++;
    }

    ~TrackedObject() {
        destruct_count++;
    }

    static void resetCounters() {
        construct_count = 0;
        destruct_count = 0;
        copy_count = 0;
        move_count = 0;
    }
};

/* Static member definitions will be in test file */

#endif /* USMART_PTR_TEST_CONFIG_H */
