#include <cstdio>
#include <iostream>
#include <unordered_map>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#include "hip/hip_runtime.h"
#pragma GCC diagnostic pop

#include "trace.h"

// ArgInfo, getArgInfo
#include "elf.h"
#include "elfio/elfio.hpp"

#include <readline/readline.h>
#include "sqlite3.h"

sqlite3* g_event_db = NULL;
sqlite3* g_results_db = NULL; 
static std::vector<std::string> code_objects;

std::unordered_map<uintptr_t, uintptr_t> allocations;
std::unordered_map<uintptr_t, std::vector<std::byte>> host_buffers;
std::unordered_map<hipStream_t, hipStream_t> stream_mappings;

std::vector<std::pair<hipEvent_t, hipEvent_t>> timers;

std::vector<std::string> event_names = {
    "hipMalloc",
    "hipMemcpy",
    "hipGetDeviceProperties",
    "hipFree",
    "hipLaunchKernel"
};

std::map<std::string, std::vector<ArgInfo>> names_to_infos;

#define HIP_ASSERT(ans) \
  { \
    if (ans == hipSuccess) { \
        {} \
    } else if (ans == hipErrorNotSupported) { \
        std::printf("NOT SUPPORTED AT %s:%d\n", __FILE__, __LINE__); \
    } else if (ans == hipErrorInvalidValue) {\
        std::printf("INVALID VALUE AT %s:%d\n", __FILE__, __LINE__); \
    } else if (ans != hipSuccess) { \
        std::printf("HIP ERROR AT %s:%d\n", __FILE__, __LINE__); \
    } else { \
        std::printf("UNKNOWN HIP ERROR AT %s:%d\n", __FILE__, __LINE__); \
    }\
  }

int get_free(int event_id, uintptr_t& p)
{
    const char* sql = "SELECT Ptr FROM EventFree WHERE Id = ?;";

    sqlite3_stmt* pStmt;
    sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);
    sqlite3_bind_int(pStmt, 1, event_id);

    int rc = sqlite3_step(pStmt);
    if (rc != SQLITE_ROW) {
        std::printf("Failed to get launch %s\n", sqlite3_errmsg(g_event_db));
    }
 
    p = sqlite3_column_int64(pStmt, 0);

    return rc;
}

int get_memcpy(int event_id, uintptr_t& dst, uintptr_t& src, size_t& size, hipMemcpyKind& kind, std::vector<std::byte>& buffer)
{
    const char* sql = "SELECT Dst, Src, Size, Kind, HostData FROM EventMemcpy WHERE Id = ?;";

    sqlite3_stmt* pStmt;
    sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);
    sqlite3_bind_int(pStmt, 1, event_id);

    int rc = sqlite3_step(pStmt);
    if (rc != SQLITE_ROW) {
        std::printf("Failed to get launch %s\n", sqlite3_errmsg(g_event_db));
    }
 
    dst = sqlite3_column_int64(pStmt, 0);
    src = sqlite3_column_int64(pStmt, 1);
    size = sqlite3_column_int(pStmt, 2);
    kind = (hipMemcpyKind) sqlite3_column_int(pStmt, 3);


    std::string filename = "hostdata-" + std::to_string(event_id) + ".bin";
    if (kind == hipMemcpyHostToDevice) {
        buffer.resize(size);

        std::FILE *fp = std::fopen(filename.c_str(), "rb");
        std::fread(buffer.data(), size, 1, fp);
        std::fclose(fp);
    }

    return rc;
}

int get_malloc(int event_id, uintptr_t& p, size_t& size)
{
    const char* sql = "SELECT Ptr, Size FROM EventMalloc WHERE Id = ?;";

    sqlite3_stmt* pStmt;
    sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);
    sqlite3_bind_int(pStmt, 1, event_id);

    int rc = sqlite3_step(pStmt);
    if (rc != SQLITE_ROW) {
        std::printf("Failed to get launch %s\n", sqlite3_errmsg(g_event_db));
    }
 
    p = sqlite3_column_int64(pStmt, 0);
    size = sqlite3_column_int(pStmt, 1);

    return rc;

}

