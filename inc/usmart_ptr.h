/*
 * Copyright 2021 Alexey Vasilenko
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * usmart_ptr - Smart pointer for dalloc memory allocator
 *
 * Similar to std::unique_ptr but works with dalloc heap.
 * Provides automatic memory deallocation and ownership transfer via move semantics.
 *
 * Version: 2.3.0
 *
 * New in 2.3.0:
 * - Automatic destructor calls: reset()/destroy() now call destructors for
 *   all elements constructed via create()/create_array()
 * - constructed_count() getter for tracking constructed elements
 * - create_array(n) - allocate and construct n objects with default constructor
 * - create_array(n, value) - allocate and construct n objects as copies
 * - Custom type traits (no STL <type_traits> dependency)
 *
 * New in 2.2.0:
 * - usmart_ptr<T[]> array specialization
 * - observer_ptr<T> non-owning pointer
 * - allocated_size() method
 * - make_usmart_array<T>(n) factory
 * - Derived-to-base polymorphism support
 * - Debug assertions (USMART_PTR_ENABLE_ASSERTIONS)
 */

#ifndef USMART_PTR_H_
#define USMART_PTR_H_

#include "dalloc.h"
#include <stdint.h>
#include <stddef.h>

#define USMART_PTR_VERSION  "2.3.0"

/*--------------------------------------------------------------------
 * Minimal type traits (no STL dependency)
 *--------------------------------------------------------------------*/
namespace usmart_detail {

/* enable_if - SFINAE helper */
template <bool B, typename T = void>
struct enable_if {};

template <typename T>
struct enable_if<true, T> { typedef T type; };

/* is_same - check if two types are identical */
template <typename T, typename U>
struct is_same { static const bool value = false; };

template <typename T>
struct is_same<T, T> { static const bool value = true; };

/* is_convertible - check if U* can be converted to T* */
template <typename From, typename To>
struct is_convertible {
private:
    static void test(To);
    template <typename F>
    static char check(decltype(test(static_cast<F>(nullptr)))*);
    template <typename>
    static long check(...);
public:
    static const bool value = sizeof(check<From>(nullptr)) == sizeof(char);
};

} /* namespace usmart_detail */

/* Configurable debug output - define before including to override */
#ifndef USMART_PTR_DEBUG
#define USMART_PTR_DEBUG(...)  ((void)0)
#endif

/* Debug assertions - define USMART_PTR_ENABLE_ASSERTIONS to enable null checks */
#ifdef USMART_PTR_ENABLE_ASSERTIONS
    #ifndef USMART_PTR_ASSERT
        #include <cassert>
        #define USMART_PTR_ASSERT(cond, msg) assert((cond) && (msg))
    #endif
#else
    #define USMART_PTR_ASSERT(cond, msg) ((void)0)
#endif

/* Forward declarations */
template <typename T> class usmart_ptr;
template <typename T> class usmart_ptr<T[]>;
template <typename T> class observer_ptr;

/*
 * usmart_ptr - Unique ownership smart pointer for dalloc
 *
 * Features:
 * - Automatic memory deallocation in destructor
 * - Automatic destructor calls for constructed elements
 * - Move semantics for ownership transfer
 * - No copy (unique ownership)
 * - Works with single-heap or multi-heap dalloc configurations
 *
 * Allocation Methods:
 *   allocate(n)       - Raw memory, NO constructors (for POD/buffers)
 *   create()          - allocate(1) + constructor
 *   create(args...)   - allocate(1) + constructor with arguments
 *   create_array(n)   - allocate(n) + default constructor for each
 *   create_array(n,v) - allocate(n) + copy constructor for each
 *
 * The destructor/reset() automatically calls destructors for all elements
 * that were constructed via create() or create_array().
 *
 * Usage:
 *   usmart_ptr<int> p;
 *   p.allocate(10);  // Raw memory for 10 ints (no constructors)
 *   p[0] = 42;
 *   // automatically freed when p goes out of scope
 *
 *   usmart_ptr<MyClass> p2;
 *   p2.create(arg1);  // Allocate + construct single object
 *   // destructor automatically called when p2 goes out of scope
 *
 *   usmart_ptr<MyClass> p3;
 *   p3.create_array(5);  // Allocate + construct 5 objects
 *   // all 5 destructors called when p3 goes out of scope
 */
