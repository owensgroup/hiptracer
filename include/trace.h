#ifndef __HIP_TRACE_H__
#define __HIP_TRACE_H__

#include <string>
#include <vector>
#include <dlfcn.h>

#include <variant>
#include <filesystem>
#include <cstddef>
#include <thread>
#include <condition_variable>

#include "elf.h"

#include "atomic_queue/atomic_queue.h"
#include "sqlite3.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#define HIPT_APP_INSTRCOUNT 1
#define HIPT_APP_MEMTRACE 2
#define HIPT_APP_COALESCE 3
#define HIPT_APP_BANKCONFLICT 4

#define HIPT_INSTRUMENTED_INSTR 2

#define HIPT_CURRENT_APP HIPT_APP_INSTRCOUNT

struct Instr {
    std::string cdna;
    int num_operands = 0;
    size_t size = 0;
    size_t offset = 0;

    std::vector<char> data;

    const char* getCdna() {
        return cdna.c_str();
    }
    uint32_t getOffset() {
        return offset;
    }

    //const char* getOpcode();

    bool isLoad() {
        assert(cdna.size() > 0);
        //std::printf("CHECKING IF LOAD\n");
        if (cdna.size() <= 0) {
            return false;
        }
        //std::printf("CDNA %s\n", cdna.c_str());
        return cdna.find("LOAD") != std::string::npos;
    }
    bool isStore() {
        assert(cdna.size() > 0);
        //std::printf("CHECKING IF STORE\n");
        if (cdna.size() <= 0) {
            return false;
        }
        //std::printf("CDNA %s\n", cdna.c_str());
        return cdna.find("STORE") != std::string::npos;
    }
    bool isFlat() {
        assert(cdna.size() > 0);

        if (cdna.size() <= 0) {
            return false;
        }
        return cdna.find("FLAT") != std::string::npos;
    }
    bool isGlobal() {
        return cdna.find("GLOBAL") != std::string::npos;
    }
    bool isBranch();

    int getNumOperands();

};

__global__ void zero_atomics(int*);

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
    TOOL_BININT
};

void prepare_events();

#define MAX_ELEMS 8192*4
struct hiptracer_state {
    hipError_t  (*malloc_fptr)(void**, size_t) = NULL;
    uint64_t* memtrace = NULL;

    void (*at_init_fptr)() = NULL;
    void (*at_launch_fptr)(int, const void*, const char*, void**) = NULL;

    uint64_t atomic = NULL;
    uint64_t buffer = NULL;

    std::vector<std::string> filenames;

    atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS> events_queue;
    std::thread* db_writer_thread = nullptr;
    std::mutex mx;
    std::condition_variable events_available;
    std::atomic<int> curr_event = 0;
    sqlite3* event_db = nullptr;
    sqlite3* arginfo_db = nullptr;
    HIPTRACER_TOOL tool = TOOL_CAPTURE;
    bool library_loaded = true;
    std::string db_path = "./tracer-default.db";
    std::string rocm_path = "/opt/rocm/lib/libamdhip64.so";
    void* rocm_lib = nullptr;
    ska::flat_hash_map<uint64_t, SizeOffset> kernel_arg_sizes;
    ska::flat_hash_map<uint64_t, bool> handled_fatbins;
    
    void wait_for_events() {
        std::unique_lock lock(mx);
        events_available.wait(lock);
    }
    