int get_launch(int event_id, hipStream_t& stream, std::string& kernel_name, dim3& numBlocks, dim3& dimBlocks, unsigned int& sharedMemBytes, std::vector<std::byte>& arg_data)
{
    const char* sql = "SELECT Stream, KernelName, NumX, NumY, NumZ, DimX, DimY, DimZ, SharedMem, ArgData FROM EventLaunch WHERE Id = ?;";

    sqlite3_stmt* pStmt;
    sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);
    sqlite3_bind_int(pStmt, 1, event_id);

    int rc = sqlite3_step(pStmt);
    if (rc != SQLITE_ROW) {
        std::printf("Failed to get launch %s\n", sqlite3_errmsg(g_event_db));
    }
 
    stream = (hipStream_t) sqlite3_column_int(pStmt, 0);

    std::string name((char*)sqlite3_column_text(pStmt, 1));
    kernel_name = name;

    std::printf("Kernel name %s\n", name.c_str());

    numBlocks.x = sqlite3_column_int(pStmt, 2);
    numBlocks.y = sqlite3_column_int(pStmt, 3);
    numBlocks.z = sqlite3_column_int(pStmt, 4);
    dimBlocks.x = sqlite3_column_int(pStmt, 5);
    dimBlocks.y = sqlite3_column_int(pStmt, 6);
    dimBlocks.z = sqlite3_column_int(pStmt, 7);

    sharedMemBytes = sqlite3_column_int(pStmt, 8);

    size_t args_size = sqlite3_column_bytes(pStmt, 9);
    arg_data.resize(args_size);

    const void * blob = sqlite3_column_blob(pStmt, 9);
    std::memcpy(arg_data.data(), blob, args_size);

    std::printf("Got blob\n");
    return rc;
}