template <typename T>
class usmart_ptr {
    /* Allow other usmart_ptr instantiations to access private members
       for converting constructors */
    template <typename U> friend class usmart_ptr;

private:
    heap_t* heap_;
    T* ptr_;
    uint32_t size_;         /* Number of elements allocated */
    uint32_t constructed_;  /* Number of elements with constructor called */

public:
    /*--------------------------------------------------------------------
     * Constructors / Destructor
     *--------------------------------------------------------------------*/

    /* Default constructor */
    usmart_ptr() noexcept
        : heap_(nullptr)
        , ptr_(nullptr)
        , size_(0)
        , constructed_(0)
    {
#ifdef USE_SINGLE_HEAP_MEMORY
        heap_ = dalloc_get_default_heap();
#endif
    }

#ifndef USE_SINGLE_HEAP_MEMORY
    /* Constructor with explicit heap (multi-heap mode only) */
    explicit usmart_ptr(heap_t* heap) noexcept
        : heap_(heap)
        , ptr_(nullptr)
        , size_(0)
        , constructed_(0)
    {
    }
#endif

    /* Move constructor - transfers ownership */
    usmart_ptr(usmart_ptr&& other) noexcept
        : heap_(other.heap_)
        , ptr_(nullptr)
        , size_(other.size_)
        , constructed_(other.constructed_)
    {
        /* dalloc tracks pointer ADDRESSES. We need to update the tracking
           to point to our ptr_ instead of other.ptr_ */
        if (other.ptr_ != nullptr) {
            replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
        }
        other.ptr_ = nullptr;  /* Ensure source is null after move */
        other.size_ = 0;
        other.constructed_ = 0;
        /* Keep other.heap_ - it's still valid for future allocations */
    }

    /*
     * Converting move constructor - allows derived-to-base conversion.
     * Enables: usmart_ptr<Base> p = usmart_ptr<Derived>();
     *
     * Only enabled when U* is implicitly convertible to T* (inheritance)
     * and U is not the same type as T (to avoid ambiguity with move constructor).
     */
    template <typename U,
              typename = typename usmart_detail::enable_if<
                  usmart_detail::is_convertible<U*, T*>::value &&
                  !usmart_detail::is_same<U, T>::value
              >::type>
    usmart_ptr(usmart_ptr<U>&& other) noexcept
        : heap_(other.heap_)
        , ptr_(nullptr)
        , size_(other.size_)
        , constructed_(other.constructed_)
    {
        if (other.ptr_ != nullptr) {
            /* Update dalloc tracking to point to our ptr_ */
            replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
        }
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.constructed_ = 0;
    }

    /* Destructor - frees owned memory */
    ~usmart_ptr() {
        reset();
    }

    /* Deleted copy constructor - no copying allowed */
    usmart_ptr(const usmart_ptr&) = delete;

    /* Deleted copy assignment - no copying allowed */
    usmart_ptr& operator=(const usmart_ptr&) = delete;

    /*--------------------------------------------------------------------
     * Assignment Operators
     *--------------------------------------------------------------------*/

    /* Move assignment - transfers ownership */
    usmart_ptr& operator=(usmart_ptr&& other) noexcept {
        if (this != &other) {
            reset();  /* Free current pointer (calls destructors if needed) */
            heap_ = other.heap_;
            size_ = other.size_;
            constructed_ = other.constructed_;
            /* dalloc tracks pointer ADDRESSES. We need to update the tracking
               to point to our ptr_ instead of other.ptr_ */
            if (other.ptr_ != nullptr) {
                replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
            }
            other.ptr_ = nullptr;  /* Ensure source is null after move */
            other.size_ = 0;
            other.constructed_ = 0;
        }
        return *this;
    }

