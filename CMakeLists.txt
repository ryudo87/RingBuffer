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
