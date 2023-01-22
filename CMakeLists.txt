cmake_minimum_required(VERSION 3.21)
project(hiptracer LANGUAGES C CXX HIP)
set(CMAKE_CXX_STANDARD 17)

add_library(hipcapture SHARED hip/hip.cpp
                       hip/sqlite3.c
                       hip/msgpuck.c
                       hip/hints.c)
target_include_directories(hipcapture PUBLIC "include")
target_include_directories(hipcapture PUBLIC "/opt/rocm/include")
target_include_directories(hipcapture PUBLIC "/opt/rocm/llvm/include")

target_link_libraries(hipcapture dl zstd pthread)


add_executable(replay replay/replay.hip hip/sqlite3.c hip/msgpuck.c hip/hints.c)
target_include_directories(replay PUBLIC include)
target_include_directories(replay PUBLIC "/opt/rocm/include")
target_include_directories(replay PUBLIC "/opt/rocm/llvm/include")

target_link_libraries(replay dl pthread readline)

add_executable(hiptracer cli.cpp)
target_include_directories(hiptracer PUBLIC include)

enable_testing()