    void init_capture() { 
        if (sqlite3_open(db_path.c_str(), &event_db) != SQLITE_OK) {
            std::printf("Unable to open event database: %s\n", sqlite3_errmsg(event_db));
            sqlite3_close(event_db);
            event_db = nullptr;
        }
        assert(event_db);

        if (sqlite3_open("./code/arginfo.db", &arginfo_db) != SQLITE_OK) {
            std::printf("Unable to open arginfo database: %s\n", sqlite3_errmsg(arginfo_db));
            sqlite3_close(arginfo_db);
            arginfo_db = nullptr;
        }
        assert(arginfo_db);
 
        sqlite3_config(SQLITE_CONFIG_MULTITHREAD);

        const char* create_events_sql = "PRAGMA journal_mode=wal;"
                                  "DROP TABLE IF EXISTS Events;"
                                  "DROP TABLE IF EXISTS EventMalloc;"
                                  "DROP TABLE IF EXISTS EventMemcpy;"
                                  "DROP TABLE IF EXISTS EventLaunch;"
                                  "DROP TABLE IF EXISTS EventFree;"
                                  "DROP TABLE IF EXISTS Code;"
                                  "DROP TABLE IF EXISTS ArgInfo;"
                                  "CREATE TABLE Code(Id INTEGER PRIMARY KEY, Path TEXT);"
                                  "CREATE TABLE Events(Id INTEGER PRIMARY KEY, EventType INT, Name TEXT, Rc INT, Stream INT);"
                                  "CREATE TABLE EventMalloc(Id INTEGER PRIMARY KEY, Stream INT, Ptr INT64, Size INT);"
                                  "CREATE TABLE EventMemcpy(Id INTEGER PRIMARY KEY, Stream INT, Dst INT64, Src INT64, Size INT, Kind INT, HostData BLOB);"
                                  "CREATE TABLE EventLaunch(Id INTEGER PRIMARY KEY, Stream INT, KernelName TEXT, NumX INT, NumY INT, NumZ INT,DimX INT, DimY INT, DimZ INT, SharedMem INT, ArgData BLOB);"
                                  "CREATE TABLE EventFree(Id INTEGER PRIMARY KEY, Stream INT, Ptr INT);"
                                  "CREATE TABLE ArgInfo(Id INTEGER PRIMARY KEY, KernelName TEXT, Ind INT, AddressSpace TEXT, Size INT, Offset INT, ValueKind TEXT, Access TEXT);";
        const char* create_arginfo_sql = "PRAGMA synchronous = OFF;"
                                         "PRAGMA journal_mode=OFF;"
                                         "DROP TABLE IF EXISTS ArgInfo;"
                                  "CREATE TABLE ArgInfo(Id INTEGER PRIMARY KEY, KernelName TEXT, Ind INT, AddressSpace TEXT, Size INT, Offset INT, ValueKind TEXT, Access TEXT);";
        if(sqlite3_exec(event_db, create_events_sql, 0, 0, NULL)) {
            std::printf("Error creating events table: %s\n", sqlite3_errmsg(event_db));
            sqlite3_close(event_db);
            event_db = nullptr;

        }
        assert(event_db);

        if(sqlite3_exec(arginfo_db, create_arginfo_sql, 0, 0, NULL)) {  
            std::printf("Error creating arginfo table: %s\n", sqlite3_errmsg(arginfo_db));
            sqlite3_close(arginfo_db);
            arginfo_db = nullptr;
        }
        assert(arginfo_db);

        library_loaded = true;
    }
    
    hiptracer_state() {
        // Setup options
        //auto as_bool = [](char* env) { return (env != NULL) && std::string(env) == "true"; };
        //std::printf("Using %d bytes for events\n", sizeof(gputrace_event) * MAX_ELEMS);
    
        //std::printf("OPENING %s\n", rocm_path.c_str());
        rocm_lib = dlopen(rocm_path.c_str(), RTLD_LAZY | RTLD_LOCAL);        
        if (rocm_lib == NULL) {
            std::printf("Unable to open libamdhip64.so\n");
        }
        //std::printf("OPENED\n");
        assert(rocm_lib);
    
        //DEBUG = as_bool(std::getenv("HIPTRACER_DEBUG")); 
        
        char* eventdb = std::getenv("HIPTRACER_EVENTDB");        
        if (eventdb != NULL) {
            db_path = std::string(eventdb);
            std::printf("Using %s\n", db_path.c_str());
            
        }        

        char* tool_str = std::getenv("HIPTRACER_MODE");
        if (tool_str != NULL) {
            std::printf("TOOL %s\n", tool_str);

            std::filesystem::create_directory("./hostdata");
            std::filesystem::create_directory("./code");

            if (std::string(tool_str) == "capture") {
                tool = TOOL_CAPTURE;
                init_capture();
            } else if (std::string(tool_str) == "binint") {
                tool = TOOL_BININT;
            
                //char* tool_code = std::getenv("HIPTRACER_TOOL");
                //void* tool_lib = dlopen(tool_code, RTLD_NOW);
                //assert(tool_lib);
                //std::printf("TOOL CODE %s\n", tool_code);
                if (malloc_fptr == NULL) {
                    malloc_fptr = (hipError_t (*) (void**, size_t)) dlsym(rocm_lib, "hipMalloc");
                }
                //if (at_init_fptr == NULL) {
                //    at_init_fptr = (void (*) ()) dlsym(tool_lib, "_Z12hipt_at_initv");
                //}
                //if (at_launch_fptr == NULL) {
                //    at_launch_fptr = (void (*) (int, const void*, const char*, void **)) dlsym(tool_lib, "_Z14hipt_at_launchiPvPKcPS_");
                //}
                assert(malloc_fptr);
                //assert(at_init_fptr);
                //assert(at_launch_fptr);
            } else {
                tool = TOOL_CAPTURE;
                init_capture();
            }
        }
    }
    
