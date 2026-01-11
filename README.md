# usmart_ptr

Smart pointer for embedded systems using [dalloc](https://github.com/SkyEng1neering/dalloc) memory allocator.

**Version:** 2.3.0
**License:** Apache 2.0
**Author:** Alexey Vasilenko

## Features

- Unique ownership semantics (like `std::unique_ptr`)
- Move semantics for ownership transfer
- Automatic memory deallocation in destructor
- **Automatic destructor calls** for objects created via `create()`/`create_array()`
- Derived-to-base polymorphism support
- Array specialization `usmart_ptr<T[]>`
- Non-owning `observer_ptr<T>`
- Placement new support for non-POD types
- Works with single-heap or multi-heap dalloc configurations
- No STL dependencies (custom type traits)
- No exceptions, embedded-friendly

## Dependencies

- [dalloc](https://github.com/SkyEng1neering/dalloc) v1.5.0+

## Quick Start

```cpp
#include "usmart_ptr.h"

// For POD types - use allocate()
usmart_ptr<int> numbers;
numbers.allocate(100);
numbers[0] = 42;
// automatically freed when numbers goes out of scope

// For classes - use create() or create_array()
usmart_ptr<MyClass> obj;
obj.create(arg1, arg2);  // calls MyClass(arg1, arg2)
obj->doSomething();
// destructor automatically called when obj goes out of scope

// Array of objects with constructors
usmart_ptr<MyClass> arr;
arr.create_array(10);  // calls default constructor for all 10 elements
// all 10 destructors called automatically

// Factory function
auto ptr = make_usmart<MyClass>(arg1, arg2);
```

## API Reference

### usmart_ptr<T> - Main Template

#### Constructors

| Constructor | Description |
|-------------|-------------|
| `usmart_ptr()` | Default constructor (uses default_heap in single-heap mode) |
| `usmart_ptr(heap_t* heap)` | Constructor with explicit heap (multi-heap mode only) |
| `usmart_ptr(usmart_ptr&& other)` | Move constructor - transfers ownership |
| `usmart_ptr(usmart_ptr<U>&& other)` | Converting move constructor (Derived to Base) |

#### Accessors

| Method | Description |
|--------|-------------|
| `T* get() const` | Get raw pointer |
| `heap_t* heap() const` | Get heap pointer |
| `uint32_t allocated_size() const` | Get number of allocated elements |
| `uint32_t constructed_count() const` | Get number of constructed elements |
| `T& operator*() const` | Dereference |
| `T* operator->() const` | Arrow operator |
| `T& operator[](uint32_t i) const` | Array subscript |
| `explicit operator bool() const` | True if owns a pointer |
| `observer_ptr<T> get_observer() const` | Get non-owning observer |

#### Modifiers

| Method | Description |
|--------|-------------|
| `T* release()` | Release ownership, return raw pointer |
| `void reset(T* p = nullptr, uint32_t size = 0)` | Free current pointer (calls destructors), optionally take new one |
| `void swap(usmart_ptr& other)` | Swap with another usmart_ptr |
| `void set_heap(heap_t* heap)` | Set heap (multi-heap mode only) |

#### Allocation Methods

| Method | Description |
|--------|-------------|
| `bool allocate(uint32_t n = 1)` | Allocate raw memory for n elements (NO constructors) |
| `bool create()` | Allocate + default constructor |
| `bool create(Args&&... args)` | Allocate + constructor with arguments |
| `bool create_array(uint32_t n)` | Allocate + default constructor for n elements |
| `bool create_array(uint32_t n, const T& value)` | Allocate + copy constructor for n elements |
| `void destroy()` | Call destructors and free memory (alias for reset) |

### usmart_ptr<T[]> - Array Specialization

For raw arrays (POD types). No constructor/destructor tracking.

| Method | Description |
|--------|-------------|
| `bool allocate(uint32_t n)` | Allocate memory for n elements |
| `T& operator[](uint32_t i) const` | Array subscript |
| `T* release()` | Release ownership |
| `void reset()` | Free memory |

### observer_ptr<T> - Non-owning Pointer

Safe non-owning reference. Does NOT manage lifetime.

| Method | Description |
|--------|-------------|
| `observer_ptr()` | Default constructor (nullptr) |
| `observer_ptr(T* p)` | From raw pointer |
| `observer_ptr(const usmart_ptr<T>& owner)` | From usmart_ptr |
| `T* get() const` | Get raw pointer |
| `void reset(T* p = nullptr)` | Change observed pointer |
| `T* release()` | Clear and return pointer |

### Factory Functions

| Function | Description |
|----------|-------------|
| `make_usmart<T>(args...)` | Create usmart_ptr with constructed object |
| `make_usmart_array<T>(n)` | Create usmart_ptr<T[]> array |
| `make_observer(T* p)` | Create observer_ptr from raw pointer |

## Usage Examples

### Allocate vs Create

```cpp
// allocate() - raw memory, NO constructors (for POD or manual construction)
usmart_ptr<int> buf;
buf.allocate(100);
buf[0] = 42;

// create() - allocate + constructor (for single object)
usmart_ptr<MyClass> obj;
obj.create(arg1, arg2);

// create_array() - allocate + constructor for each element
usmart_ptr<MyClass> arr;
arr.create_array(10);  // 10 default-constructed objects

// create_array() with value - copy constructor for each
MyClass template_obj(42);
arr.create_array(5, template_obj);  // 5 copies of template_obj
```

### Automatic Destructor Calls

```cpp
{
    usmart_ptr<MyClass> arr;
    arr.create_array(5);  // 5 constructors called
    // ... use arr ...
}  // 5 destructors automatically called here
```

### Polymorphism (Derived to Base)

```cpp
class Base { virtual ~Base() = default; };
class Derived : public Base { };

usmart_ptr<Derived> derived;
derived.create();

// Converting move - Derived* to Base*
usmart_ptr<Base> base(static_cast<usmart_ptr<Derived>&&>(derived));
// derived is now nullptr
// base owns the pointer, virtual destructor works correctly
```

### Observer Pattern

```cpp
usmart_ptr<Data> owner;
owner.create();

// Non-owning reference
observer_ptr<Data> obs = owner.get_observer();
obs->process();  // Safe as long as owner is alive

// Or from raw pointer
auto obs2 = make_observer(owner.get());
```

### Ownership Transfer

```cpp
usmart_ptr<Data> createData() {
    usmart_ptr<Data> p;
    p.create();
    p->value = 42;
    return p;  // Move semantics
}

void process() {
    usmart_ptr<Data> data = createData();  // Takes ownership

    usmart_ptr<Data> other = static_cast<usmart_ptr<Data>&&>(data);
    // data is now nullptr
    // other owns the pointer
}
```

### Multi-heap Mode

```cpp
heap_t heap1, heap2;
uint8_t heap1_mem[4096], heap2_mem[4096];

heap_init(&heap1, heap1_mem, sizeof(heap1_mem));
heap_init(&heap2, heap2_mem, sizeof(heap2_mem));

usmart_ptr<int> p1(&heap1);
p1.allocate(10);

auto p2 = make_usmart<MyClass>(&heap2, arg1, arg2);
```

### Debug Output

```cpp
#define USMART_PTR_DEBUG printf
#include "usmart_ptr.h"
```

### Debug Assertions

```cpp
#define USMART_PTR_ENABLE_ASSERTIONS
#include "usmart_ptr.h"
// Now null pointer dereference will trigger assert
```

## Memory Overhead

| Type | Size (bytes) |
|------|--------------|
| `usmart_ptr<T>` | 16 (heap* + ptr* + size + constructed) |
| `usmart_ptr<T[]>` | 12 (heap* + ptr* + size) |
| `observer_ptr<T>` | 4/8 (single pointer) |

## Notes

- **POD types**: Use `allocate()` - no constructor/destructor calls
- **Non-POD types**: Use `create()`/`create_array()` - automatic destructor calls
- **Ownership**: Only one usmart_ptr can own a pointer at a time
- **Thread safety**: Not thread-safe, use external synchronization if needed
- **Exception safety**: Not exception-safe (designed for MCU without exceptions)

## Testing

```bash
cd tests
mkdir build && cd build
cmake ..
make -j$(nproc)
./usmart_ptr_tests
```

95 unit tests covering all functionality.

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