    /*
     * Converting move assignment - allows derived-to-base conversion.
     * Enables: base_ptr = std::move(derived_ptr);
     *
     * Only enabled when U* is implicitly convertible to T*.
     */
    template <typename U,
              typename = typename usmart_detail::enable_if<
                  usmart_detail::is_convertible<U*, T*>::value &&
                  !usmart_detail::is_same<U, T>::value
              >::type>
    usmart_ptr& operator=(usmart_ptr<U>&& other) noexcept {
        reset();  /* Free current pointer (calls destructors if needed) */
        heap_ = other.heap_;
        size_ = other.size_;
        constructed_ = other.constructed_;
        if (other.ptr_ != nullptr) {
            replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
        }
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.constructed_ = 0;
        return *this;
    }

    /* Nullptr assignment - releases and clears */
    usmart_ptr& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    /*--------------------------------------------------------------------
     * Accessors
     *--------------------------------------------------------------------*/

    /* Get raw pointer */
    T* get() const noexcept {
        return ptr_;
    }

    /* Get heap pointer */
    heap_t* heap() const noexcept {
        return heap_;
    }

    /* Get number of allocated elements */
    uint32_t allocated_size() const noexcept {
        return size_;
    }

    /* Get number of constructed elements (for destroy) */
    uint32_t constructed_count() const noexcept {
        return constructed_;
    }

    /* Dereference operator */
    T& operator*() const {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Dereferencing null usmart_ptr");
        return *ptr_;
    }

    /* Arrow operator */
    T* operator->() const noexcept {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Arrow operator on null usmart_ptr");
        return ptr_;
    }

    /* Array subscript operator */
    T& operator[](uint32_t i) const {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Subscript on null usmart_ptr");
        USMART_PTR_ASSERT(i < size_, "Subscript out of bounds");
        return ptr_[i];
    }

    /* Boolean conversion - true if owns a pointer */
    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    /*
     * Create a non-owning observer pointer.
     * The observer does not manage lifetime - ensure usmart_ptr outlives it.
     */
    observer_ptr<T> get_observer() const noexcept;

    /*--------------------------------------------------------------------
     * Modifiers
     *--------------------------------------------------------------------*/

    /*
     * Release ownership and return raw pointer.
     * Caller becomes responsible for calling destructors and freeing memory.
     *
     * @return Raw pointer (caller must call destructors and dfree)
     */
    T* release() noexcept {
        T* p = ptr_;
        ptr_ = nullptr;
        size_ = 0;
        constructed_ = 0;
        return p;
    }

    /*
     * Reset pointer - calls destructors for constructed elements,
     * frees memory, and optionally takes new pointer.
     *
     * @param p - new pointer to own (default nullptr)
     * @param new_size - size for new pointer (default 0)
     */
    void reset(T* p = nullptr, uint32_t new_size = 0) noexcept {
        if (ptr_ != nullptr) {
            /* Call destructors for all constructed elements */
            for (uint32_t i = 0; i < constructed_; i++) {
                ptr_[i].~T();
            }
            /* Free memory */
            if (heap_ != nullptr) {
                dfree(heap_, (void**)&ptr_, USING_PTR_ADDRESS);
            }
        }
        ptr_ = p;
        size_ = new_size;
        constructed_ = 0;
    }

    /*
     * Swap with another usmart_ptr.
     *
     * Important: dalloc tracks pointer ADDRESSES, not values.
     * We must use replace_pointers to update dalloc's internal tracking
     * when swapping ownership between usmart_ptr instances.
     *
     * Critical: pointer operations must use ORIGINAL heaps before heap swap.
     */
    void swap(usmart_ptr& other) noexcept {
        if (this == &other) return;

        /* Save original heaps - pointers were allocated from these */
        heap_t* my_heap = heap_;
        heap_t* other_heap = other.heap_;

        /* For ptr_, we need to update dalloc's pointer tracking FIRST
           (before heap swap, using original heaps) */
        if (ptr_ != nullptr && other.ptr_ != nullptr) {
            /* Both have allocations - need to swap in dalloc's tracking */
            /* Use temporary to do three-way swap via replace_pointers */
            T* tmp = nullptr;
            replace_pointers(my_heap, (void**)&ptr_, (void**)&tmp);           /* ptr_ -> tmp (from my_heap) */
            replace_pointers(other_heap, (void**)&other.ptr_, (void**)&ptr_); /* other.ptr_ -> ptr_ (from other_heap) */
            replace_pointers(my_heap, (void**)&tmp, (void**)&other.ptr_);     /* tmp -> other.ptr_ (from my_heap) */
        } else if (ptr_ != nullptr) {
            /* Only this has allocation */
            replace_pointers(my_heap, (void**)&ptr_, (void**)&other.ptr_);
        } else if (other.ptr_ != nullptr) {
            /* Only other has allocation */
            replace_pointers(other_heap, (void**)&other.ptr_, (void**)&ptr_);
        }
        /* If both are nullptr, nothing to do for pointers */

        /* Now swap heap pointers */
        heap_ = other_heap;
        other.heap_ = my_heap;

        /* Swap sizes */
        uint32_t tmp_size = size_;
        size_ = other.size_;
        other.size_ = tmp_size;

        /* Swap constructed counts */
        uint32_t tmp_constructed = constructed_;
        constructed_ = other.constructed_;
        other.constructed_ = tmp_constructed;
    }

#ifndef USE_SINGLE_HEAP_MEMORY
    /*
     * Set heap for allocations (multi-heap mode only).
     * Must be called before allocate() if default constructor was used.
     *
     * @param heap - pointer to initialized heap
     */
    void set_heap(heap_t* heap) noexcept {
        heap_ = heap;
    }
#endif

