#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <vector>
#include <string>
#include <cstddef>

typedef enum HIP_EVENT {
    EVENT_UNDEFINED = 0,
    EVENT_DEVICE,
    EVENT_MALLOC,
    EVENT_MEMCPY,
    EVENT_FREE,
    EVENT_LAUNCH
} HIP_EVENT;

typedef struct gputrace_event {
    uint64_t id;
    const char* name;
    hipResult_t rc;
    hipStream_t stream;

    HIP_EVENT type;
    std::variant<gputrace_event_malloc,
                 gputrace_event_memcpy,
                 gputrace_event_free,
                 gputrace_event_launch> data; // FIXME Just a union - Will always be the largest
} gputrace_event;

typedef struct gputrace_event_malloc {
    void* p;
    size_t size;
} gputrace_event_malloc;

typedef struct gputrace_event_memcpy {
    void* dst;
    void* src;
    size_t size;
    hipMemcpyKind kind;
    std::vector<std::byte> hostdata;
} gputrace_event_memcpy;

typedef struct gputrace_event_free {
    void* p;
} gputrace_event_free;

typedef struct gputrace_event_launch {
    const char* kernel_name;
    dim3 num_blocks;
    dim3 dim_blocks;
    int shared_mem_bytes;
    std::vector<std::byte> argdata;
} gputrace_event_launch;

#endif // __HIP_TRACE_H__
