#include <cstdio>
#include <cstddef>
#include <vector>
#include <iostream>
#include <cstring>
#include <string>
#include <filesystem>
#include <condition_variable>

#include <dlfcn.h>

#include <zstd.h>

// event_t, header_t, data_t
#include "trace.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#include "sqlite3.h"
#include "atomic_queue/atomic_queue.h"
#include "progressbar.h"

void prepare_events();

std::unique_ptr<atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS>>& get_events_queue() {
    static std::unique_ptr<atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS>> events_queue(new atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS>);
    return events_queue;
}
std::condition_variable& get_events_available() {
    static std::condition_variable events_available;
    return events_available;
}
std::atomic<int>& get_curr_event() {
    static std::atomic<int> g_curr_event;
    return g_curr_event;
}
sqlite3*& get_event_db() {
    static sqlite3* g_event_db;
    return g_event_db;
}
sqlite3*& get_arginfo_db() {
    static sqlite3* g_arginfo_db;
    return g_arginfo_db;
}

std::unique_ptr<std::thread>& get_db_writer_thread() {
    static std::unique_ptr<std::thread> db_writer_thread(new std::thread(prepare_events));
    return db_writer_thread;
}

HIPTRACER_TOOL& get_tool() {
    static HIPTRACER_TOOL TOOL = TOOL_CAPTURE;
    return TOOL;
}

bool& get_library_loaded() {
    static bool g_library_loaded = true;
    return g_library_loaded;
}

std::string& get_db_path() {
    static std::string db_path = "./tracer-default.db";
    return db_path;
}

std::string& get_rocm_path() {
    static std::string rocm_path = "/opt/rocm/lib/libamdhip64.so";
    return rocm_path;
}
void*& get_rocm_lib() {
    static void* rocmLibHandle = NULL;
    return rocmLibHandle;
}

std::unique_ptr<ska::flat_hash_map<uint64_t, SizeOffset>>& get_kernel_arg_sizes() {
    static std::unique_ptr<ska::flat_hash_map<uint64_t, SizeOffset>> kernel_arg_sizes( new ska::flat_hash_map<uint64_t, SizeOffset>);
    return kernel_arg_sizes;
}

std::unique_ptr<ska::flat_hash_map<uint64_t, bool>>& get_handled_fatbins() {
    static std::unique_ptr<ska::flat_hash_map<uint64_t, bool>> handled_fatbins(new ska::flat_hash_map<uint64_t, bool>);
    return handled_fatbins;
}

void events_wait() {
    static std::mutex mx;
    static std::unique_lock<std::mutex> lock(mx);
    get_events_available().wait(lock);
}

void pushback_event(gputrace_event event)
{
    static bool notified = false;

    if (!notified && get_events_queue()->was_full()) {
        notified = true;
        std::printf("Event queue full - Spinning on main thread\nUse a larger buffer size or allocate more threads for event insertion\n");
    }
    get_events_queue()->push(event);
    get_events_available().notify_one();
}

int SQLITE_CHECK(int ans) \
  { \
    if (ans == SQLITE_OK) {
        {}
    } else if (ans == SQLITE_DONE) {
        std::printf("SQLITE_DONE AT %s:%d (_step?)\n", __FILE__, __LINE__);
    } else if (ans == SQLITE_ROW) {
        std::printf("SQLITE_ROW AT %s:%d (_step?)\n", __FILE__, __LINE__);
    } else if (ans != SQLITE_OK) {
        std::printf("SQLITE ERROR AT %s:%d %s\n", __FILE__, __LINE__, sqlite3_errmsg(get_event_db()));
        assert(false);
    } else {
        std::printf("UNKNOWN SQL RESULT AT %s:%d (%d) \n", __FILE__, __LINE__, ans);
        assert(false);
    }
    return ans;
  }