    /*--------------------------------------------------------------------
     * Allocation Methods
     *--------------------------------------------------------------------*/

    /*
     * Allocate memory for n elements.
     * Does NOT call constructors - use for POD types or arrays.
     *
     * @param n - number of elements to allocate
     * @return true if allocation succeeded
     */
    bool allocate(uint32_t n = 1) {
        if (heap_ == nullptr) {
            USMART_PTR_DEBUG("usmart_ptr: heap not set\n");
            return false;
        }
        if (ptr_ != nullptr) {
            USMART_PTR_DEBUG("usmart_ptr: already allocated\n");
            return false;
        }
        if (n == 0) {
            return false;
        }

        /* Check for overflow: n * sizeof(T) must not overflow size_t */
        if (n > SIZE_MAX / sizeof(T)) {
            USMART_PTR_DEBUG("usmart_ptr: allocation size overflow\n");
            return false;
        }

        dalloc(heap_, n * sizeof(T), (void**)&ptr_);
        if (ptr_ == nullptr) {
            USMART_PTR_DEBUG("usmart_ptr: allocation failed\n");
            return false;
        }
        size_ = n;
        return true;
    }

    /*
     * Allocate and construct a single object using placement new.
     * Calls default constructor.
     *
     * @return true if creation succeeded
     */
    bool create() {
        if (!allocate(1)) {
            return false;
        }
        new (ptr_) T();
        constructed_ = 1;
        return true;
    }

    /*
     * Allocate and construct a single object with arguments.
     * Calls constructor with provided arguments.
     *
     * @param args - constructor arguments
     * @return true if creation succeeded
     */
    template <typename... Args>
    bool create(Args&&... args) {
        if (!allocate(1)) {
            return false;
        }
        new (ptr_) T(static_cast<Args&&>(args)...);
        constructed_ = 1;
        return true;
    }

    /*
     * Allocate and construct an array of n objects.
     * Calls default constructor for each element.
     *
     * @param n - number of elements to create
     * @return true if creation succeeded
     */
    bool create_array(uint32_t n) {
        if (!allocate(n)) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            new (&ptr_[i]) T();
        }
        constructed_ = n;
        return true;
    }

    /*
     * Allocate and construct an array of n objects with a value.
     * Calls copy constructor for each element.
     *
     * @param n - number of elements to create
     * @param value - value to copy into each element
     * @return true if creation succeeded
     */
    bool create_array(uint32_t n, const T& value) {
        if (!allocate(n)) {
            return false;
        }
        for (uint32_t i = 0; i < n; i++) {
            new (&ptr_[i]) T(value);
        }
        constructed_ = n;
        return true;
    }

    /*
     * Destroy all constructed objects and free memory.
     * Equivalent to reset() - calls destructors for constructed_ elements.
     *
     * Note: reset() now automatically calls destructors, so destroy()
     * is just an alias for clarity when working with non-POD types.
     */
    void destroy() {
        reset();
    }
};

/*--------------------------------------------------------------------
 * Array Specialization usmart_ptr<T[]>
 *--------------------------------------------------------------------*/

