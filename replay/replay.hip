#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "hip/hip_runtime.h"

#include "trace.h"

// ArgInfo, getArgInfo
#include "elf.h"
#include "elfio/elfio.hpp"

#include <readline/readline.h>
#include "sqlite3.h"

sqlite3* g_event_db = NULL;
sqlite3* g_results_db = NULL; 
std::string g_codeobj_filename = "./hipv4-amdgcn-amd-amdhsa--gfx908.code";

std::unordered_map<uintptr_t, uintptr_t> allocations;
std::unordered_map<uintptr_t, std::vector<std::byte>> host_buffers;

std::vector<std::pair<hipEvent_t, hipEvent_t>> timers;

uintptr_t getReplayPointer(uintptr_t p) 
{
    return allocations[(uintptr_t)p];
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
    if (kind == hipMemcpyHostToDevice) {
        const void * blob = sqlite3_column_blob(pStmt, 4);
        buffer.resize(size);
        std::memcpy(buffer.data(), blob, size);
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
    std::printf("SIZE: %d\n", args_size);
    arg_data.resize(args_size);

    const void * blob = sqlite3_column_blob(pStmt, 9);
    std::memcpy(arg_data.data(), blob, args_size);

    return rc;
}

int step_event(gputrace_event replay_event)
{
    std::printf("%s\n", replay_event.name.c_str());
    hipEvent_t start;
    hipEvent_t finish;
    hipEventCreate(&start);
    hipEventCreate(&finish);

    hipEventRecord(start, hipStreamDefault);
    /* Execute Event */
    if (replay_event.type == EVENT_DEVICE) {
        std::printf("hipGetDeviceProperties\n");
        hipDeviceProp_t my_devProp;
        int device = 0; // FIXME

        hipGetDeviceProperties(&my_devProp, device);
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

            hipMemcpy(real_dst, buffer.data(), size, kind);
        } else if (kind == hipMemcpyDeviceToHost) {
            auto it = host_buffers.find(dst);
            if (it == host_buffers.end()) {
                host_buffers[dst] = std::vector<std::byte>(size);
            }
            it = host_buffers.find(dst);
            
            std::vector<std::byte>& buff = it->second;
            if(buff.size() < size) { buff.resize(size); }
            
            hipMemcpy(buff.data(), (void*) getReplayPointer(src), size, kind);
        }
    } else if (replay_event.type == EVENT_LAUNCH) {
        std::printf("\thipLaunchKernel\n");

        std::printf("event size %lu\n", replay_event.size);
        std::printf("Reading from offset %lu\n", replay_event.offset);

        std::string kernel_name;
        dim3 numBlocks;
        dim3 dimBlocks;
        unsigned int sharedMemBytes = 1;
        hipStream_t stream;
        std::vector<std::byte> args;

        get_launch(replay_event.id, stream, kernel_name, numBlocks, dimBlocks, sharedMemBytes, args);

        std::printf("\tnumBlocks %d %d %d\n", numBlocks.x, numBlocks.y, numBlocks.z);
        std::printf("\tsharedMemBytes %d\n", sharedMemBytes);
        std::printf("\tkernel_name %s\n", kernel_name.c_str());
        std::printf("\t args size (in bytes) %d\n", args.size());

        std::printf("\tGetting arg info from code object\n");
        uint64_t total_argsize = 0;
        
        std::vector<ArgInfo> arg_infos = getArgInfo(g_codeobj_filename.c_str());
        std::printf("arg_infos size %d\n", arg_infos.size());

        std::printf("\tChecking args for allocations\n");
        for (int i = 0; i < arg_infos.size(); i++) {
            total_argsize += arg_infos[i].size;
            std::printf("value kind %s\n", arg_infos[i].value_kind.c_str());
            std::printf("Access %s\n", arg_infos[i].access.c_str());

            if (arg_infos[i].value_kind == "global_buffer") {
                // Check for pointers in allocation table
                uintptr_t value;
                auto size = arg_infos[i].size;
                auto offset = arg_infos[i].offset;
                std::memcpy(&value, args.data() + offset, size);

                std::printf("Looking for %d\n", value);

                auto ptr_it = allocations.find(value);
                if (ptr_it != allocations.end()) {
                    uintptr_t replay_value = ptr_it->second;
                    std::printf("Actual value %p\n", replay_value);
                    std::memcpy(args.data() + offset, &replay_value, size); 
                } else {
                    std::printf("Unable to find allocation\n");
                }
            }
        }
        std::printf("\t Load code object\n");
        hipModule_t module;
        hipError_t load_res = hipModuleLoad(&module, g_codeobj_filename.c_str());

        if (load_res != hipSuccess) {
            std::printf("\t FAILED to LOAD CODE OBJECT\n");
        }

        hipFunction_t kernel;
        hipError_t func_res = hipModuleGetFunction(&kernel, module, kernel_name.c_str());

        if (func_res == hipSuccess) {
            std::printf("\t SUCCESS found kernel: %s\n", kernel_name.c_str());
        } else {
            // FIXME
            std::printf("\t ERROR unable to find %s\n", kernel_name.c_str());
            return -1;
        }

        std::printf("\t Launching...\n");

        size_t args_size = args.size();
        void* config[]{
            HIP_LAUNCH_PARAM_BUFFER_POINTER,
            args.data(),
            HIP_LAUNCH_PARAM_BUFFER_SIZE,
            &args_size, // address of operator (&) is intended - very weird IMO
            HIP_LAUNCH_PARAM_END};

        hipModuleLaunchKernel(kernel,numBlocks.x, numBlocks.y, numBlocks.z,
                                      dimBlocks.x, dimBlocks.y, dimBlocks.z,
                                      sharedMemBytes, stream, NULL, &config[0]);
    } else if (replay_event.type == EVENT_MALLOC) {
        std::cout << "\thipMalloc" << std::endl;

        uintptr_t ptr = NULL; 
        size_t size;

        get_malloc(replay_event.id, ptr, size);

        std::printf("\tRead pointer %p\n", ptr);
        std::printf("\tRead size %lu\n", size); 

        void* ret = NULL;
        int status = hipMalloc(&ret, size);

        if (!ret) {
            std::printf("Failed to provide traced allocation (error: %d)\n", status);
            return -1;
        } else {
            std::printf("Allocation succeeded at location %p\n", ptr);
        } 

        uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t ret_as_int = reinterpret_cast<uintptr_t>(ret);

        std::printf("ptr %p\n", ptr);
        std::printf("ret %p\n", ret);

        allocations.insert({ptr_as_int, ret_as_int});
    } else if (replay_event.type == EVENT_FREE) {
        std::printf("\thipFree\n");

        uintptr_t p = NULL;
        get_free(replay_event.id, p);
        std::printf("\t\tTrace freed ptr:%p\n", p); 

        void* real_ptr = NULL;
        // Get actual pointer from allocations table
        if (allocations.find(p) != allocations.end()) {
            real_ptr = reinterpret_cast<void*>(allocations[p]);
        }
        if (real_ptr) {
            hipFree(real_ptr);
        }
    } else {
        // Unhandled event type
        std::printf("Error: Event UNHANDLED\n");
    }
    /* ****** */
    //hipEventRecord(finish, (hipStream_t) replay_event.stream);

    //timers.push_back(std::pair<hipEvent_t, hipEvent_t>(start, finish));
    return 0;
}