void init_capture()
{
    std::filesystem::create_directory("./hostdata");
    std::filesystem::create_directory("./code");
    
    get_rocm_lib() = dlopen(get_rocm_path().c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (get_rocm_lib() == NULL) {
        std::printf("Unable to open libamdhip64.so\n");
    }
    assert(get_rocm_lib());
    
    if (sqlite3_open(get_db_path().c_str(), &get_event_db()) != SQLITE_OK) {
        std::printf("Unable to open event database: %s\n", sqlite3_errmsg(get_event_db()));
        sqlite3_close(get_event_db());
        get_event_db() = nullptr;
    }
    assert(get_event_db());

    if (sqlite3_open("./code/arginfo.db", &get_arginfo_db()) != SQLITE_OK) {
        std::printf("Unable to open arginfo database: %s\n", sqlite3_errmsg(get_arginfo_db()));
        sqlite3_close(get_arginfo_db());
        get_arginfo_db() = nullptr;
    }
    assert(get_arginfo_db());
    std::printf("Can join?\n");
 
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
    if(sqlite3_exec(get_event_db(), create_events_sql, 0, 0, NULL)) {
        std::printf("Error creating events table: %s\n", sqlite3_errmsg(get_event_db()));
        sqlite3_close(get_event_db());
        get_event_db() = nullptr;

    }
    assert(get_event_db());

    if(sqlite3_exec(get_arginfo_db(), create_arginfo_sql, 0, 0, NULL)) {  
        std::printf("Error creating arginfo table: %s\n", sqlite3_errmsg(get_arginfo_db()));
        sqlite3_close(get_arginfo_db());
        get_arginfo_db() = nullptr;
    }
    assert(get_arginfo_db());

    get_library_loaded() = true;
    if (!get_db_writer_thread()->joinable()) {
        std::printf("Failed to start thread\n");
    };
}


__attribute__((constructor)) void hiptracer_init()
{
    // Setup options
    //auto as_bool = [](char* env) { return (env != NULL) && std::string(env) == "true"; };
    std::printf("Using %d bytes for events\n", sizeof(gputrace_event) * MAX_ELEMS);
    
    //DEBUG = as_bool(std::getenv("HIPTRACER_DEBUG")); 
    char* eventdb = std::getenv("HIPTRACER_EVENTDB");
    if (eventdb != NULL) {
        std::string& db_path = get_db_path();
        db_path = std::string(eventdb);
    }

    char* tool = std::getenv("HIPTRACER_TOOL");
    if (tool != NULL) {
        std::printf("TOOL %s\n", tool);
        HIPTRACER_TOOL& TOOL = get_tool();

        if (std::string(tool) == "capture") {
            TOOL = TOOL_CAPTURE;
            init_capture();
        } else if (std::string(tool) == "memtrace") {
            TOOL = TOOL_MEMTRACE;
        } else {
            TOOL = TOOL_CAPTURE;
            init_capture();
        }
    }
}

__attribute__((destructor)) void hiptracer_deinit()
{
    if (get_tool() == TOOL_CAPTURE) {
        get_library_loaded() = false;
        get_events_available().notify_all();

        sqlite3_close(get_event_db());
        sqlite3_close(get_arginfo_db());

        if (get_db_writer_thread() != NULL) {
            (*get_db_writer_thread()).join();
        }
    } else if (get_tool() = TOOL_MEMTRACE) {
        
    }
}

std::vector<std::byte> compress_data(void *data, size_t size)
{
    size_t cBuffSize = ZSTD_compressBound(size);
        
    std::vector<std::byte> compressed;
    compressed.reserve(cBuffSize);
    size_t actualSize = ZSTD_compress(compressed.data(), cBuffSize, data, size, 1);

    return compressed;
}

int insert_code(gputrace_event event, sqlite3_stmt* pStmt)
{
    gputrace_event_code code_event = std::get<gputrace_event_code>(event.data);

    std::FILE* code = std::fopen(code_event.filename.c_str(), "wb");
    std::fwrite(code_event.code.data(), sizeof(std::byte), code_event.code.size(), code);
    std::fclose(code);

    int rc = SQLITE_CHECK(sqlite3_bind_text(pStmt, 1, code_event.filename.c_str(), -1, SQLITE_TRANSIENT));
    sqlite3_step(pStmt);
    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);

    return rc;
}

