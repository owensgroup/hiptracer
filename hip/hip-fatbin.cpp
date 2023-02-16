#include <dlfcn.h>
#include <cstdio>
#include <cstddef>
#include <condition_variable>
#include <string>

#include "elf.h"
#include "trace.h"

#include "sqlite3.h"

#include "atomic_queue/atomic_queue.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#define MAX_ELEMS 5124
extern atomic_queue::AtomicQueue2<gputrace_event, sizeof(gputrace_event) * MAX_ELEMS> events_queue;
extern std::atomic<int> g_curr_event;
extern sqlite3 *g_event_db;
extern sqlite3 *g_arginfo_db;
extern void* rocmLibHandle;
extern std::condition_variable events_available;

extern "C" {
void*       (*hipRegisterFatBinary_fptr)(const void*) = NULL;
hipError_t  (*hipModuleLoad_fptr)(hipModule_t*, const char*) = NULL;
hipError_t  (*hipModuleGetFunction_fptr)(hipFunction_t*, hipModule_t, const char*) = NULL;
const char* (*hipKernelNameRefByPtr_fptr)(const void*, hipStream_t) = NULL;
const char* (*hipKernelNameRef_fptr)(const hipFunction_t) = NULL;
hipError_t  (*hipModuleLaunchKernel_fptr)(hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, hipStream_t, void**, void**) = NULL;
hipError_t  (*hipLaunchKernel_fptr)(const void*, dim3, dim3, void**, size_t, hipStream_t) = NULL;

const unsigned __hipFatMAGIC2 = 0x48495046; // "HIPF"
#define CLANG_OFFLOAD_BUNDLER_MAGIC "__CLANG_OFFLOAD_BUNDLE__"
#define AMDGCN_AMDHSA_TRIPLE "hip-amdgcn-amd-amdhsa"

//FIXME
void pushback_event(gputrace_event event)
{
    if (events_queue.was_full()) {
        std::printf("Unable to add event, event queue full\nUse a larger buffer size or allocate more threads for event insertion\n");
        std::exit(-1);
    } else {
        events_queue.push(event);
        events_available.notify_one();
    }
}



void* __hipRegisterFatBinary(const void* data)
{
    g_curr_event++;

    if (hipRegisterFatBinary_fptr == NULL) {
        hipRegisterFatBinary_fptr = ( void* (*) (const void*)) dlsym(rocmLibHandle, "__hipRegisterFatBinary");
    }

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
            { '_', '_', 
              'C', 'L', 'A', 'N', 'G', '_', 
              'O', 'F', 'F', 'L', 'O', 'A', 'D', '_',
              'B', 'U', 'N', 'D', 'L', 'E',
              '_', '_' };
        uint64_t numBundles; 
    } fb_header_t;

    fb_header_t fbheader;
    std::memcpy(&fbheader, bin_bytes, sizeof(fb_header_t));

    const std::byte* next = bin_bytes + sizeof(fb_header_t);

    for(int i = 0; i < fbheader.numBundles; i++) {
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
        triple[desc.tripleSize] = '\0';

        std::string s(triple.c_str());
        if (s.find("host") != std::string::npos) {
            next += chunk_size;
            continue;
        }

        std::string filename = std::string("./code/") + s + "-" + std::to_string(g_curr_event) + "-" + std::to_string(i) + ".code";
        //std::printf("Getting %s\n", filename.c_str());

        std::string image{reinterpret_cast<const char*>(
          reinterpret_cast<uintptr_t>(bin_bytes) + desc.offset), desc.size};

        std::istringstream is(image);
        sqlite3_exec(g_arginfo_db, "BEGIN TRANSACTION", NULL, NULL, NULL);
        getArgInfo(is, g_arginfo_db, g_curr_event++);
        sqlite3_exec(g_arginfo_db, "END TRANSACTION", NULL, NULL, NULL);

        gputrace_event event;
        gputrace_event_code code_event;

        code_event.filename = filename;
        code_event.code = image;

        event.id = g_curr_event;
        event.rc = hipSuccess;
        event.stream = hipStreamDefault;
        event.type = EVENT_CODE;

        event.data = std::move(code_event);

        pushback_event(event);

        next += chunk_size;
    }

    return (*hipRegisterFatBinary_fptr)(data);
}

