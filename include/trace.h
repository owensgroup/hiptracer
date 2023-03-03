#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <string>
#include <vector>
#include <variant>
#include <cstddef>
#include <thread>
#include <condition_variable>

#include "elf.h"

#include "atomic_queue/atomic_queue.h"
#include "sqlite3.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

struct Instr {
    std::string cdna;
    const char* getCdna() {
        return cdna.c_str();
    }
    uint32_t getOffset();
    uint32_t getIdx();

    //const char* getOpcode();

    bool isLoad();
    bool isStore();
    bool isBranch();

    int getNumOperands();

    int num_operands;
    size_t size;
    size_t offset;
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
    const void* function_address;
    dim3 num_blocks;
    dim3 dim_blocks;
    int shared_mem_bytes;
    std::vector<std::byte> argdata;
    std::string kernel_name;
};

struct gputrace_event_code {
    std::string filename;
    std::string code;
};

struct gputrace_event {
    uint64_t id;
    hipError_t rc;
    hipStream_t stream;
    const char* name;

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
std::unique_ptr<atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS>>& get_events_queue();
std::condition_variable& get_events_available();
std::atomic<int>& get_curr_event();
sqlite3*& get_event_db();
sqlite3*& get_arginfo_db();
std::unique_ptr<std::thread>& get_db_writer_thread();
HIPTRACER_TOOL& get_tool();
bool& get_library_loaded();
std::string& get_db_path();
std::string& get_rocm_path();
void*& get_rocm_lib();
std::unique_ptr<ska::flat_hash_map<uint64_t, SizeOffset>>& get_kernel_arg_sizes();
std::unique_ptr<ska::flat_hash_map<uint64_t, bool>>& get_handled_fatbins();
void events_wait();
void pushback_event(gputrace_event event);

#endif // __HIP_TRACE_H__
