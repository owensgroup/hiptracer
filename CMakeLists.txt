cmake_minimum_required(VERSION 3.21)
project(hiptracer LANGUAGES C CXX HIP)
set(CMAKE_CXX_STANDARD 17)
add_library(hipcapture SHARED hip/hip.cpp hip/hip-fatbin.cpp
                       hip/sqlite3.c
                       hip/xxhash.c
                       hip/msgpuck.c
                       hip/hints.c)
target_include_directories(hipcapture PUBLIC "include")
target_include_directories(hipcapture PUBLIC "/opt/rocm/include")
target_include_directories(hipcapture PUBLIC "/opt/rocm/llvm/include")

target_link_libraries(hipcapture dl zstd pthread)

# add_library(cudacapture SHARED cuda/cuda.cpp cuda/cuda-fatbin.cpp
#                                hip/sqlite3.c)
# target_include_directories(cudacapture PUBLIC "include")
# target_include_directories(cudacapture PUBLIC "/usr/local/cuda/targets/x86_64-linux/include")
# target_include_directories(cudacapture PUBLIC ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
# 
# target_link_libraries(cudacapture dl pthread)

set_source_files_properties (replay/replay.cpp PROPERTIES LANGUAGE HIP) 
add_executable(replay replay/replay.cpp hip/sqlite3.c hip/xxhash.c hip/msgpuck.c hip/hints.c)
target_include_directories(replay PUBLIC include)
target_include_directories(replay PUBLIC "/opt/rocm/include")
target_include_directories(replay PUBLIC "/opt/rocm/llvm/include")

target_link_libraries(replay dl pthread readline)

add_executable(hiptracer cli.cpp)
target_include_directories(hiptracer PUBLIC include)

enable_testing()

## Little Tests
add_test(NAME vectoradd COMMAND ${CMAKE_BUILD_DIR}/hiptracer -o vectoradd.db ${CMAKE_SOURCE_DIR}/examples/vectorAdd/vectoradd_hip.exe)
add_test(NAME cuda-stream COMMAND ${CMAKE_BUILD_DIR}/hiptracer -o cuda-stream.db ${CMAKE_SOURCE_DIR}/examples/cuda-stream/stream)
add_test(NAME strided-access COMMAND ${CMAKE_BUILD_DIR}/hiptracer -o strided-access.db ${CMAKE_SOURCE_DIR}/examples/strided-access/strided-access)

add_test(NAME kripke COMMAND ${CMAKE_BUILD_DIR}/hiptracer -o kripke.db -- ${CMAKE_SOURCE_DIR}/examples/Kripke/build/bin/kripke.exe --niter 1)

# strided-access
# gpu-stream

## Big Tests
#add_test(NAME add4 COMMAND ${CMAKE_BUILD_DIR}/hiptracer --output add4.db ${CMAKE_SOURCE_DIR}/examples/add4/runhip.sh)
add_test(NAME reduction COMMAND ${CMAKE_BUILD_DIR}/hiptracer -o reduction.db ${CMAKE_SOURCE_DIR}/examples/reduction/reduction)
#add_test(NAME rtm8 COMMAND ${CMAKE_BUILD_DIR}/hiptracer ${CMAKE_SOURCE_DIR}/examples/rtm8/rtm8_hip --output rtm8.db)


