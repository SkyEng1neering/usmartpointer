# About the project
This is the [dalloc](https://github.com/SkyEng1neering/dalloc) wrapper, it designed to make "dalloc" more comfortable to use. For example if you need to return pointer to memory allocated inside some function (see part "__Limitations__ in [dalloc](https://github.com/SkyEng1neering/dalloc) readme file").

# Dependencies
__usmartpointer__ based on [dalloc](https://github.com/SkyEng1neering/dalloc) allocator, so you should include it to your project.

# Usage
## Using usmartpointer with single heap area
[dalloc](https://github.com/SkyEng1neering/dalloc) allocator is configurable, it can work with only one memory area that you define, or you can select which memory area should be used for each your allocation.

If you want to use in your project only one heap area, you should define "USE_SINGLE_HEAP_MEMORY" in file __dalloc_conf.h__. This is the simpliest way to use __usmartpointer__ because it allows you to abstract away from working with memory.

```c++
/* File dalloc_conf.h */
#define USE_SINGLE_HEAP_MEMORY
#define SINGLE_HEAP_SIZE				4096UL //define heap size that you want to have
```

Then you should define uint8_t array in your code, that will be used for storing data. This array should be named "single_heap" and it should be have size SINGLE_HEAP_SIZE (defined in file __dalloc_conf.h__).

```c++
#include "usmartpointer.h"

uint8_t single_heap[SINGLE_HEAP_SIZE] = {0};
```

Why you should implement this array in your code by yourself? Because you may want to store this array for example in specific memory region, or you may want to apply to this array some attributes, like this:

```c++
__attribute__((section(".ITCM_RAM"))) uint8_t single_heap[SINGLE_HEAP_SIZE] = {0};
```
or like this:

```c++
__ALIGN_BEGIN uint8_t single_heap[SINGLE_HEAP_SIZE] __ALIGN_END;
```

So for example that's how looks like example of using __usmartpointer__ with single memory region on STM32 MCU:

```c++
#include "usmartpointer.h"

__ALIGN_BEGIN uint8_t single_heap[SINGLE_HEAP_SIZE] __ALIGN_END;

SmartPointer<char> func_returns_smartpointer(){
    char string[] = "charstring";
    
    SmartPointer<char> char_ptr;
    char_ptr.allocate(strlen(string) + 1);// +1 byte for null terminator
    memcpy(char_ptr.get(), string, strlen(string) + 1);

    return char_ptr;
}

void main(){
  SmartPointer<char> char_ptr = func_returns_smartpointer();

  printf("String value is %s\n", char_ptr.get());
 
  char_ptr.free();
 
  while(1){}
}  
```

## Using usmartpointer with different heap areas

If you want to use several different heap areas, you can define it explicitly:

```c++
/* File dalloc_conf.h */
//#define USE_SINGLE_HEAP_MEMORY //comment this define
```

```c++
#include "usmartpointer.h"

#define HEAP_SIZE			1024

/* Declare an arrays that will be used for dynamic memory allocations */
__ALIGN_BEGIN uint8_t heap_array1[HEAP_SIZE] __ALIGN_END;
__ALIGN_BEGIN uint8_t heap_array2[HEAP_SIZE] __ALIGN_END;

/* Declare an dalloc heap structures, it will contains all allocations info */
heap_t heap1;
heap_t heap2;

SmartPointer<char> func_returns_smartpointer1(heap_t* heap_ptr){
    char string[] = "charstring";
    
    SmartPointer<char> char_ptr;
    char_ptr.allocate(strlen(string) + 1);// +1 byte for null terminator
    memcpy(char_ptr.get(), string, strlen(string) + 1);

    return char_ptr;
}

void main(){
  /* Init heap memory */
  heap_init(&heap1, (void*)heap_array1, HEAP_SIZE);
  heap_init(&heap2, (void*)heap_array2, HEAP_SIZE);

  SmartPointer<char> char_ptr1;
  char_ptr1.assignAllocMemPointer(&heap1);
  SmartPointer<char> char_ptr1 = func_returns_smartpointer(&heap1);
  
  SmartPointer<char> char_ptr2;
  char_ptr2.assignAllocMemPointer(&heap2);
  SmartPointer<char> char_ptr2 = func_returns_smartpointer(&heap2);

  printf("String1 value is %s\n", char_ptr1.get());
  printf("String2 value is %s\n", char_ptr2.get());
 
  char_ptr1.free();
  char_ptr2.free();
 
  while(1){}
}  
  
```
## P.S.
In any time you can check what exactly is going on in your heap memory using functions:
```c++
/* If you use different heap areas in your project */
void print_dalloc_info(heap_t *heap_struct_ptr);
void dump_dalloc_ptr_info(heap_t* heap_struct_ptr);
void dump_heap(heap_t* heap_struct_ptr);
```
```c++
/* If you use single heap area in your project */
void print_def_dalloc_info();
void dump_def_dalloc_ptr_info();
void dump_def_heap();
```