int insert_free(gputrace_event event, sqlite3_stmt* pStmt)
{
    int rc = sqlite3_bind_int(pStmt, 1, event.id);
    rc = sqlite3_bind_int(pStmt, 2, (uintptr_t) event.stream);

    gputrace_event_free free_event = std::get<gputrace_event_free>(event.data);
    rc = sqlite3_bind_int64(pStmt, 3, (uintptr_t) free_event.p);

    rc = sqlite3_step(pStmt);
    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);
    return rc;
}

int insert_launch(gputrace_event event, sqlite3_stmt* pStmt)
{
    gputrace_event_launch launch_event = std::get<gputrace_event_launch>(event.data);

    int rc = sqlite3_bind_int(pStmt, 1, event.id);
    rc = sqlite3_bind_int(pStmt, 2, (uint64_t) event.stream);
    rc = sqlite3_bind_text(pStmt, 3, launch_event.kernel_name.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_bind_int(pStmt, 4, launch_event.num_blocks.x);
    rc = sqlite3_bind_int(pStmt, 5, launch_event.num_blocks.y);
    rc = sqlite3_bind_int(pStmt, 6, launch_event.num_blocks.z);
    rc = sqlite3_bind_int(pStmt, 7, launch_event.dim_blocks.x);
    rc = sqlite3_bind_int(pStmt, 8, launch_event.dim_blocks.y);
    rc = sqlite3_bind_int(pStmt, 9, launch_event.dim_blocks.z);
    rc = sqlite3_bind_int(pStmt, 10, launch_event.shared_mem_bytes);

    rc = sqlite3_bind_blob(pStmt, 11, launch_event.argdata.data(), launch_event.argdata.size(), SQLITE_TRANSIENT);

    rc = sqlite3_step(pStmt);
    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);
    return rc;
}

int insert_malloc(gputrace_event event, sqlite3_stmt* pStmt)
{
    gputrace_event_malloc malloc_event = std::get<gputrace_event_malloc>(event.data);

    int rc = sqlite3_bind_int(pStmt, 1, event.id);
    rc = sqlite3_bind_int(pStmt, 2, (uint64_t) event.stream);
    rc = sqlite3_bind_int64(pStmt, 3, (uint64_t) malloc_event.p);
    rc = sqlite3_bind_int(pStmt, 4, malloc_event.size);
 
    rc = sqlite3_step(pStmt);

    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);
    return rc;
}

int insert_memcpy(gputrace_event event, sqlite3_stmt* pStmt)
{
    gputrace_event_memcpy memcpy_event = std::get<gputrace_event_memcpy>(event.data);

    int rc = sqlite3_bind_int(pStmt, 1, event.id);

    rc = sqlite3_bind_int(pStmt, 2, (uint64_t) event.stream);
    rc = sqlite3_bind_int64(pStmt, 3, (uint64_t) memcpy_event.dst);
    rc = sqlite3_bind_int64(pStmt, 4, (uint64_t) memcpy_event.src);
    rc = sqlite3_bind_int(pStmt, 5, memcpy_event.size);

    rc = sqlite3_bind_int(pStmt, 6, memcpy_event.kind);

    if (memcpy_event.kind == hipMemcpyHostToDevice) {
        //rc = sqlite3_bind_blob(pStmt, 7, memcpy_event.hostdata.data(), memcpy_event.hostdata.size(), SQLITE_STATIC);
        std::string filename = "./hostdata/" + std::string("hostdata-") + std::to_string(event.id) + ".bin";
        std::FILE* fp = std::fopen(filename.c_str(), "wb");
        std::fwrite(memcpy_event.hostdata.data(), memcpy_event.hostdata.size(), 1, fp);
        std::fclose(fp);
    }

    rc = sqlite3_step(pStmt);

    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);
    return rc;
}

