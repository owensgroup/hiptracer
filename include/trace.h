#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <vector>
#include <variant>
#include <string>
#include <cstddef>
#include <thread>
#include <condition_variable>

#include "sqlite3.h"
#include "atomic_queue/atomic_queue.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

struct Instr {
    const char* getCdna(); // Return the string containing the full text of the instruction
    uint32_t getOffset();
    uint32_t getIdx();

    const char* getOpcode();

    bool isLoad();
    bool isStore();
    bool isBranch();

    int getNumOperands();
};

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
    const void* kernel_pointer;
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

enum HIPTRACER_TOOL {
    TOOL_CAPTURE,
    TOOL_MEMTRACE
};

#define MAX_ELEMS 8192*8
extern atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS> events_queue;
extern std::atomic<int> g_curr_event;
extern sqlite3 *g_event_db;
extern sqlite3 *g_arginfo_db;
extern void* rocmLibHandle;
extern void pushback_event(gputrace_event);
extern std::condition_variable events_available;


extern HIPTRACER_TOOL TOOL;

#endif // __HIP_TRACE_H__