int step_event(gputrace_event replay_event)
{
    std::printf("%d\n", code_objects.size());
    std::printf("%s\n", replay_event.name);
    hipEvent_t start;
    hipEvent_t finish;
    HIP_ASSERT(hipEventCreate(&start));
    HIP_ASSERT(hipEventCreate(&finish));

    HIP_ASSERT(hipEventRecord(start, hipStreamDefault));
    std::string name = replay_event.name;
    /* Execute Event */
    if (replay_event.type == EVENT_DEVICE && name == "hipGetDeviceProperties") {
        hipDeviceProp_t my_devProp;
        int device = 0; // FIXME

        HIP_ASSERT(hipGetDeviceProperties(&my_devProp, device));
    } else if (name == "hipStreamCreate") {
        hipStream_t stream;
        HIP_ASSERT(hipStreamCreate(&stream));

        stream_mappings[replay_event.stream] = stream;
    } else if (name == "hipStreamSynchronize") {
        hipStream_t stream = hipStreamDefault;
        if (replay_event.stream != hipStreamDefault) {
            stream = stream_mappings[replay_event.stream];
        }

        HIP_ASSERT(hipStreamSynchronize(stream));
    } else if (replay_event.type == EVENT_MEMCPY) {
        std::printf("hipMemcpy\n");

        uintptr_t dst;
        uintptr_t src;
        size_t size;
        hipMemcpyKind kind;
        std::vector<std::byte> buffer;

        get_memcpy(replay_event.id, dst, src, size, kind, buffer);

        if (kind == hipMemcpyHostToDevice) {
            auto it = allocations.find(dst);
            if (it == allocations.end()) {
                // Allocation not found on device
            }

            void* real_dst = (void*) it->second;

            if (name == "hipMemcpyWithStream") {
                HIP_ASSERT(hipMemcpyWithStream(real_dst, buffer.data(), size, kind, replay_event.stream));
            } else {
                HIP_ASSERT(hipMemcpy(real_dst, buffer.data(), size, kind));
            }
        } else if (kind == hipMemcpyDeviceToHost) {
            auto it = host_buffers.find(dst);
            if (it == host_buffers.end()) {
                host_buffers[dst] = std::vector<std::byte>(size);
            }
            it = host_buffers.find(dst);
            
            std::vector<std::byte>& buff = it->second;
            if(buff.size() < size) { buff.resize(size); }
            
            if (name == "hipMemcpyWithStream") {
                HIP_ASSERT(hipMemcpyWithStream(buff.data(), (void*) allocations[src], size, kind, replay_event.stream));
            } else {
                HIP_ASSERT(hipMemcpy(buff.data(), (void*) allocations[src], size, kind));
            }
            std::printf("EX IS %f\n", ((float*)buff.data())[12]);
        }
    } else if (replay_event.type == EVENT_LAUNCH) {
        std::printf("\thipLaunchKernel\n");

        std::string kernel_name;
        dim3 numBlocks;
        dim3 dimBlocks;
        unsigned int sharedMemBytes = 1;
        hipStream_t stream;
        std::vector<std::byte> args;

        get_launch(replay_event.id, stream, kernel_name, numBlocks, dimBlocks, sharedMemBytes, args);

        uint64_t total_argsize = 0;

        std::map<std::string, std::vector<ArgInfo>> names_to_infos;
        for (int i = 0; i < code_objects.size(); i++) {
            oldGetArgInfo(code_objects[i].c_str(), names_to_infos);
        }
        std::vector<ArgInfo> arg_infos = names_to_infos[kernel_name];

        std::printf("ARG INFO SIZE %d\n", arg_infos.size());
        for (int i = 0; i < arg_infos.size(); i++) {
            total_argsize += arg_infos[i].size;

            if (arg_infos[i].value_kind == "global_buffer") {
                // Check for pointers in allocation table
                uintptr_t value;
                auto size = arg_infos[i].size;
                auto offset = arg_infos[i].offset;
                std::printf("A?\n");
                std::printf("ARGS SIZE %d\n", args.size());
                std::memcpy(&value, args.data() + offset, size);
                std::printf("Found %px\n", value);

                auto ptr_it = allocations.find(value);
                if (ptr_it != allocations.end()) {
                    uintptr_t replay_value = ptr_it->second;
                    std::printf("B?\n");
                    std::memcpy(args.data() + offset, &replay_value, size);
                    std::printf("C?\n");
                } else {
                    std::printf("Unable to find allocation of global_buffer argument\n");
                }
            }
        }

        hipModule_t module;
        hipFunction_t kernel;
        bool found = false;
        for (int i = 0; i < code_objects.size(); i++) {
            std::printf("Checking %s\n", code_objects[i].c_str());
            hipError_t load_res = hipModuleLoad(&module, code_objects[i].c_str());
            if (load_res == hipSuccess) {
                std::printf("\t LOADed CODE OBJECT\n");
            }

            hipError_t func_res = hipModuleGetFunction(&kernel, module, kernel_name.c_str());

            if (func_res == hipSuccess) {
                std::printf("\t SUCCESS found kernel: %s\n", kernel_name.c_str());
                found = true;
                break;
            }
        }
        if (!found) {
            std::printf("Unable to find kernel %s in any code object\n", kernel_name.c_str());
            return -1;
        }

        size_t args_size = args.size();
        void* config[]{
            HIP_LAUNCH_PARAM_BUFFER_POINTER,
            args.data(),
            HIP_LAUNCH_PARAM_BUFFER_SIZE,
            &args_size, // address of operator (&) is intended
            HIP_LAUNCH_PARAM_END};

        hipStream_t new_stream = hipStreamDefault;
        if (stream != hipStreamDefault) {
            new_stream = stream_mappings[stream];
        }

        HIP_ASSERT(hipModuleLaunchKernel(kernel,numBlocks.x, numBlocks.y, numBlocks.z,
                                      dimBlocks.x, dimBlocks.y, dimBlocks.z,
                                      sharedMemBytes, new_stream, NULL, &config[0]));
    } else if (replay_event.type == EVENT_MALLOC) {
        uintptr_t ptr = NULL; 
        size_t size;

        int rc = get_malloc(replay_event.id, ptr, size);

        uintptr_t ret = NULL;

        HIP_ASSERT(hipMalloc((void**)&ret, size));

        if (!ret) {
            return -1;
        } else {
            std::printf("Allocation succeeded at location %px\n", ret);
        }

        allocations.insert({(uintptr_t)ptr, (uintptr_t)ret});
    } else if (replay_event.type == EVENT_FREE) {
        uintptr_t p = NULL;
        int rc = get_free(replay_event.id, p);

        uintptr_t real_ptr = NULL;
        // Get actual pointer from allocations table

        if (allocations.find(p) != allocations.end()) {
            real_ptr = allocations[p];
        }
        if (real_ptr) {
            HIP_ASSERT(hipFree((void*)real_ptr));
        }
    } else {
        // Unhandled event type
        std::printf("Error: Event UNHANDLED %s\n", replay_event.name);
    }
    HIP_ASSERT(hipEventRecord(finish, hipStreamDefault));

    timers.push_back(std::pair<hipEvent_t, hipEvent_t>(start, finish));
    return 0;
}

