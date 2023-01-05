#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "hip/hip_runtime.h"

// header_t, event_t
#include "trace.h"

// ArgInfo, getArgInfo
#include "elf.h"
#include "elfio/elfio.hpp"

#include "replay.h"

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

    hipEventRecord(start, replay_event.stream);
    /* Execute Event */
    if (event.type == EVENT_DEVICE) {
        hipDeviceProp_t my_devProp;
        int device;

        hipGetDeviceProperties(my_devProp, device);
    } else if (event.type == EVENT_MEMCPY) {
        std::printf("\thipMemcpy\n");

        std::printf("event size: %lu\n", event.size);

        uintptr_t dst;
        uintptr_t src;
        size_t size;
        hipMemcpyKind kind; 

        std::fread(&dst, sizeof(void*), 1, data_fp);
        std::fread(&src, sizeof(const void*), 1, data_fp);
        std::fread(&size, sizeof(size_t), 1, data_fp);
        std::fread(&kind, sizeof(hipMemcpyKind), 1, data_fp);

        if (kind == hipMemcpyHostToDevice) {
            auto it = allocations.find(dst);
            if (it == allocations.end()) {
                // Allocation not found on device
            }
            
            void* real_dst = (void*) it->second;
            
            std::printf("\t size: %lu\n", size);
            std::printf("\t reading host buffer of size %zu from trace...\n", size);
            std::vector<std::byte> buffer(size);
            std::fread(buffer.data(), sizeof(std::byte), size, data_fp); 

            std::printf("Fifth value is %f\n", ((float*)buffer.data())[5]);
            std::printf("\t replaying HostToDev MemCpy\n");
            
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
    } else if (event.type == EVENT_LAUNCH) {
        std::printf("\thipLaunchKernel\n");

        std::printf("event size %lu\n", event.size);
        std::printf("Reading from offset %lu\n", event.offset);

        uint64_t function_address;
        dim3 numBlocks;
        dim3 dimBlocks;
        unsigned int sharedMemBytes = 1;
        hipStream_t stream;

        uint64_t name_size;
        uint64_t args_size;

        std::fread(&function_address, sizeof(uint64_t), 1, data_fp);
        std::fread(&numBlocks, sizeof(dim3), 1, data_fp);
        std::fread(&dimBlocks, sizeof(dim3), 1, data_fp);
        std::fread(&sharedMemBytes, sizeof(unsigned int), 1, data_fp);
        std::fread(&stream, sizeof(hipStream_t), 1, data_fp);
        std::fread(&name_size, sizeof(name_size), 1, data_fp);
        std::fread(&args_size, sizeof(args_size), 1, data_fp);

        std::string kernel_name;
        kernel_name.resize(name_size);
        std::fread(kernel_name.data(), sizeof(char), name_size, data_fp);

        std::printf("\tFunction address %lx\n", function_address);
        std::printf("\tnumBlocks %d %d %d\n", numBlocks.x, numBlocks.y, numBlocks.z);
        std::printf("\tsharedMemBytes %d\n", sharedMemBytes);
        std::printf("\tkernel_name %s\n", kernel_name.c_str());
        std::printf("\t args size (in bytes) %d\n", args_size);

        std::printf("\tReading arg data from trace\n");
        std::vector<std::byte> args_bytes;
        args_bytes.resize(args_size);
        std::fread(args_bytes.data(), sizeof(std::byte), args_size, data_fp);

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
            continue;
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
    } else if (event.type == EVENT_MALLOC) {
        std::cout << "\thipMalloc" << std::endl;

        void* ptr = NULL; 
        size_t size;

        std::fread(&ptr, sizeof(void*), 1, data_fp);
        std::fread(&size, sizeof(size_t), 1, data_fp);

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
    } else if (event.type == EVENT_FREE) {
        std::printf("\thipFree\n");

        void* p = NULL;
        std::fread(&p, sizeof(void*), 1, data_fp);
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
    }
    /* ****** */
    hipEventRecord(finish, replay_event.stream);

    timers.push_back(std::make_pair<hipEvent_t, hipEvent_t>(start, finish));
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
    std::FILE* data_fp = trace.data_fp;

    std::vector<event_t>& events = trace.events;
    std::vector<data_t> chunks;
    
    char* line = NULL;
    int curr_event = 0;

    while((line = readline("command > ")) != NULL) {
        if (line[0] == 'i') { // INSPECT
            std::system("llvm-objdump -d './gfx908.code'");
        }

        if (line[0] == 'r') { // RUN
            // Iterate over events 
        
            for (; curr_event < events.size(); curr_event++)
            {
                event_t replay_event = events[curr_event];
                step_event(replay_event);
            }

            char* sql = "DROP TABLE IF EXISTS Results;"
                        "CREATE TABLE Results(EventId INT PRIMARY KEY, Result DOUBLE);"
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

                sqlite3_bind_int(pStmt, 1, event_id);
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
        
        if (line[0] == 'e') { // EDIT
            // Get offset from input
            std::string l(line);
            if (l.find(" ") == std::string::npos) {
                std::printf("e [offset]\nChoose program offset (decimal)\n");
                continue;
            }
            std::string off_str = l.substr(l.find(" "), l.size());
            int16_t offset = std::atoi(off_str.c_str()); 
            
            elfio reader;
            if (!reader.load("./gfx908.code")) {
                std::printf("Failed to load\n");
                continue;
            }
        
            Elf_Half sec_num = reader.sections.size(); 
            for (int i = 0; i < sec_num; i++) {
                section* psec = reader.sections[i];
            
                if (psec) {                    
                    std::string sec_name = psec->get_name();
                    const std::string TEXT = ".text";
                    if(sec_name == TEXT) {
                        std::printf("Found .text\n");                               
                        std::vector<std::byte> code_buff(psec->get_size());
                        std::memcpy(code_buff.data(), psec->get_data(), psec->get_size());
                        
                        int16_t jump = psec->get_size() - offset; 
                        uint32_t new_inst = 0xBF820000; // s_branch [offset]
                        uint32_t ret_inst = 0xBF820000; // s_branch [offset]
                        new_inst |= 3; // (jump / 4) - 1;
                        int16_t ret_jump = -8; //(-jump / 4) - 1;
                        ret_inst |= ((uint16_t)ret_jump);
                        
                        uint32_t old_inst = 0x0; // FIXME ~ Target instr could be 64 bits, ask llvm-mc
                        std::memcpy(&old_inst, &code_buff[offset], sizeof(old_inst));
                        std::memcpy(&code_buff[offset], &new_inst, sizeof(old_inst));
                        
                        // Instrument before old instructions
                        //std::string new_code = "\x00\x00\x80\xBF\x00\x00\x00\x00";
                        //std::printf("new: %s\n", new_code.c_str());
                        //new_code.append((char*) &old_inst, sizeof(old_inst));
                        //new_code.append((char*) &ret_inst, sizeof(ret_inst));
                        
                        psec->set_data((const char*)code_buff.data(), code_buff.size());
                        //psec->append_data(new_code);
                        
                        uint32_t new_code = 0x8004FF80;
                        uint32_t new_code2 = 0x4048F5C3;
                        uint32_t new_code3 = 0x7E0C0204;
                        psec->append_data((char*) &new_code, sizeof(new_code));
                        psec->append_data((char*) &new_code2, sizeof(new_code2));
                        psec->append_data((char*) &new_code3, sizeof(new_code3));
                        psec->append_data((char*) &old_inst, sizeof(old_inst));
                        psec->append_data((char*) &ret_inst, sizeof(ret_inst));
                        
                        reader.save("./gfx908.code");
                    }
                }
            }
            
            
        }
    }
}