/*
 * usmart_ptr<T[]> - Array specialization
 *
 * Similar to std::unique_ptr<T[]> but for dalloc heap.
 * Disables single-element operators (*, ->) and enables array access.
 *
 * Usage:
 *   usmart_ptr<int[]> arr;
 *   arr.allocate(100);
 *   arr[0] = 42;
 */
template <typename T>
class usmart_ptr<T[]> {
private:
    heap_t* heap_;
    T* ptr_;
    uint32_t size_;

public:
    /*--------------------------------------------------------------------
     * Constructors / Destructor
     *--------------------------------------------------------------------*/

    usmart_ptr() noexcept
        : heap_(nullptr)
        , ptr_(nullptr)
        , size_(0)
    {
#ifdef USE_SINGLE_HEAP_MEMORY
        heap_ = dalloc_get_default_heap();
#endif
    }

#ifndef USE_SINGLE_HEAP_MEMORY
    explicit usmart_ptr(heap_t* heap) noexcept
        : heap_(heap)
        , ptr_(nullptr)
        , size_(0)
    {
    }
#endif

    usmart_ptr(usmart_ptr&& other) noexcept
        : heap_(other.heap_)
        , ptr_(nullptr)
        , size_(other.size_)
    {
        if (other.ptr_ != nullptr) {
            replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
        }
        other.ptr_ = nullptr;  /* Ensure source is null after move */
        other.size_ = 0;
    }

    ~usmart_ptr() {
        reset();
    }

    usmart_ptr(const usmart_ptr&) = delete;
    usmart_ptr& operator=(const usmart_ptr&) = delete;

    /*--------------------------------------------------------------------
     * Assignment Operators
     *--------------------------------------------------------------------*/

    usmart_ptr& operator=(usmart_ptr&& other) noexcept {
        if (this != &other) {
            reset();
            heap_ = other.heap_;
            size_ = other.size_;
            if (other.ptr_ != nullptr) {
                replace_pointers(heap_, (void**)&other.ptr_, (void**)&ptr_);
            }
            other.ptr_ = nullptr;  /* Ensure source is null after move */
            other.size_ = 0;
        }
        return *this;
    }

    usmart_ptr& operator=(decltype(nullptr)) noexcept {
        reset();
        return *this;
    }

    /*--------------------------------------------------------------------
     * Accessors
     *--------------------------------------------------------------------*/

    T* get() const noexcept {
        return ptr_;
    }

    heap_t* heap() const noexcept {
        return heap_;
    }

    uint32_t allocated_size() const noexcept {
        return size_;
    }

