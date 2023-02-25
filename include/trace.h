#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <vector>
#include <variant>
#include <string>
#include <cstddef>

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

enum HIP_EVENT {
    EVENT_UNDEFINED = 0,
    EVENT_DEVICE,
    EVENT_MALLOC,
    EVENT_MEMCPY,
    EVENT_FREE,
    EVENT_LAUNCH,
    EVENT_SYNC,
    EVENT_HOST,
    EVENT_GPUEVENT,
    EVENT_STREAM,
    EVENT_CODE
};

struct gputrace_event_malloc {
    uint64_t p;
    size_t size;
};

struct gputrace_event_memcpy {
    uint64_t dst;
    uint64_t src;
    uint64_t size;
    hipMemcpyKind kind;
    std::vector<std::byte> hostdata;
};

struct gputrace_event_free {
    uint64_t p;
};

struct gputrace_event_launch {
    //std::string kernel_name;
    uint32_t hashed_kname;
    dim3 num_blocks;
    dim3 dim_blocks;
    int shared_mem_bytes;
    std::vector<std::byte> argdata;
};

struct gputrace_event_code {
    std::string filename;
    std::string code;
};

struct gputrace_event {
    uint64_t id;
    const char* name;
    hipError_t rc;
    hipStream_t stream;

    HIP_EVENT type;
    std::variant<gputrace_event_malloc,
                 gputrace_event_memcpy,
                 gputrace_event_free,
                 gputrace_event_launch,
                 gputrace_event_code> data; // FIXME perf issues w/ std::variant
};

#endif // __HIP_TRACE_H__