int insert_event(gputrace_event event, sqlite3_stmt* pStmt)
{ 
    int rc = sqlite3_bind_int(pStmt, 1, event.type);
    rc = sqlite3_bind_text(pStmt, 2, event.name, -1, SQLITE_STATIC);
    rc = sqlite3_bind_int(pStmt, 3, event.rc);
    rc = sqlite3_bind_int(pStmt, 4, (uint64_t) event.stream);
    rc = sqlite3_bind_int(pStmt, 5, event.id);

    rc = sqlite3_step(pStmt);
    if (rc != SQLITE_DONE) {
        std::printf("Not able to insert event %s\n", sqlite3_errmsg(get_event_db()));
    }
    sqlite3_reset(pStmt);
    sqlite3_clear_bindings(pStmt);
    return rc;
}

void prepare_events()
{
    std::printf("PREPARING\n");
    sqlite3_stmt* eventStmt = NULL;
    sqlite3_stmt* mallocStmt = NULL;
    sqlite3_stmt* memcpyStmt = NULL;
    sqlite3_stmt* freeStmt = NULL;
    sqlite3_stmt* launchStmt = NULL;
    sqlite3_stmt* codeStmt = NULL;

    const char* eventSql = "INSERT INTO Events(EventType, Name, Rc, Stream, Id) VALUES(?, ?, ?, ?, ?);";
    const char* memcpySql = "INSERT INTO EventMemcpy(Id, Stream, Dst, Src, Size, Kind, HostData) VALUES(?, ?, ?, ?, ?, ?, ?);";
    const char* mallocSql = "INSERT INTO EventMalloc(Id, Stream, Ptr, Size) VALUES(?, ?, ?, ?);";
    const char* launchSql = "INSERT INTO EventLaunch(Id, Stream, KernelName, NumX, NumY, NumZ, DimX, DimY, DimZ, SharedMem, ArgData)"
                            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    const char* freeSql = "INSERT INTO EventFree(Id, Stream, Ptr) VALUES(?, ?, ?);";
    const char* codeSql = "INSERT INTO Code(Path) VALUES(?)";

    sqlite3*& g_event_db = get_event_db();
    sqlite3_prepare_v2(g_event_db, eventSql, -1, &eventStmt, 0);
    sqlite3_prepare_v2(g_event_db, memcpySql, -1, &memcpyStmt, 0);
    sqlite3_prepare_v2(g_event_db, mallocSql, -1, &mallocStmt, 0);
    sqlite3_prepare_v2(g_event_db, launchSql, -1, &launchStmt, 0);
    sqlite3_prepare_v2(g_event_db, freeSql, -1, &freeStmt, 0);
    sqlite3_prepare_v2(g_event_db, codeSql, -1, &codeStmt, 0);

    char* errmsg = NULL;
    sqlite3_exec(g_event_db, "BEGIN TRANSACTION", NULL, NULL, &errmsg);

    std::printf("Prepared statements\n");
	progressbar* progress = NULL;
    while(get_library_loaded()) {
        while(!get_events_queue()->was_empty()) {      
            if (!get_library_loaded() && progress == NULL) {
                // Display progress of remaining statements
                size_t events_remaining = get_events_queue()->was_size();
				progress = new progressbar(events_remaining);
            }
            gputrace_event event;
            
            get_events_queue()->try_pop(event);

            if (event.type != EVENT_CODE) {
                insert_event(event, eventStmt);
            }
            if (event.type == EVENT_MALLOC) { 
                insert_malloc(event, mallocStmt);
            } else if (event.type == EVENT_MEMCPY) {
                insert_memcpy(event, memcpyStmt);
            } else if (event.type == EVENT_FREE) {
                insert_free(event, freeStmt);
            } else if (event.type == EVENT_LAUNCH) {
                insert_launch(event, launchStmt);
            } else if (event.type == EVENT_CODE) {
                insert_code(event, codeStmt);
            }
			if (progress != NULL) {
				progress->update();
			}
        }
        if (get_events_queue()->was_empty() && get_library_loaded()) {
            events_wait();
        }
    }

    sqlite3_exec(get_event_db(), "END TRANSACTION", NULL, NULL, &errmsg);

	std::printf("\nCAPTURE COMPLETE\n");

    sqlite3_finalize(eventStmt);
    sqlite3_finalize(mallocStmt);
    sqlite3_finalize(memcpyStmt);
    sqlite3_finalize(freeStmt);
    sqlite3_finalize(launchStmt);
}