    /* Array subscript - primary access method for arrays */
    T& operator[](uint32_t i) const {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Subscript on null usmart_ptr<T[]>");
        USMART_PTR_ASSERT(i < size_, "Array index out of bounds");
        return ptr_[i];
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    /* No operator* or operator-> for array type - use operator[] */

    /*--------------------------------------------------------------------
     * Modifiers
     *--------------------------------------------------------------------*/

    T* release() noexcept {
        T* p = ptr_;
        ptr_ = nullptr;
        size_ = 0;
        return p;
    }

    void reset(T* p = nullptr, uint32_t new_size = 0) noexcept {
        if (ptr_ != nullptr && heap_ != nullptr) {
            dfree(heap_, (void**)&ptr_, USING_PTR_ADDRESS);
        }
        ptr_ = p;
        size_ = new_size;
    }

    void swap(usmart_ptr& other) noexcept {
        if (this == &other) return;

        /* Save original heaps - pointers were allocated from these */
        heap_t* my_heap = heap_;
        heap_t* other_heap = other.heap_;

        /* Update dalloc's pointer tracking FIRST (using original heaps) */
        if (ptr_ != nullptr && other.ptr_ != nullptr) {
            T* tmp = nullptr;
            replace_pointers(my_heap, (void**)&ptr_, (void**)&tmp);
            replace_pointers(other_heap, (void**)&other.ptr_, (void**)&ptr_);
            replace_pointers(my_heap, (void**)&tmp, (void**)&other.ptr_);
        } else if (ptr_ != nullptr) {
            replace_pointers(my_heap, (void**)&ptr_, (void**)&other.ptr_);
        } else if (other.ptr_ != nullptr) {
            replace_pointers(other_heap, (void**)&other.ptr_, (void**)&ptr_);
        }

        /* Now swap heaps and sizes */
        heap_ = other_heap;
        other.heap_ = my_heap;

        uint32_t tmp_size = size_;
        size_ = other.size_;
        other.size_ = tmp_size;
    }

#ifndef USE_SINGLE_HEAP_MEMORY
    void set_heap(heap_t* heap) noexcept {
        heap_ = heap;
    }
#endif

    /*--------------------------------------------------------------------
     * Allocation Methods
     *--------------------------------------------------------------------*/

    bool allocate(uint32_t n) {
        if (heap_ == nullptr) {
            USMART_PTR_DEBUG("usmart_ptr<T[]>: heap not set\n");
            return false;
        }
        if (ptr_ != nullptr) {
            USMART_PTR_DEBUG("usmart_ptr<T[]>: already allocated\n");
            return false;
        }
        if (n == 0) {
            return false;
        }

        /* Check for overflow: n * sizeof(T) must not overflow size_t */
        if (n > SIZE_MAX / sizeof(T)) {
            USMART_PTR_DEBUG("usmart_ptr<T[]>: allocation size overflow\n");
            return false;
        }

        dalloc(heap_, n * sizeof(T), (void**)&ptr_);
        if (ptr_ == nullptr) {
            USMART_PTR_DEBUG("usmart_ptr<T[]>: allocation failed\n");
            return false;
        }
        size_ = n;
        return true;
    }
};

/*--------------------------------------------------------------------
 * observer_ptr - Non-owning pointer wrapper
 *--------------------------------------------------------------------*/

/*
 * observer_ptr<T> - Non-owning smart pointer
 *
 * Similar to std::observer_ptr (Library Fundamentals TS v2).
 * Does NOT manage lifetime - just provides a safer way to pass
 * non-owning references.
 *
 * Usage:
 *   usmart_ptr<MyClass> owner;
 *   owner.create();
 *   observer_ptr<MyClass> obs = owner.get_observer();
 *   obs->method();  // Safe as long as owner is alive
 */
template <typename T>
class observer_ptr {
private:
    T* ptr_;

public:
    /* Constructors */
    constexpr observer_ptr() noexcept : ptr_(nullptr) {}
    constexpr observer_ptr(decltype(nullptr)) noexcept : ptr_(nullptr) {}
    explicit observer_ptr(T* p) noexcept : ptr_(p) {}

    /* Construct from usmart_ptr (non-owning view) */
    observer_ptr(const usmart_ptr<T>& owner) noexcept : ptr_(owner.get()) {}

    /* Copy/Move - all allowed (non-owning) */
    observer_ptr(const observer_ptr&) = default;
    observer_ptr& operator=(const observer_ptr&) = default;
    observer_ptr(observer_ptr&&) = default;
    observer_ptr& operator=(observer_ptr&&) = default;

    /* Accessors */
    T* get() const noexcept {
        return ptr_;
    }

    T& operator*() const {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Dereferencing null observer_ptr");
        return *ptr_;
    }