hipError_t hipLaunchKernel(const void* function_address,
        dim3 numBlocks,
        dim3 dimBlocks,
        void** args,
        size_t sharedMemBytes ,
        hipStream_t stream)
{
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

    std::string kernel_name = std::string((*hipKernelNameRefByPtr_fptr)(function_address, stream));

    uint64_t total_size = 0;
    if (kernel_arg_sizes.find(kernel_name) == kernel_arg_sizes.end()) {
        sqlite3_stmt* pStmt = NULL;
        const char* sql = "SELECT Size FROM ArgInfo WHERE KernelName = ?;";
        if (sqlite3_prepare_v2(g_arginfo_db, sql, -1, &pStmt, 0) != SQLITE_OK) {
            std::printf("SQL ERROR %s\n", sqlite3_errmsg(g_arginfo_db));
        }
        sqlite3_bind_text(pStmt, 1, kernel_name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(pStmt) == SQLITE_ROW) {
            total_size = sqlite3_column_int(pStmt, 0);
            kernel_arg_sizes[kernel_name] = total_size;
            std::printf("Added new kernel %s of size %d\n", kernel_name.c_str(), total_size);
        }
    }    
    total_size = kernel_arg_sizes[kernel_name];

    //std::printf("Copying args of size %d\n", total_size);
    std::vector<std::byte> arg_data(total_size);
    std::memcpy(arg_data.data(), args, total_size);

    hipError_t result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);

    gputrace_event event;
    gputrace_event_launch launch_event;

    event.id = g_curr_event++;
    event.name = "hipLaunchKernel";
    event.rc = result;
    event.stream = stream;
    event.type = EVENT_LAUNCH;

    launch_event.kernel_name = kernel_name;
    launch_event.num_blocks = numBlocks;
    launch_event.dim_blocks = dimBlocks;
    launch_event.shared_mem_bytes = sharedMemBytes;
    launch_event.argdata = arg_data;

    event.data = std::move(launch_event);

    pushback_event(event);

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
    if (hipModuleLaunchKernel_fptr == NULL) {
        hipModuleLaunchKernel_fptr = ( hipError_t (*) (hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, hipStream_t, void**, void**)) dlsym(rocmLibHandle, "hipModuleLaunchKernel");
    }
    
    if (hipKernelNameRef_fptr == NULL) {
        hipKernelNameRef_fptr = ( const char* (*) (const hipFunction_t)) dlsym(rocmLibHandle, "hipKernelNameRef");
    }  

    const char* kernel_name = (*hipKernelNameRef_fptr)(f);

    //std::vector<ArgInfo> arg_infos = names_to_info[kernel_name];

    uint64_t total_size = 0;
    /*
    if (arg_infos.size() > 0) {
        total_size = arg_infos[arg_infos.size() - 1].offset + arg_infos[arg_infos.size() - 1].size;
    }
    */

    std::vector<std::byte> arg_data(total_size);

    /*
    for(int i = 0; i < arg_infos.size(); i++) {
        std::memcpy(arg_data.data() + arg_infos[i].offset, kernelParams[i], arg_infos[i].size);
        // FIXME? (kernelParams?, __HIP__KERNEL_PARAM___?)
    }
    */
    
    hipError_t result = (*hipModuleLaunchKernel_fptr)(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                                                            sharedMemBytes, stream, kernelParams, extra);

    dim3 numBlocks = { gridDimX, gridDimY, gridDimZ };
    dim3 dimBlocks = { blockDimX, blockDimY, blockDimZ };

    gputrace_event event;
    gputrace_event_launch launch_event;

    event.id = g_curr_event++;
    event.name = "hipModuleLaunchKernel";
    event.rc = result;
    event.stream = stream;
    event.type = EVENT_LAUNCH;


    launch_event.kernel_name = kernel_name;
    launch_event.num_blocks = numBlocks;
    launch_event.dim_blocks = dimBlocks;
    launch_event.shared_mem_bytes = sharedMemBytes;
    launch_event.argdata = arg_data;

    event.data = std::move(launch_event);
    pushback_event(event);

    return result;
}

};
