#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "hip/hip_runtime.h"

// header_t, event_t
#include "trace.h"

// ArgInfo, getArgInfo
#include "elf.h"
#include "elfio/elfio.hpp"

#include <readline/readline.h>
#include "sqlite3.h"

sqlite3* g_event_db = NULL;
sqlite3* g_results_db = NULL; 

std::unordered_map<uintptr_t, uintptr_t> allocations;
std::unordered_map<uintptr_t, std::vector<std::byte>> host_buffers;

std::vector<std::pair<hipEvent_t, hipEvent_t>> timers;

uint64_t gPageSize = 4096;

Trace trace;

uintptr_t getReplayPointer(uintptr_t p) 
{
    return allocations[(uintptr_t)p];
}

int step_event(event_t replay_event)
{
    hipEvent_t start;
    hipEvent_t finish;
    hipEventCreate(&start);
    hipEventCreate(&finish);

    hipEventRecord(start, (hipStream_t)replay_event.stream); // FIXME
    /* Execute Event */
    if (replay_event.type == EVENT_DEVICE) {
        hipDeviceProp_t my_devProp;
        int device;

        hipGetDeviceProperties(&my_devProp, device);

    } else if (replay_event.type == EVENT_MEMCPY) {
        uintptr_t dst;
        uintptr_t src;
        size_t size;
        hipMemcpyKind kind;
        std::vector<std::byte> buffer;

        if (kind == hipMemcpyHostToDevice) {
            auto it = allocations.find(dst);
            if (it == allocations.end()) {
                // Allocation not found on device
            }
            
            void* real_dst = (void*) it->second;
            
            buffer.resize(size);
            // Recover host buffer from capture...

            
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

        uint64_t function_address;
        dim3 numBlocks;
        dim3 dimBlocks;
        unsigned int sharedMemBytes = 1;
        hipStream_t stream;

        uint64_t name_size;
        uint64_t args_size;

        std::string kernel_name;
        kernel_name.resize(name_size);

        std::printf("\tFunction address %lx\n", function_address);
        std::printf("\tnumBlocks %d %d %d\n", numBlocks.x, numBlocks.y, numBlocks.z);
        std::printf("\tsharedMemBytes %d\n", sharedMemBytes);
        std::printf("\tkernel_name %s\n", kernel_name.c_str());
        std::printf("\t args size (in bytes) %d\n", args_size);

        std::printf("\tReading arg data from trace\n");
        std::vector<std::byte> args_bytes;
        args_bytes.resize(args_size);

        std::printf("\tGetting arg info from code object\n");
        uint64_t total_argsize = 0;
        
        std::vector<ArgInfo> arg_infos = getArgInfo("gfx908.code");
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
                std::memcpy(&value, args_bytes.data() + offset, size);

                auto ptr_it = allocations.find(value);
                if (ptr_it != allocations.end()) {
                    uintptr_t replay_value = ptr_it->second;
                    std::printf("Actual value %p\n", replay_value);
                    std::memcpy(args_bytes.data() + offset, &replay_value, size); 
                } else {
                    std::printf("Unable to find allocation\n");
                }
            }
        }
        std::printf("\t Load code object\n");
        hipModule_t module;
        hipError_t load_res = hipModuleLoad(&module, "./gfx908.code");

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

        void* config[]{
            HIP_LAUNCH_PARAM_BUFFER_POINTER,
            args_bytes.data(),
            HIP_LAUNCH_PARAM_BUFFER_SIZE,
            &args_size, // address of operator (&) is intended - very weird IMO
            HIP_LAUNCH_PARAM_END};

        hipModuleLaunchKernel(kernel,numBlocks.x, numBlocks.y, numBlocks.z,
                                      dimBlocks.x, dimBlocks.y, dimBlocks.z,
                                      sharedMemBytes, stream, NULL, &config[0]);
    } else if (replay_event.type == EVENT_MALLOC) {
        std::cout << "\thipMalloc" << std::endl;

        void* ptr = NULL; 
        size_t size;

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

        void* p = NULL;
        std::printf("\t\tTrace freed ptr:%p\n", p);

        void* real_ptr = NULL;
        // Get actual pointer from allocations table
        uintptr_t p_as_int = reinterpret_cast<uintptr_t>(p);
        if (allocations.find(p_as_int) != allocations.end()) {
            real_ptr = reinterpret_cast<void*>(allocations[p_as_int]);
        }
        if (real_ptr) {
            hipFree(real_ptr);
        }
    } else {
        // Unhandled event type
        std::printf("Error: Event UNHANDLED\n");
    }
    /* ****** */
    hipEventRecord(finish, (hipStream_t) replay_event.stream);

    timers.push_back(std::pair<hipEvent_t, hipEvent_t>(start, finish));
}

int run_all_events(/* runner monitor */)
{

}

int edit_binary()
{

}


__attribute__((constructor)) void replay_init()
{
    char* db_filename = std::getenv("HIPTRACER_EVENTDB");
    if (db_filename == NULL) {
        db_filename = "tracer-default.sqlite";
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

int sql_callback(void *NotUsed, int argc, char **argv, 
                    char **azColName)
{
    NotUsed = 0;

    for (int i = 0; i < argc; i++) {

        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }

    printf("\n");

    return 0;
}

int main()
{   
    char* line = NULL;
    int curr_event = 0;
    std::vector<event_t> events;

    while((line = readline("command > ")) != NULL) {
        if (line[0] == 'r') { // RUN
            const char* events_sql = "SELECT * FROM Events;";
            sqlite3_exec(g_event_db, events_sql, sql_callback, 0, NULL);

            // Iterate over events 
        
            for (; curr_event < events.size(); curr_event++)
            {
                event_t replay_event = events[curr_event];
                step_event(replay_event);
            }

            char* sql = "DROP TABLE IF EXISTS Results;"
                        "CREATE TABLE Results(EventId INT PRIMARY KEY, Result DOUBLE);";
            sqlite3_exec(g_results_db, sql, 0, 0, NULL);

            sqlite3_stmt* pStmt = NULL;
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
                std::printf("\t Event ID: %d\t Elapsed: %f\t Name: %s\t Stream: %d\t \n", event.id, elapsed, event.name, event.stream);
            }
            
            return 0;
        }

        if (line[0] == 's') { // STEP
            event_t replay_event = events[curr_event];
            step_event(replay_event);
        }
    }
}
