#include <cstdio>
#include <cstddef>
#include <vector>
#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <functional>

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include <zstd.h>

// event_t, header_t, data_t
#include "trace.h"
// ArgInfo, getArgInfo
#include "elf.h"

#include <hip/hip_runtime.h>

#include "subprocess.h"
#include "callbacks.h"

bool REWRITE = true;
bool APICAPTURE = false;
bool DATACAPTURE = false;
bool REPLAY = false;
bool DEBUG = false;
char* EVENTDB = NULL;

int g_curr_event = 0;

header_t hdr;
std::vector<event_t> g_event_list;
std::vector<data_t> g_data_list;
uint64_t g_curr_offset = 0;

sqlite3 *g_event_db = NULL;

void* rocmLibHandle = NULL;

#define CODE_OBJECT_FILENAME "gfx908.code"
#define REWRITE_FILENAME "memtrace-gfx908.code"
#define SUBPROCESS_BUFFER 4096

extern "C" {

__attribute__((constructor)) void hiptracer_init()
{
    // Setup options
    auto as_bool = [](char* env) { return std::string(env) == "true" };
    
    REWRITE = as_bool(std::getenv("HIPTRACER_REWRITE"));
    APICAPTURE = as_bool(std::getenv("HIPTRACER_APICAPTURE"));
    DATACAPTURE = as_bool(std::getenv("HIPTRACER_DATACAPTURE"));
    REPLAY = as_bool(std::getenv("HIPTRACER_REPLAY");
    DEBUG = as_bool(std::getenv("HIPTRACER_DEBUG");
    EVENTDB = std::getenv("HIPTRACER_EVENTDB");
    
    rocmLibHandle = dlopen("/opt/rocm/hip/lib/libamdhip64.so", RTLD_LAZY | RTLD_LOCAL);
    if (rocmLibHandle == NULL) {
        std::printf("Unable to open libamdhip64.so\n");
        std::exit(-1);
    }
    
    if (sqlite3_open(EVENTDB, &g_event_db) != SQLITE_OK) {
        std::printf("Unable to open event database: %s\n", sqlite3_errmsg(g_event_db));
        sqlite3_close(g_event_db);
        std::exit(-1);
    }
    
    char* create_table_sql = "DROP TABLE IF EXISTS Events;"
                         "CREATE TABLE Events(Id INT, Name TEXT, Rc INT, Stream INT, Data BLOB);";
    if(sqlite3_exec(g_event_db, create_table_sql, 0, 0, NULL)) {
        std::printf("Failed to create Events table: %s\n", sqlite3_errmsg(g_event_db));
        std::exit(-1);
    }
}

void list_instrumentation_points(Inst* instructions, unsigned long instructions_len,
                                 Rewrite** rewrites, unsigned long* rewrites_len) {
    for (int i = 0; i < instructions_len; i++) {
        Inst inst = instructions[i];

        std::cout << "NAME: " << inst.inst_name << " OFFSET: " << inst.offset << " SIZE: " << inst.size << std::endl;
    }
}

uint64_t* g_memtrace_buffer = 0x0;
uint64_t* g_memtrace_offset = 0x0;
void modify_binary(std::vector<Rewrite>& rewrites) {
        elfio reader;
        if (!reader.load(CODE_OBJECT_FILENAME)) {
            std::printf("Failed to load binary for rewrite\n");
            return;
        }

        Elf_Half sec_num = reader.sections.size(); 
        for (int i = 0; i < sec_num; i++) {
            section* psec = reader.sections[i];
    
            if (psec) {                    
                std::string sec_name = psec->get_name();
                const std::string TEXT = ".text";
                const std::string NOTES = ".notes";
                if(sec_name == TEXT) {
                    std::printf("Found .text\n");                               
                    std::vector<std::byte> code_buff(psec->get_size());
                    std::memcpy(code_buff.data(), psec->get_data(), psec->get_size());

                    uint64_t offset = (uint64_t) g_memtrace_offset;
                    uint64_t buffer = (uint64_t) g_memtrace_buffer;

                    uint32_t offset_lower = (uint32_t) offset; 
                    uint32_t offset_higher = (uint32_t) (offset >> 32);
                    uint32_t address_lower = ((uint32_t) buffer) & 0xFFFFF000;
                    uint32_t address_higher = (uint32_t) (buffer >> 32);
                    uint32_t address_last = ((uint32_t)buffer) & 0x00000FFF;

                    for (int i = 0; i < rewrites.size(); i++) {
                        uint64_t offset = rewrites[i].offset;
                
                        std::printf("Start: %xd JumpTO: %d\n", offset, psec->get_size());
                        int16_t jump = 0x10F0 - offset - 4; // FIXME

                        uint32_t new_inst = 0xBF820000; // s_branch [offset]
                        uint32_t ret_inst = 0xBF820000; // s_branch [offset]
                        new_inst |= 2;

                        uint32_t nop_inst = 0xBF800000;
                
                        uint64_t old_inst; // FIXME ~ Target instr could be 64 bits, ask llvm-mc
                        std::memcpy(&old_inst, &code_buff[offset], sizeof(old_inst));
                        std::memcpy(&code_buff[offset], &new_inst, sizeof(new_inst));
                        std::memcpy(&code_buff[offset + sizeof(uint32_t)], &nop_inst, sizeof(nop_inst));
                
                        psec->set_data((const char*)code_buff.data(), code_buff.size());
                
                        uint32_t new_code[17] = { 0x7E140281, 0x7E1002FF, offset_higher, 0x7E160280,
                                                  0x7E1202FF, 
                                                  offset_lower,
                                                  0xDD890000, 0x0A000A08, 0xBF8C0070, 0x7E1402FF, address_higher, 0x7E1602FF,
                                                  address_lower, (0xDC740 << 12) | (address_last), 0x0000000A, (uint32_t) old_inst,
                                                  (uint32_t) (old_inst >> 32)};


                        int16_t ret_jump = -20;
                        ret_inst |= ((uint16_t)ret_jump);

                        psec->append_data((char*) &new_code, sizeof(new_code));
                        psec->append_data((char*) &ret_inst, sizeof(ret_inst));
                    }
                }
            }
        }
    reader.save(REWRITE_FILENAME);
}

std::vector<Inst> g_instruction_data;
uint64_t get_num_instructions() {
    std::string command = std::string("/opt/rocm/llvm/bin/llvm-objdump -d --section=.text ") + std::string(CODE_OBJECT_FILENAME);
    //std::cout << command << std::endl;

    std::FILE* fp_output = NULL;
    fp_output = popen(command.c_str(), "r");
    if (fp_output == NULL) {
        std::printf("Run failed\n");
        return 0;
    }
    
    char output[SUBPROCESS_BUFFER];
    uint64_t num_instructions = 0;
    uint64_t base = 0;
    while(std::fread(output, 1, SUBPROCESS_BUFFER, fp_output) > 0) { 
        std::istringstream ss(std::string(output, SUBPROCESS_BUFFER));

        std::string line;
        while(std::getline(ss, line)) {
            if(line[0] == '\t') {
                num_instructions++;

                std::string name;
                std::string word;
                std::stringstream words(line);
                Inst inst;
                words >> name;

                while(words >> word) {
                    if (word == "//") {
                        std::string offset;
                        words >> offset;

                        std::string op_lower;
                        std::string op_higher;

                        words >> op_lower;
                        words >> op_higher;

                        if(op_higher == "") {
                            inst.size = Inst::SIZE32;
                        } else if(name[0] == 'g') {
                            inst.size = Inst::SIZE128;
                        } else {
                            inst.size = Inst::SIZE64;
                        }

                        if (op_lower.size() > 0 && name.size() > 0) { 
                            inst.inst_name = name;
                            inst.offset = std::stoi(offset, 0, 16);
                            if (base == 0) {
                                base = inst.offset;
                            } 
                            inst.offset -= base;
                            
                            g_instruction_data.push_back(inst);
                        }
                        break;
                    }
                }
            }
        }
    }
    return g_instruction_data.size();
}

Inst* get_instruction_data() {

    return g_instruction_data.data();
}

template<typename T>
data_t as_bytes(T a)
{
    data_t data;
    data.bytes.resize(sizeof(a));
    data.size = sizeof(a);

    std::memcpy(data.bytes.data(), (std::byte*) &a, sizeof(a));

    return data;
}

template<typename T, typename... Targs>
data_t as_bytes(T value, Targs... Fargs) // recursive variadic function
{
    data_t data;
    data.bytes.resize(sizeof(value));
    data.size = sizeof(value);

    std::memcpy(data.bytes.data(), (std::byte*) &value, sizeof(value));

    data_t rest_bytes = as_bytes(Fargs...);

    data.bytes.insert(data.bytes.end(), rest_bytes.bytes.begin(), rest_bytes.bytes.end());
    data.size += rest_bytes.size;

    return data;
}

void write_event_list(std::FILE* fp)
{
    hdr.magic = 0xDEADBEEF;

    hdr.num_events = g_event_list.size();
    std::fwrite(&hdr, sizeof(header_t), 1, fp);

    std::fwrite(g_event_list.data(), sizeof(event_t), g_event_list.size(), fp);
}

void write_data(std::FILE* fp)
{
    for (int i = 0; i < g_data_list.size(); i++) {
        size_t size = g_data_list[i].bytes.size();
        
        std::printf("compressing chunk %d of size %d\n", i, size);
        size_t cBuffSize = ZSTD_compressBound(size);
        
        std::vector<char> compressed;
        compressed.reserve(cBuffSize);
        size_t actualSize = ZSTD_compress(compressed.data(), cBuffSize, g_data_list[i].bytes.data(), size, 1);
        
        std::printf("writing compressed chunk %d of size %d \n", i, actualSize);
        std::fwrite(compressed.data(), sizeof(char), actualSize, fp);
    }
}

void flush_files()
{
    std::FILE* event_file = std::fopen("trace.events", "wb");
    std::FILE* data_file = std::fopen("trace.data", "wb");
    write_event_list(event_file);
    write_data(data_file);
}


    const unsigned __hipFatMAGIC2 = 0x48495046; // "HIPF"

#define CLANG_OFFLOAD_BUNDLER_MAGIC "__CLANG_OFFLOAD_BUNDLE__"
#define AMDGCN_AMDHSA_TRIPLE "hip-amdgcn-amd-amdhsa"

    /*
       typedef struct __ClangOffloadBundleDesc {
       uint64_t offset;
       uint64_t size;

       uint64_t tripleSize;
       uint8_t triple[1];
       } __ClangOffloadBundleDesc;

       typedef struct __ClangOffloadBundleHeader {
       const char magic[sizeof(CLANG_OFFLOAD_BUNDLER_MAGIC) - 1];
       uint64_t numBundles;
       __ClangOffloadBundleDesc desc[1];
       } __ClangOffloadBundleHeader;

       typedef struct __CudaFatBinaryWrapper {
       unsigned int                magic;
       unsigned int                version;
       __ClangOffloadBundleHeader* binary;
       void*                       unused;
       } __CudaFatBinaryWrapper;
     */
    
    hipError_t (*hipGetDeviceProperties_fptr)(hipDeviceProp_t*,int) = NULL;
    void* (*hipRegisterFatBinary_fptr)(const void*) = NULL;
    hipError_t (*hipLaunchKernel_fptr)(const void*, dim3, dim3, void**, size_t, hipStream_t) = NULL;
    hipError_t (*hipSetupArgument_fptr)(const void* arg, size_t size, size_t offset) = NULL;

    hipError_t (*hipModuleLaunchKernel_fptr)(hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
                                                unsigned int, hipStream_t, void**, void**) = NULL;
    hipError_t (*hipModuleLoad_fptr)(hipModule_t*, const char*) = NULL;
    hipError_t (*hipModuleGetFunction_fptr)(hipFunction_t*, hipModule_t, const char*) = NULL;
    hipError_t (*hipFree_fptr)(void*) = NULL;
    hipError_t (*hipMalloc_fptr)(void**, size_t) = NULL;
    hipError_t (*hipMemcpy_fptr)(void*, const void*, size_t, hipMemcpyKind) = NULL;

    const char* (*hipKernelNameRefByPtr_fptr)(const void*, hipStream_t) = NULL;
    const char* (*hipKernelNameRef_fptr)(const hipFunction_t) = NULL;

    hipError_t hipFree(void *p)
    {
        g_curr_event++;

        if (hipFree_fptr == NULL) {
            hipFree_fptr = (hipError_t (*) (void*)) dlsym(rocmLibHandle, "hipFree");
        }

        if (DEBUG) {
            printf("[%d] hooked: hipFree\n", g_curr_event);
            printf("[%d] \t p: %p\n", g_curr_event, p);
            printf("[%d] calling: hipFree\n", g_curr_event); 
        }

        event_t e;
        e.id = g_curr_event;
        e.name = __func__;
        e.type = EVENT_FREE;
        e.offset = g_curr_offset;

        if (APICAPTURE) {
            data_t chunk;
            chunk = as_bytes(p);

            e.size = chunk.bytes.size();

            g_event_list.push_back(e);
            g_data_list.push_back(chunk);

            g_curr_offset += chunk.size;
        }

        hipError_t result = (*hipFree_fptr)(p);

        if (DEBUG) {
            printf("[%d] \t result: %d\n", g_curr_event, result);
        }
        return result;
 
    }

    hipError_t hipMalloc(void** p, size_t size)
    { 
        g_curr_event++;

        if (hipMalloc_fptr == NULL) {
            hipMalloc_fptr = (hipError_t (*) (void**, size_t)) dlsym(rocmLibHandle, "hipMalloc");
        }

        if (DEBUG) {
            printf("[%d] hooked: hipMalloc\n", g_curr_event);
            printf("[%d] calling: hipMalloc\n", g_curr_event); 
        }
        hipError_t result = (*hipMalloc_fptr)(p, size);

        if (DEBUG) {
            printf("[%d] \t result: %d\n", g_curr_event, result);
            printf("[%d] hooked: hipMalloc\n", g_curr_event);
            printf("[%d] \t returned ptr: %p\n", g_curr_event, *p);
            printf("[%d] \t size: %lu\n", g_curr_event, size);
        }

        event_t e;
        e.id = g_curr_event;
        e.name = __func__;
        e.type = EVENT_MALLOC;
        e.offset = g_curr_offset;

        if (APICAPTURE) {
            data_t chunk;
            chunk = as_bytes(*p, size);

            e.size = chunk.bytes.size();

            g_event_list.push_back(e);
            g_data_list.push_back(chunk);

            g_curr_offset += chunk.size;
        }

        return result; 
    }

    hipError_t hipMemcpy(void *dst, const void *src, size_t size, hipMemcpyKind kind)
    { 
        g_curr_event++;

        if (hipMemcpy_fptr == NULL) {
            hipMemcpy_fptr = (hipError_t (*) (void*, const void*, size_t, hipMemcpyKind)) dlsym(rocmLibHandle, "hipMemcpy");
        }

        if (DEBUG) {
            printf("[%d] hooked: hipMemcpy\n", g_curr_event);
            printf("[%d] \t dst: %p\n", g_curr_event, dst);
            printf("[%d] \t src: %p\n", g_curr_event, src);
            printf("[%d] \t size: %lu\n", g_curr_event, size);
            printf("[%d] \t kind: %d\n", g_curr_event, (int) kind); // FIXME, gen enum strings
            printf("[%d] calling: hipMempcy\n", g_curr_event); 
        }

        event_t e;
        e.id = g_curr_event;
        e.name = __func__;
        e.type = EVENT_MEMCPY;
        e.offset = g_curr_offset;

        if (APICAPTURE) {
            data_t chunk;
            chunk = as_bytes(dst, src, size, kind);

            e.size = chunk.bytes.size();

            g_event_list.push_back(e);
            g_data_list.push_back(chunk);

            g_curr_offset += chunk.size;

            data_t src_bytes;
            src_bytes.bytes.resize(size);
            std::memcpy(src_bytes.bytes.data(), src, size);
            src_bytes.size = size;

            g_data_list.push_back(src_bytes);
            g_curr_offset += src_bytes.size;

        }
        hipError_t result = (*hipMemcpy_fptr)(dst, src, size, kind);

        if (DEBUG) {
            printf("[%d] \t result: %d\n", g_curr_event, result);
        }
        return result;

        
    }

    hipError_t hipGetDeviceProperties(hipDeviceProp_t* p_prop, int device)
    {
        g_curr_event++;

        if (hipGetDeviceProperties_fptr == NULL) {
            hipGetDeviceProperties_fptr = (hipError_t (*) (hipDeviceProp_t*, int)) dlsym(rocmLibHandle, "hipGetDeviceProperties");
        }

        if (DEBUG) {
            printf("[%d] hooked: hipGetDeviceProperties\n", g_curr_event);
            printf("[%d] \t p_prop: %p\n", g_curr_event, p_prop);
            printf("[%d]\t device: %d\n", g_curr_event, device);
            printf("[%d] calling: hipGetDeviceProperties\n", g_curr_event); 
        }

        event_t e;
        e.id = g_curr_event;
        e.name = __func__;
        e.type = EVENT_DEVICE;
        e.offset = g_curr_offset;

        if (APICAPTURE) {
            data_t chunk;
            chunk = as_bytes(p_prop, device);

            e.size = chunk.bytes.size();

            g_event_list.push_back(e);
            g_data_list.push_back(chunk);

            g_curr_offset += chunk.size;
        }

        hipError_t result = (*hipGetDeviceProperties_fptr)(p_prop, device);

        printf("[%d] \t result: %d\n", g_curr_event, result);
        return result;
    }

    hipError_t hipSetupArgument(const void * arg, size_t size, size_t offset)
    {
         g_curr_event++;

        if (hipSetupArgument_fptr == NULL) {
            hipSetupArgument_fptr = ( hipError_t (*) (const void*, size_t size, size_t offset)) dlsym(rocmLibHandle, "hipSetupArgument");
        } 

        return (hipSetupArgument_fptr)(arg, size, offset);
    }

    hipError_t hipLaunchKernel(const void* function_address,
            dim3 numBlocks,
            dim3 dimBlocks,
            void** args,
            unsigned int sharedMemBytes ,
            hipStream_t stream)
    {
        g_curr_event++;

        if (hipLaunchKernel_fptr == NULL) {
            hipLaunchKernel_fptr = ( hipError_t (*) (const void*, dim3, dim3, void**, size_t, hipStream_t)) dlsym(rocmLibHandle, "hipLaunchKernel");
        } 
        if (hipModuleLoad_fptr == NULL) {
            hipModuleLoad_fptr = ( hipError_t (*) (hipModule_t* , const char* )) dlsym(rocmLibHandle, "hipModuleLoad");
        }

        if (hipKernelNameRefByPtr_fptr == NULL) {
            hipKernelNameRefByPtr_fptr = ( const char* (*) (const void*, hipStream_t)) dlsym(rocmLibHandle, "hipKernelNameRefByPtr");
        }
        if (hipModuleLaunchKernel_fptr == NULL) {
            hipModuleLaunchKernel_fptr = ( hipError_t (*) (hipFunction_t, unsigned int, unsigned int, unsigned int,
                                                              unsigned int, unsigned int, unsigned int, unsigned int,
                                                              hipStream_t, void**, void**)) dlsym(rocmLibHandle, "hipModuleLaunchKernel");
        }
        if (hipModuleGetFunction_fptr == NULL) {
            hipModuleGetFunction_fptr = ( hipError_t (*) (hipFunction_t*, hipModule_t, const char*)) dlsym(rocmLibHandle, "hipModuleGetFunction");
        }

        std::string kernel_name((*hipKernelNameRefByPtr_fptr)(function_address, stream));
        
        std::vector<ArgInfo> arg_infos = getArgInfo(CODE_OBJECT_FILENAME);
        
        uint64_t total_size = 0;
        if (arg_infos.size() > 0) {
            total_size = arg_infos[arg_infos.size() - 1].offset + arg_infos[arg_infos.size() - 1].size;
        }

        std::vector<std::byte> arg_data(total_size);

        //std::printf("num arg infos %d\n", arg_infos.size());
        for(int i = 0; i < arg_infos.size(); i++) {
            //std::printf("offset: %d\n", arg_infos[i].offset);
            //std::printf("adding %d\n", arg_infos[i].size);
            std::memcpy(arg_data.data() + arg_infos[i].offset, args + arg_infos[i].offset, arg_infos[i].size);
        }
        
        event_t e;
        e.id = g_curr_event;
        e.name = __func__;
        e.type = EVENT_LAUNCH;
        e.offset = g_curr_offset;

        if (APICAPTURE) {
            data_t name_chunk;
            name_chunk.bytes.resize(kernel_name.size());
            name_chunk.size = kernel_name.size();
            std::memcpy(name_chunk.bytes.data(), kernel_name.data(), name_chunk.size);

            data_t arg_chunk;
            arg_chunk.size = arg_data.size();
            arg_chunk.bytes = arg_data;
        //std::printf("arg chunk size %d\n", arg_chunk.size);
        //printf("OFFSET %d\n", g_curr_offset);
            data_t chunk;
            struct launch_data {
                const void* function_address;
                dim3 numBlocks;
                dim3 dimBlocks;
                unsigned int sharedMemBytes;
                hipStream_t stream;
                size_t name_size;
                size_t arg_size;
            };
            launch_data ld = launch_data{function_address, numBlocks, dimBlocks, sharedMemBytes, stream, name_chunk.size, arg_chunk.size};
            chunk = as_bytes(ld);

            e.size = chunk.bytes.size() + arg_chunk.bytes.size() + name_chunk.bytes.size();

            g_event_list.push_back(e);
            g_data_list.push_back(chunk);
            g_data_list.push_back(name_chunk);
            g_data_list.push_back(arg_chunk);

            g_curr_offset += e.size; 
        }

        if (DEBUG) {
            printf("[%d] hooked: hipLaunchKernel\n", g_curr_event);
            printf("[%d] \t kernel_name: %s\n", g_curr_event, kernel_name.c_str());
            printf("[%d] \t function_address: %lx\n", g_curr_event, (uint64_t) function_address);
            printf("[%d] \t numBlocks: %d %d %d\n", g_curr_event, numBlocks.x, numBlocks.y, numBlocks.z);
            printf("[%d] \t dimBlocks: %d %d %d\n", g_curr_event, dimBlocks.x, dimBlocks.y, dimBlocks.z);
            printf("[%d] \t sharedMemBytes: %lu\n", g_curr_event, sharedMemBytes);
            printf("[%d] \t stream: %p\n", g_curr_event, stream); 
            printf("[%d] calling: hipLaunchKernel\n", g_curr_event);
        }

        if (REWRITE) {
            uint64_t instructions_len = 0;
            Inst* instructions = nullptr;

            if (g_instruction_data.size() == 0) {
                instructions_len = get_num_instructions();
                instructions = get_instruction_data();
            
                std::printf("NUM INSTRS: %d\n", instructions_len);
                
                std::vector<Rewrite> rewrites;
                
                // MEMTRACE
                size_t current_offset = 0;
                for (int i = 0; i < instructions_len; i++) {
                    Inst& inst = g_instruction_data[i];
                    if (std::string(inst.inst_name).find("global_store") != std::string::npos) {
                        Rewrite rewrite;
                        rewrite.offset = inst.offset;
                        std::printf("Adding offset: %d\n", inst.offset);
                        rewrites.push_back(rewrite);
                        break; // TODO
                    }
                }
                //list_instrumentation_points(instructions, instructions_len, rewrites, &rewrites_len);
                std::printf("NUM REWRITES: %d\n", rewrites.size());
                /*
                for (int i = 0; i < rewrites.size(); i++) {
                    std::printf("REWRITE: %d OFFSET: %d", i, rewrites[i].offset);
                }
                */
                
                if (rewrites.size() != 0 ) {
                    uint64_t* offset;
                    uint64_t* buffer;
                    (*hipMalloc_fptr)((void**)&offset, sizeof(uint64_t));
                    (*hipMalloc_fptr)((void**)&buffer, sizeof(uint64_t) * 1024);
                    g_memtrace_offset = offset;
                    g_memtrace_buffer = buffer;
                    std::printf("g_memtrace_offset: 0x%p", offset);
                    std::printf("g_memtrace_buffer: 0x%p", buffer);

                    modify_binary(rewrites);
                    std::printf("Rewrites made and file saved (hopefully)\n"); 
                }
            }
        }   
 
        hipError_t result;
        if (REWRITE) {
            std::printf("Launching module loaded kernel\n");

            void* config[]{
                        HIP_LAUNCH_PARAM_BUFFER_POINTER,
               			arg_data.data(),
                        HIP_LAUNCH_PARAM_BUFFER_SIZE,
						&total_size,
                        HIP_LAUNCH_PARAM_END};

			hipModule_t rewrite_mod;
			hipFunction_t rewrite_func;
            if ((*hipModuleLoad_fptr)(&rewrite_mod, REWRITE_FILENAME) != hipSuccess) {
				std::printf("Failed to load module\n");
			}
			std::printf("KERNEL: %s\n", kernel_name.c_str());
            if ((*hipModuleGetFunction_fptr)(&rewrite_func, rewrite_mod, kernel_name.c_str()) != hipSuccess) {
				std::printf("Failed to find function\n");
			}
            result = (*hipModuleLaunchKernel_fptr)(rewrite_func, numBlocks.x, numBlocks.y, numBlocks.z,
                                                                           dimBlocks.x, dimBlocks.y, dimBlocks.z,
                                                                           sharedMemBytes, stream, args, NULL);
        } else {
            result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);
        }

        return result;
    }

    hipError_t hipModuleLaunchKernel(hipFunction_t f,
            unsigned int gridDimX,
            unsigned int gridDimY,
            unsigned int gridDimZ,
            unsigned int blockDimX,
            unsigned int blockDimY,
            unsigned int blockDimZ,
            unsigned int sharedMemBytes,
            hipStream_t stream,
            void ** kernelParams,
            void ** extra)
    {
        g_curr_event++;

        if (hipModuleLaunchKernel_fptr == NULL) {
            hipModuleLaunchKernel_fptr = ( hipError_t (*) (hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, hipStream_t, void**, void**)) dlsym(rocmLibHandle, "hipModuleLaunchKernel");
        }
        
        if (hipKernelNameRef_fptr == NULL) {
            hipKernelNameRef_fptr = ( const char* (*) (const hipFunction_t)) dlsym(rocmLibHandle, "hipKernelNameRef");
        }

        std::string kernel_name((*hipKernelNameRef_fptr)(f));
        
        printf("kernel name %s\n", kernel_name.c_str());

        // data_t chunk;
//         chunk = as_bytes(function_address, numBlocks, dimBlocks, sharedMemBytes, stream, name_chunk.size, arg_chunk.size);
//
//         e.size = chunk.bytes.size() + arg_chunk.bytes.size() + name_chunk.bytes.size();
//
//         g_event_list.push_back(e);
//         g_data_list.push_back(chunk);
//         g_data_list.push_back(name_chunk);
//         g_data_list.push_back(arg_chunk);
//
//         g_curr_offset += e.size;
//

        printf("[%d] hooked: hipModuleLaunchKernel\n", g_curr_event);
        //printf("[%d] \t function_address: %p\n", g_curr_event,  function_address);
        printf("[%d] \t numBlocks: %d %d %d\n", g_curr_event, gridDimX, gridDimY, gridDimZ);
        printf("[%d] \t dimBlocks: %d %d %d\n", g_curr_event, blockDimX, blockDimY, blockDimZ); 
        printf("[%d] \t sharedMemBytes: %lu\n", g_curr_event, sharedMemBytes);
        printf("[%d] \t stream: %p\n", g_curr_event, stream); 
        printf("[%d] calling: hipModuleLaunchKernel\n", g_curr_event);

        hipError_t result = (*hipModuleLaunchKernel_fptr)(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                                                                sharedMemBytes, stream, kernelParams, extra);

        printf("[%d] \t result: %d\n", g_curr_event, result);

        return result;

    }

    void* __hipRegisterFatBinary(const void* data)
    {
        if (hipRegisterFatBinary_fptr == NULL) {
            hipRegisterFatBinary_fptr = ( void* (*) (const void*)) dlsym(rocmLibHandle, "__hipRegisterFatBinary");
        }
        //printf("[%d] hooked: __hipRegisterFatBinary(%p)\n", g_curr_event, data);

        const std::byte* wrapper_bytes = reinterpret_cast<const std::byte*>(data);
        struct fb_wrapper_t {
            uint32_t magic;
            uint32_t version;
            void* binary;
            void* unused;
        };
        fb_wrapper_t fbwrapper;
        std::memcpy(&fbwrapper, wrapper_bytes, sizeof(fb_wrapper_t));

        const std::byte* bin_bytes = reinterpret_cast<const std::byte*>(fbwrapper.binary);
        typedef struct {
            const char magic[sizeof(CLANG_OFFLOAD_BUNDLER_MAGIC) - 1] = 
            {'_', '_', 
                'C', 'L', 'A', 'N', 'G', '_', 
                'O', 'F', 'F', 'L', 'O', 'A', 'D', '_',
                'B', 'U', 'N', 'D', 'L', 'E',
                '_', '_'};
            uint64_t numBundles; 
        } fb_header_t;

        fb_header_t fbheader;
        std::memcpy(&fbheader, bin_bytes, sizeof(fb_header_t));

        const std::byte* next = bin_bytes + sizeof(fb_header_t);

        for( int i = 0; i < fbheader.numBundles; i++) {
            struct desc_t {
                uint64_t offset;
                uint64_t size;
                uint64_t tripleSize;
            };

            desc_t desc;
            std::memcpy(&desc, next, sizeof(desc));

            // Determine chunk size 
            size_t chunk_size = sizeof(desc) + desc.tripleSize;
            std::vector<std::byte> bytes(chunk_size);   

            std::memcpy(bytes.data(), next, chunk_size);

            std::string triple;
            triple.reserve(desc.tripleSize + 1);
            std::memcpy(triple.data(), bytes.data() + sizeof(desc), desc.tripleSize);
            triple[desc.tripleSize] = '\0'; // FIXME

			std::string other(triple.c_str());
            if (desc.size > 0 && other.find("gfx908") != std::string::npos) { // FIXME
                // Write the code object to a file
                printf("[%d] writing code object for %s:\n", g_curr_event, triple.c_str());
                std::FILE* code = std::fopen(CODE_OBJECT_FILENAME, "wb");
                std::fwrite(bin_bytes + desc.offset, sizeof(std::byte), desc.size, code);
                std::fclose(code);
            }
            next += chunk_size;
        }

        if (APICAPTURE) {
            std::atexit(flush_files);
        }
        return (*hipRegisterFatBinary_fptr)(data);
    }
};