/*
   ****
   HIP Functions
   ****
*/

hipError_t  (*hipFree_fptr)(void*) = NULL;
hipError_t  (*hipMalloc_fptr)(void**, size_t) = NULL;
hipError_t  (*hipMemcpy_fptr)(void*, const void*, size_t, hipMemcpyKind) = NULL;
hipError_t  (*hipGetDeviceProperties_fptr)(hipDeviceProp_t*,int) = NULL;

hipError_t  (*hipSetupArgument_fptr)(const void* arg, size_t size, size_t offset) = NULL;
hipChannelFormatDesc (*hipCreateChannelDesc_fptr)(int x, int y, int z, int w, hipChannelFormatKind f) = NULL;

hipError_t (*hipStreamSynchronize_fptr)(hipStream_t) = NULL;
hipError_t (*hipDeviceSynchronize_fptr)() = NULL;
hipError_t (*hipStreamCreate_fptr)(hipStream_t*) = NULL;
hipError_t (*hipStreamCreateWithFlags_fptr)(hipStream_t*, unsigned int) = NULL;
hipError_t (*hipGetDeviceCount_fptr)(int*) = NULL;
hipError_t (*hipGetDevice_fptr)(int*) = NULL;
hipError_t (*hipStreamDestroy_fptr)(hipStream_t) = NULL;
hipError_t (*hipStreamQuery_fptr)(hipStream_t) = NULL;
hipError_t (*hipStreamWaitEvent_fptr)(hipStream_t, hipEvent_t, unsigned int) = NULL;

hipError_t (*hipEventCreate_fptr)(hipEvent_t *) = NULL;
hipError_t (*hipEventCreateWithFlags_fptr)(hipEvent_t *, unsigned) = NULL;
hipError_t (*hipEventRecord_fptr)(hipEvent_t, hipStream_t) = NULL;
hipError_t (*hipEventDestroy_fptr)(hipEvent_t) = NULL;
hipError_t (*hipEventSynchronize_fptr)(hipEvent_t) = NULL;
hipError_t (*hipEventQuery_fptr)(hipEvent_t event) = NULL;

hipError_t (*hipMemcpyWithStream_fptr)(void*, const void*, size_t, hipMemcpyKind, hipStream_t) = NULL;


