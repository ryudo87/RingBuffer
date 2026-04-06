cmake_minimum_required(VERSION 3.16)
project(LockFreeSPSC VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build with ThreadSanitizer:  cmake -DENABLE_TSAN=ON ..
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)

add_executable(spsc src/main.cpp)

target_compile_options(spsc PRIVATE
    -O3
    -march=native
    -Wall
    -Wextra
)

# pthreads for affinity, scheduler, and mlockall APIs
find_package(Threads REQUIRED)
target_link_libraries(spsc PRIVATE Threads::Threads)

if(ENABLE_TSAN)
    target_compile_options(spsc PRIVATE -fsanitize=thread -g -O1)
    target_link_options(spsc PRIVATE -fsanitize=thread)
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    find_package(GTest QUIET)

    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()

    add_executable(test_ringbuffer tests/test_ringbuffer.cpp)
    target_include_directories(test_ringbuffer PRIVATE src)
    target_compile_options(test_ringbuffer PRIVATE -Wall -Wextra)
    target_link_libraries(test_ringbuffer PRIVATE GTest::gtest GTest::gtest_main Threads::Threads)

    include(GoogleTest)
    gtest_discover_tests(test_ringbuffer)
endif()