int main()
{   
    const char* db_filename = std::getenv("HIPTRACER_EVENTDB");
    if (db_filename == NULL) {
        db_filename = "tracer-default.db";
    }
 
    std::printf("db: %s\n", db_filename);
    if (sqlite3_open(db_filename, &g_event_db) != SQLITE_OK) {
        std::exit(-1);
    }
    if (sqlite3_open(":memory:", &g_results_db) != SQLITE_OK) {
        std::exit(-1);
    }

    const char* code_sql = "SELECT Path FROM Code;";
    sqlite3_stmt* pStmt;
    int rc = sqlite3_prepare_v2(g_event_db, code_sql, -1, &pStmt, 0);
    if (rc != SQLITE_OK) {
        sqlite3_errmsg(g_event_db);
    }

    //sqlite3_bind_text(pStmt, 1, "gfx908", -1, SQLITE_STATIC);

    rc = sqlite3_step(pStmt);
    if (rc != SQLITE_ROW) {
        sqlite3_errmsg(g_event_db);
    }
    while(rc == SQLITE_ROW) {
        std::string name((char*)sqlite3_column_text(pStmt, 0));

        code_objects.push_back(name);
        rc = sqlite3_step(pStmt);
    }
    sqlite3_finalize(pStmt);

    std::printf("Code objects size %d\n", code_objects.size());

    char* line = NULL;
    int curr_event = 0;
    std::vector<gputrace_event> events;

    while((line = readline("command > ")) != NULL) {
        if (line[0] == 'r') { // RUN
            const char* events_sql = "SELECT Id, Name, EventType FROM Events ORDER BY Id ASC;";
            sqlite3_stmt* pStmt;
            sqlite3_prepare_v2(g_event_db, events_sql, -1, &pStmt, 0);

            while (sqlite3_step(pStmt) == SQLITE_ROW) {
                gputrace_event event;
                event.id = sqlite3_column_int(pStmt, 0);
                event.name = NULL;
                std::string name((char*)sqlite3_column_text(pStmt, 1));
                for(int i = 0; i < event_names.size(); i++) {
                    if (event_names[i] == name) {
                        event.name = event_names[i].c_str();
                    }
                }
                if (event.name == NULL) {
                    event_names.push_back(name);
                    event.name = event_names[event_names.size() - 1].c_str();
                }

                event.type = (HIP_EVENT) sqlite3_column_int(pStmt, 2);

                events.push_back(event);
            }

            sqlite3_finalize(pStmt);

            std::printf("NUM EVENTS %d\n", events.size());
            // Iterate over events
            for (; curr_event < events.size(); curr_event++)
            {
                gputrace_event replay_event = events[curr_event];
                step_event(replay_event);
            }

            const char* sql = "DROP TABLE IF EXISTS Results;"
                        "CREATE TABLE Results(EventId INT PRIMARY KEY, Result DOUBLE);";
            sqlite3_exec(g_results_db, sql, 0, 0, NULL);

            pStmt = NULL;
            for (int i = 0; i < timers.size(); i++) {
                float elapsed = 0;
                gputrace_event event = events[i];
                auto timer = timers[i];
                HIP_ASSERT(hipEventSynchronize(timer.second));
                HIP_ASSERT(hipEventElapsedTime(&elapsed, timer.first, timer.second));

                sql = "INSERT INTO Results(EventId, Result) VALUES(?, ?);";
                sqlite3_prepare(g_results_db, sql, -1, &pStmt, 0);

                sqlite3_bind_int(pStmt, 1, events[i].id);
                sqlite3_bind_double(pStmt, 2, (double) elapsed);

                sqlite3_step(pStmt);
                sqlite3_finalize(pStmt); // FIXME

                std::printf("\t Event ID: %d\t Elapsed: %f\t Name: %s\t \n", event.id, elapsed, event.name);
            }
        }

        if (line[0] == 'n') { // STEP
            gputrace_event replay_event = events[curr_event];
            step_event(replay_event);
        }

        if (line[0] == 's') { // 
            // for allocation in allocations
            // do copy allocation to disk
            // save stream mappings
            std::printf("Saving device state at event %d\n", events[curr_event].id);
        }

        if (line[0] == 'l') {
            std::printf("Loading device state\n");
        }
    }


    const char* sql = "DROP TABLE IF EXISTS Results;";
    sqlite3_exec(g_results_db, sql, 0, 0, NULL);

    sqlite3_close(g_event_db);
    sqlite3_close(g_results_db);

    return 0;
}