extern "C" {
hipError_t hipStreamCreateWithFlags(hipStream_t* stream, unsigned flags) {
    if (hipStreamCreateWithFlags_fptr == NULL) {
        hipStreamCreateWithFlags_fptr = (hipError_t (*) (hipStream_t*, unsigned)) dlsym(get_rocm_lib(), "hipStreamCreateWithFlags");
    }

    hipError_t result = (*hipStreamCreateWithFlags)(stream, flags);

    if (get_tool() == TOOL_CAPTURE) {
        gputrace_event event;

        event.id = get_curr_event()++;
        event.name = "hipStreamCreateWithFlags";
        event.rc = result;
        if (stream != NULL) {
            event.stream = *stream;
        }
        event.type = EVENT_STREAM;

        pushback_event(event);
    }

    return result;
}

hipError_t hipStreamCreate(hipStream_t* stream)
{
    if (hipStreamCreate_fptr == NULL) {
        hipStreamCreate_fptr = (hipError_t (*) (hipStream_t*)) dlsym(get_rocm_lib(), "hipStreamCreate");
    }

    hipError_t result = (*hipStreamCreate_fptr)(stream);
 
    if (get_tool() == TOOL_CAPTURE) {
        gputrace_event event;

        event.id = get_curr_event()++;
        event.name = "hipStreamCreate";
        event.rc = result;
        if (stream != NULL) {
                event.stream = *stream;
        }
        event.type = EVENT_STREAM;

        pushback_event(event);        
    }

    return result;
}

hipError_t hipStreamDestroy(hipStream_t stream)
{
    if (hipStreamDestroy_fptr == NULL) {
        hipStreamDestroy_fptr = (hipError_t (*) (hipStream_t)) dlsym(get_rocm_lib(), "hipStreamDestroy");
    }

    hipError_t result = (*hipStreamDestroy_fptr)(stream);

    gputrace_event event;

    event.id = get_curr_event()++;
    event.name = "hipStreamDestroy";
    event.rc = result;
    event.stream = stream;
    event.type = EVENT_STREAM;

    pushback_event(event);

    return result;
}

hipError_t hipGetDevice(int* deviceId) {
    if (hipGetDevice_fptr == NULL) {
        hipGetDevice_fptr = (hipError_t (*) (int* deviceId)) dlsym(get_rocm_lib(), "hipGetDevice");
    }

    hipError_t result = (*hipGetDevice_fptr)(deviceId);

    //gputrace_event* event;
    
    gputrace_event event;

    event.id = get_curr_event()++;
    event.name = "hipGetDevice";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_DEVICE;

    pushback_event(event);

    return result;
}


hipError_t hipGetDeviceCount(int* count) {
    if (hipGetDeviceCount_fptr == NULL) {
        hipGetDeviceCount_fptr = (hipError_t (*) (int* count)) dlsym(get_rocm_lib(), "hipGetDeviceCount");
    }

    hipError_t result = (*hipGetDeviceCount_fptr)(count);

    //gputrace_event* event;

    gputrace_event event;
    event.id = get_curr_event()++;
    event.name = "hipGetDeviceCount";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_DEVICE;

    pushback_event(event);

    return result;
}
hipError_t hipStreamSynchronize(hipStream_t stream)
{
    if (hipStreamSynchronize_fptr == NULL) {
        hipStreamSynchronize_fptr = (hipError_t (*) (hipStream_t)) dlsym(get_rocm_lib(), "hipStreamSynchronize");
    }

    hipError_t result = (*hipStreamSynchronize_fptr)(stream);

    //gputrace_event* event;

    gputrace_event event;

    event.id = get_curr_event()++;
    event.name = "hipStreamSynchronize";
    event.rc = result;
    event.stream = stream;
    event.type = EVENT_SYNC;

    pushback_event(event);

    return result;
}

hipError_t hipDeviceSynchronize()
{
    if (hipDeviceSynchronize_fptr == NULL) {
        hipDeviceSynchronize_fptr = (hipError_t (*) ()) dlsym(get_rocm_lib(), "hipDeviceSynchronize");
    }

    hipError_t result = (*hipDeviceSynchronize_fptr)();

    //gputrace_event* event;

    gputrace_event event;

    event.id = get_curr_event()++;
    event.name = "hipDeviceSynchronize";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_SYNC;

    pushback_event(event);

    return result;
}

hipError_t hipFree(void *p)
{
    if (hipFree_fptr == NULL) {
        hipFree_fptr = (hipError_t (*) (void*)) dlsym(get_rocm_lib(), "hipFree");
    }

    hipError_t result = (*hipFree_fptr)(p);

    //gputrace_event* event;

    gputrace_event event;
    gputrace_event_free free_event;

    event.id = get_curr_event()++;
    event.name = "hipFree";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_FREE;

    free_event.p = (uint64_t) p;

    event.data = std::move(free_event);
    pushback_event(event);

    return result;

}

hipError_t hipMalloc(void** p, size_t size)
{
    if (hipMalloc_fptr == NULL) {
        hipMalloc_fptr = (hipError_t (*) (void**, size_t)) dlsym(get_rocm_lib(), "hipMalloc");
    }

    hipError_t result = (*hipMalloc_fptr)(p, size);

    gputrace_event event;
    gputrace_event_malloc malloc_event;
    event.id = get_curr_event()++;
    event.type = EVENT_MALLOC;
    event.name = "hipMalloc";
    event.rc = result;
    event.stream = hipStreamDefault;
    if (p == NULL) {
        malloc_event.p = 0;
    } else {
        malloc_event.p = (uint64_t) *p;
    }
    malloc_event.size = size;
    event.data = std::move(malloc_event);

    pushback_event(event);

    return result;
}

hipError_t hipMemcpy(void *dst, const void *src, size_t size, hipMemcpyKind kind)
{ 
    if (hipMemcpy_fptr == NULL) {
        hipMemcpy_fptr = (hipError_t (*) (void*, const void*, size_t, hipMemcpyKind)) dlsym(get_rocm_lib(), "hipMemcpy");
    }

    hipError_t result = (*hipMemcpy_fptr)(dst, src, size, kind);

    gputrace_event event;
    gputrace_event_memcpy memcpy_event;

    event.id = get_curr_event()++;
    event.name = "hipMemcpy";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_MEMCPY;

    memcpy_event.dst = (uint64_t) dst;
    memcpy_event.src = (uint64_t) src;	
    memcpy_event.size = (uint64_t) size;
    memcpy_event.kind = kind;
    if (memcpy_event.kind == hipMemcpyHostToDevice) {
        memcpy_event.hostdata.resize(size);
        std::memcpy(memcpy_event.hostdata.data(), src, size);
    }

    event.data = std::move(memcpy_event);

    pushback_event(event);

    return result;    
}

hipError_t hipMemcpyWithStream(void* dst, const void* src, size_t size, hipMemcpyKind kind, hipStream_t stream)
{
    if (hipMemcpyWithStream_fptr == NULL) {
        hipMemcpyWithStream_fptr = (hipError_t (*) (void*, const void*, size_t, hipMemcpyKind, hipStream_t)) dlsym(get_rocm_lib(), "hipMemcpyWithStream");
    }

    hipError_t result = (*hipMemcpyWithStream_fptr)(dst, src, size, kind, stream);
    //gputrace_event* event;

    gputrace_event event;
    gputrace_event_memcpy memcpy_event;

    event.id = get_curr_event()++;
    event.name = "hipMemcpyWithStream";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_MEMCPY;

    memcpy_event.dst = (uint64_t) dst;
    memcpy_event.src = (uint64_t) src;	
    memcpy_event.size = (uint64_t) size;
    memcpy_event.kind = kind;
    if (memcpy_event.kind == hipMemcpyHostToDevice) {
        memcpy_event.hostdata.resize(size);
        std::memcpy(memcpy_event.hostdata.data(), src, size);
    }

    event.data = std::move(memcpy_event);

    pushback_event(event);

    return result;    
}

hipError_t hipGetDeviceProperties(hipDeviceProp_t* p_prop, int device)
{
    if (hipGetDeviceProperties_fptr == NULL) {
        hipGetDeviceProperties_fptr = (hipError_t (*) (hipDeviceProp_t*, int)) dlsym(get_rocm_lib(), "hipGetDeviceProperties");
    }

    hipError_t result = (*hipGetDeviceProperties_fptr)(p_prop, device);

    gputrace_event event;
    event.id = get_curr_event()++;
    event.name = "hipGetDeviceProperties";
    event.rc = result;
    event.stream = hipStreamDefault;
    event.type = EVENT_DEVICE;

    pushback_event(event);

    return result;
}
/* Unhandled
hipError_t hipLaunchKernelGGL;
hipError_t hipGetDeviceCount;
hipError_t hipSetDevice;
hipError_t hipBindTexture;
hipError_t hipHostMalloc;
hipError_t hipHostFree;
hipError_t hipDeviceSynchronize;
hipError_t hipMemGetInfo;
hipError_t hipHostGetDevicePointer;
*/
};