    ~hiptracer_state() {
        if (tool == TOOL_CAPTURE) {
            printf("HELLO?\n");
            library_loaded = false;
            events_available.notify_all();

            sqlite3_close(event_db);
            sqlite3_close(arginfo_db);

            if (db_writer_thread != NULL) {
                db_writer_thread->join();
            }
            delete db_writer_thread;
        } else if (tool == TOOL_BININT) {
            int value = -1;

            hipError_t (*hipDeviceSynchronize_fptr)() = NULL;
            hipDeviceSynchronize_fptr = (hipError_t (*) ()) dlsym(rocm_lib, "hipDeviceSynchronize");

            hipError_t sync_result = hipDeviceSynchronize_fptr();
            assert(sync_result == hipSuccess);

            hipError_t  (*hipMemcpy_fptr)(void*, const void*, size_t, hipMemcpyKind) = NULL;
            
            if (hipMemcpy_fptr == NULL) {
                hipMemcpy_fptr = (hipError_t (*) (void*, const void*, size_t, hipMemcpyKind)) dlsym(rocm_lib, "hipMemcpy");
            }

            if (HIPT_CURRENT_APP == HIPT_APP_INSTRCOUNT || HIPT_CURRENT_APP == HIPT_APP_MEMTRACE) {
                hipError_t memcpy_result = hipMemcpy_fptr(&value, (void*) atomic, sizeof(int), hipMemcpyDeviceToHost);
                assert(memcpy_result == hipSuccess);
                std::printf("COUNT: %d\n", value);
            }
            if (HIPT_CURRENT_APP == HIPT_APP_MEMTRACE) {
                std::vector<uint64_t> host_buffer(3000000 + 10);
                host_buffer[0] = 0;
                hipError_t memcpy_result = hipMemcpy_fptr(host_buffer.data(), (void*) buffer, sizeof(uint64_t) * host_buffer.size(), hipMemcpyDeviceToHost);

                assert(memcpy_result == hipSuccess);

                std::printf("VALUE IS %d\n", value);
                for (int i = 0 ; i < host_buffer.size(); i++) {
                    std::printf("BUFFER [%d] is %p\n", i, host_buffer[i]);
                    if (i % 96 == 95)
                    break;
                }
            }
        }
    }
};

hiptracer_state& get_hiptracer_state();
atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS>& get_events_queue();
std::atomic<int>& get_curr_event();
sqlite3*& get_event_db();
sqlite3*& get_arginfo_db();
HIPTRACER_TOOL& get_tool();
bool& get_library_loaded();
std::string& get_db_path();
std::string& get_rocm_path();
void*& get_rocm_lib();
uint64_t& get_atomic_addr();
uint64_t& get_buffer_addr();

ska::flat_hash_map<uint64_t, SizeOffset>& get_kernel_arg_sizes();
ska::flat_hash_map<uint64_t, bool>& get_handled_fatbins();
std::vector<std::string>& get_filenames();
void events_wait();
void pushback_event(gputrace_event event);

#endif // __HIP_TRACE_H__
