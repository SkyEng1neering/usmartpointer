cmake_minimum_required(VERSION 3.14)
project(usmart_ptr_tests)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable sanitizers for debug builds
option(USE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" ON)
if(USE_SANITIZERS AND NOT WIN32)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# dalloc library
add_library(dalloc STATIC
    ../../dalloc/src/dalloc.c
)
target_include_directories(dalloc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dalloc/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(dalloc PUBLIC
    DALLOC_CUSTOM_CONF_FILE="usmart_ptr_test_conf.h"
)

# Test executable
add_executable(usmart_ptr_tests
    usmart_ptr_tests.cpp
    test_with_uvector.cpp
)

target_include_directories(usmart_ptr_tests PRIVATE
    ../inc
    ../../dalloc/inc
    ../../uvector/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(usmart_ptr_tests PRIVATE
    DALLOC_CUSTOM_CONF_FILE="usmart_ptr_test_conf.h"
    USE_SINGLE_HEAP_MEMORY
)

target_link_libraries(usmart_ptr_tests
    dalloc
    GTest::gtest_main
)

# Enable test discovery
include(GoogleTest)
gtest_discover_tests(usmart_ptr_tests)