    T* operator->() const noexcept {
        USMART_PTR_ASSERT(ptr_ != nullptr, "Arrow operator on null observer_ptr");
        return ptr_;
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    /* Modifiers */
    void reset(T* p = nullptr) noexcept {
        ptr_ = p;
    }

    T* release() noexcept {
        T* p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    void swap(observer_ptr& other) noexcept {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }
};

/* observer_ptr comparison operators */
template <typename T>
bool operator==(const observer_ptr<T>& a, const observer_ptr<T>& b) noexcept {
    return a.get() == b.get();
}

template <typename T>
bool operator!=(const observer_ptr<T>& a, const observer_ptr<T>& b) noexcept {
    return a.get() != b.get();
}

template <typename T>
bool operator==(const observer_ptr<T>& p, decltype(nullptr)) noexcept {
    return p.get() == nullptr;
}

template <typename T>
bool operator==(decltype(nullptr), const observer_ptr<T>& p) noexcept {
    return p.get() == nullptr;
}

template <typename T>
bool operator!=(const observer_ptr<T>& p, decltype(nullptr)) noexcept {
    return p.get() != nullptr;
}

template <typename T>
bool operator!=(decltype(nullptr), const observer_ptr<T>& p) noexcept {
    return p.get() != nullptr;
}

/* Create observer from raw pointer */
template <typename T>
observer_ptr<T> make_observer(T* p) noexcept {
    return observer_ptr<T>(p);
}

/*--------------------------------------------------------------------
 * usmart_ptr::get_observer() implementation
 *--------------------------------------------------------------------*/

template <typename T>
observer_ptr<T> usmart_ptr<T>::get_observer() const noexcept {
    return observer_ptr<T>(ptr_);
}

/*--------------------------------------------------------------------
 * Helper Functions
 *--------------------------------------------------------------------*/

/* Swap two usmart_ptr instances */
template <typename T>
void swap(usmart_ptr<T>& a, usmart_ptr<T>& b) noexcept {
    a.swap(b);
}

/* Swap two observer_ptr instances */
template <typename T>
void swap(observer_ptr<T>& a, observer_ptr<T>& b) noexcept {
    a.swap(b);
}

/* Comparison operators */
template <typename T>
bool operator==(const usmart_ptr<T>& p, decltype(nullptr)) noexcept {
    return p.get() == nullptr;
}

template <typename T>
bool operator==(decltype(nullptr), const usmart_ptr<T>& p) noexcept {
    return p.get() == nullptr;
}

template <typename T>
bool operator!=(const usmart_ptr<T>& p, decltype(nullptr)) noexcept {
    return p.get() != nullptr;
}

template <typename T>
bool operator!=(decltype(nullptr), const usmart_ptr<T>& p) noexcept {
    return p.get() != nullptr;
}

template <typename T, typename U>
bool operator==(const usmart_ptr<T>& a, const usmart_ptr<U>& b) noexcept {
    return a.get() == b.get();
}

template <typename T, typename U>
bool operator!=(const usmart_ptr<T>& a, const usmart_ptr<U>& b) noexcept {
    return a.get() != b.get();
}

/*--------------------------------------------------------------------
 * Factory Function (single-heap mode)
 *--------------------------------------------------------------------*/

#ifdef USE_SINGLE_HEAP_MEMORY
/*
 * Create a usmart_ptr with a newly constructed object.
 * Similar to std::make_unique.
 *
 * Usage:
 *   auto p = make_usmart<MyClass>(arg1, arg2);
 *
 * @param args - constructor arguments
 * @return usmart_ptr owning the new object, or empty on failure
 */
template <typename T, typename... Args>
usmart_ptr<T> make_usmart(Args&&... args) {
    usmart_ptr<T> p;
    p.create(static_cast<Args&&>(args)...);
    return p;
}

/*
 * Create a usmart_ptr<T[]> array.
 * Similar to std::make_unique<T[]>(n).
 *
 * Usage:
 *   auto arr = make_usmart_array<int>(100);
 *   arr[0] = 42;
 *
 * @param n - number of elements to allocate
 * @return usmart_ptr<T[]> owning the array, or empty on failure
 */
template <typename T>
usmart_ptr<T[]> make_usmart_array(uint32_t n) {
    usmart_ptr<T[]> p;
    p.allocate(n);
    return p;
}
#else
/*
 * Create a usmart_ptr with a newly constructed object (multi-heap mode).
 *
 * @param heap - heap to allocate from
 * @param args - constructor arguments
 * @return usmart_ptr owning the new object, or empty on failure
 */
template <typename T, typename... Args>
usmart_ptr<T> make_usmart(heap_t* heap, Args&&... args) {
    usmart_ptr<T> p(heap);
    p.create(static_cast<Args&&>(args)...);
    return p;
}

/*
 * Create a usmart_ptr<T[]> array (multi-heap mode).
 *
 * @param heap - heap to allocate from
 * @param n - number of elements to allocate
 * @return usmart_ptr<T[]> owning the array, or empty on failure
 */
template <typename T>
usmart_ptr<T[]> make_usmart_array(heap_t* heap, uint32_t n) {
    usmart_ptr<T[]> p(heap);
    p.allocate(n);
    return p;
}
#endif

#endif /* USMART_PTR_H_ */