__attribute__((constructor)) void replay_init()
{
    char* db_filename = std::getenv("HIPTRACER_EVENTDB");
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
}

__attribute__((destructor)) void replay_destroy()
{
    sqlite3_close(g_event_db);
    sqlite3_close(g_results_db);
}

int main()
{   
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
                event.name = std::string((char*)sqlite3_column_text(pStmt, 1));
                event.type = (HIP_EVENT) sqlite3_column_int(pStmt, 2);

                events.push_back(event);
            }

            sqlite3_finalize(pStmt);

            // Iterate over events
            for (; curr_event < events.size(); curr_event++)
            {
                event_t replay_event = events[curr_event];
                step_event(replay_event);
            }

            char* sql = "DROP TABLE IF EXISTS Results;"
                        "CREATE TABLE Results(EventId INT PRIMARY KEY, Result DOUBLE);";
            sqlite3_exec(g_results_db, sql, 0, 0, NULL);

            pStmt = NULL;
            for (int i = 0; i < timers.size(); i++) {
                float elapsed = 0;
                event_t event = events[i];
                auto timer = timers[i];
                hipEventSynchronize(timer.second);
                hipEventElapsedTime(&elapsed, timer.first, timer.second);

                sql = "INSERT INTO Results(EventId, Result) VALUES(?, ?);";
                sqlite3_prepare(g_results_db, sql, -1, &pStmt, 0);

                sqlite3_bind_int(pStmt, 1, events[i].id);
                sqlite3_bind_double(pStmt, 2, (double) elapsed);

                sqlite3_step(pStmt);
                sqlite3_finalize(pStmt); // FIXME
                std::printf("\t Event ID: %d\t Elapsed: %f\t Name: %s\t Stream: %d\t \n", event.id, elapsed, event.name.c_str(), event.stream);
            }
            
            return 0;
        }

        if (line[0] == 's') { // STEP
            gputrace_event replay_event = events[curr_event];
            step_event(replay_event);
        }
    }
}
