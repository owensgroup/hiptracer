#include <cstdio>
#include <cstddef>
#include <vector>
#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <functional>
#include <future>

#include <dlfcn.h>

#include <zstd.h>

// event_t, header_t, data_t
#include "trace.h"
// ArgInfo, getArgInfo
#include "elf.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

#include "sqlite3.h"
#include "callbacks.h"

bool REPLAY = false;
bool DEBUG = false;
bool REWRITE = false;
char* EVENTDB = NULL;

int g_curr_event = 0;
sqlite3 *g_event_db = NULL;
std::string g_codeobj_filename;

void* rocmLibHandle = NULL;

std::vector<std::future<void>> prepared;

__attribute__((constructor)) void hiptracer_init()
{
    // Setup options
    auto as_bool = [](char* env) { return (env != NULL) && std::string(env) == "true"; };
    
    DEBUG = as_bool(std::getenv("HIPTRACER_DEBUG"));
    EVENTDB = std::getenv("HIPTRACER_EVENTDB");

    if (EVENTDB == NULL) {
        EVENTDB = "./tracer-default.db";
    }
    
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
    
    const char* create_events_sql = "DROP TABLE IF EXISTS Events;"
                              "DROP TABLE IF EXISTS EventMalloc;"
                              "DROP TABLE IF EXISTS EventMemcpy;"
                              "DROP TABLE IF EXISTS EventLaunch;"
                              "DROP TABLE IF EXISTS EventFree;"
                              "DROP TABLE IF EXISTS Code;"
                              "CREATE TABLE Events(Id INTEGER PRIMARY KEY, EventType INT, Name TEXT, Rc INT, Stream INT);"
                              "CREATE TABLE EventMalloc(Id INTEGER PRIMARY KEY, Stream INT, Ptr INT64, Size INT);"
                              "CREATE TABLE EventMemcpy(Id INTEGER PRIMARY KEY, Stream INT, Dst INT64, Src INT64, Size INT, Kind INT, HostData BLOB);"
                              "CREATE TABLE EventLaunch(Id INTEGER PRIMARY KEY, Stream INT, KernelName TEXT, NumX INT, NumY INT, NumZ INT,DimX INT, DimY INT, DimZ INT, SharedMem INT, ArgData BLOB);"
                              "CREATE TABLE EventFree(Id INTEGER PRIMARY KEY, Stream INT, Ptr INT);"
                              "CREATE TABLE Code(Id INTEGER PRIMARY KEY, Idx INTEGER, Triple TEXT, Path TEXT);";
    if(sqlite3_exec(g_event_db, create_events_sql, 0, 0, NULL)) {
        std::printf("Failed to create Events table: %s\n", sqlite3_errmsg(g_event_db));
        std::exit(-1);
    }
}

void complete_sql_stmt(sqlite3_stmt* pStmt) {
    sqlite3_step(pStmt);
    sqlite3_finalize(pStmt);
}

__attribute__((destructor)) void hiptracer_deinit()
{
    for (int i = 0; i < prepared.size(); i++) {
        prepared[i].wait();
    }
    sqlite3_close(g_event_db);
}

std::vector<char> compress_data(void *data, size_t size)
{
    size_t cBuffSize = ZSTD_compressBound(size);
        
    std::vector<char> compressed;
    compressed.reserve(cBuffSize);
    size_t actualSize = ZSTD_compress(compressed.data(), cBuffSize, data, size, 1);

    return compressed;
}

int insert_free(int event_id, hipStream_t stream, void* p)
{
    sqlite3_stmt* pStmt;
    const char* sql = "INSERT INTO EventFree(Id, Stream, Ptr) VALUES(?, ?, ?);";

    int rc = sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);

    rc = sqlite3_bind_int(pStmt, 1, event_id);
    rc = sqlite3_bind_int(pStmt, 2, (uintptr_t) stream);
    rc = sqlite3_bind_int(pStmt, 3, (uintptr_t) p);

    if (rc == SQLITE_OK) {
        prepared.push_back(std::async(std::launch::deferred, complete_sql_stmt, pStmt)); 
    } else {
        std::printf("Error with statement%s\n", sqlite3_errmsg(g_event_db));
    }
    return rc;
}

int insert_launch(int event_id, hipStream_t stream, const char* kernel_name, 
                  dim3 num_blocks, dim3 dim_blocks, int shared_mem_bytes,
                  void* arg_data, size_t arg_size)
{
    sqlite3_stmt* pStmt;
    const char* sql = "INSERT INTO EventLaunch(Id, Stream, KernelName, NumX, NumY, NumZ, DimX, DimY, DimZ, SharedMem, ArgData)"
                      "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);

    rc = sqlite3_bind_int(pStmt, 1, event_id);
    rc = sqlite3_bind_int(pStmt, 2, (uint64_t)stream);
    rc = sqlite3_bind_text(pStmt, 3, kernel_name, -1, SQLITE_STATIC);
    rc = sqlite3_bind_int(pStmt, 4, num_blocks.x);
    rc = sqlite3_bind_int(pStmt, 5, num_blocks.y);
    rc = sqlite3_bind_int(pStmt, 6, num_blocks.z);
    rc = sqlite3_bind_int(pStmt, 7, dim_blocks.x);
    rc = sqlite3_bind_int(pStmt, 8, dim_blocks.y);
    rc = sqlite3_bind_int(pStmt, 9, dim_blocks.z);
    rc = sqlite3_bind_int(pStmt, 10, shared_mem_bytes);
    rc = sqlite3_bind_blob(pStmt, 11, arg_data, arg_size, SQLITE_TRANSIENT);

    if (rc == SQLITE_OK) {
        prepared.push_back(std::async(std::launch::deferred, complete_sql_stmt, pStmt)); 
    } else {
        std::printf("Error with statement%s\n", sqlite3_errmsg(g_event_db));
    }
    return rc;
}

int insert_malloc(int event_id, hipStream_t stream, void** p, size_t size)
{
    sqlite3_stmt* pStmt = NULL;
    const char* sql = "INSERT INTO EventMalloc(Id, Stream, Ptr, Size) VALUES(?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);
    if (rc != SQLITE_OK) {
        std::printf("SQL ERROR\n");
    }

    rc = sqlite3_bind_int(pStmt, 1, event_id);
    rc = sqlite3_bind_int(pStmt, 2, (uint64_t) stream);
    rc = sqlite3_bind_int64(pStmt, 3, (uint64_t) *p);
    rc = sqlite3_bind_int(pStmt, 4, size);

    if (rc == SQLITE_OK) {
        prepared.push_back(std::async(std::launch::deferred, complete_sql_stmt, pStmt)); 
    } else {
        std::printf("Error with statement%s\n", sqlite3_errmsg(g_event_db));
    }
    return rc;
}

int insert_memcpy(int event_id, hipStream_t stream, void* dst, const void* src, size_t size, hipMemcpyKind kind)
{
    sqlite3_stmt* pStmt = NULL;
    const char* sql = "INSERT INTO EventMemcpy(Id, Stream, Dst, Src, Size, Kind, HostData) VALUES(?, ?, ?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);

    rc = sqlite3_bind_int(pStmt, 1, event_id);
    rc = sqlite3_bind_int(pStmt, 2, (uint64_t) stream);
    rc = sqlite3_bind_int64(pStmt, 3, (uint64_t) dst);
    rc = sqlite3_bind_int64(pStmt, 4, (uint64_t) src);
    rc = sqlite3_bind_int(pStmt, 5, size);
    rc = sqlite3_bind_int(pStmt, 6, kind);

    if (kind == hipMemcpyHostToDevice) {
        rc = sqlite3_bind_blob(pStmt, 7, src, size, SQLITE_TRANSIENT);
    }

    if (rc == SQLITE_OK) {
        prepared.push_back(std::async(std::launch::deferred, complete_sql_stmt, pStmt)); 
    }  else {
        std::printf("Error with statement%s\n", sqlite3_errmsg(g_event_db));
    }
    return rc;
}

int insert_event(HIP_EVENT type, char* name, int hipRc, hipStream_t stream, int event_id)
{
    sqlite3_stmt* pStmt = NULL;
    const char* sql = "INSERT INTO Events(EventType, Name, Rc, Stream, Id) VALUES(?, ?, ?, ?, ?);";

    int rc = sqlite3_prepare_v2(g_event_db, sql, -1, &pStmt, 0);

    rc = sqlite3_bind_int(pStmt, 1, type);
    rc = sqlite3_bind_text(pStmt, 2, name, -1, SQLITE_STATIC);
    rc = sqlite3_bind_int(pStmt, 3, hipRc);
    rc = sqlite3_bind_int(pStmt, 4, (uint64_t) stream);
    rc = sqlite3_bind_int(pStmt, 5, event_id);

    if (rc == SQLITE_OK) {
        prepared.push_back(std::async(std::launch::deferred, complete_sql_stmt, pStmt)); 
    } else {
        std::printf("Error with statement%s\n", sqlite3_errmsg(g_event_db));
    }
    return rc;
}

/*
   ****
   HIP Functions
   ****
*/

const unsigned __hipFatMAGIC2 = 0x48495046; // "HIPF"

#define CLANG_OFFLOAD_BUNDLER_MAGIC "__CLANG_OFFLOAD_BUNDLE__"
#define AMDGCN_AMDHSA_TRIPLE "hip-amdgcn-amd-amdhsa"

extern "C" {

hipError_t  (*hipFree_fptr)(void*) = NULL;
hipError_t  (*hipMalloc_fptr)(void**, size_t) = NULL;
hipError_t  (*hipMemcpy_fptr)(void*, const void*, size_t, hipMemcpyKind) = NULL;
hipError_t  (*hipGetDeviceProperties_fptr)(hipDeviceProp_t*,int) = NULL;
void*       (*hipRegisterFatBinary_fptr)(const void*) = NULL;
hipError_t  (*hipLaunchKernel_fptr)(const void*, dim3, dim3, void**, size_t, hipStream_t) = NULL;
hipError_t  (*hipSetupArgument_fptr)(const void* arg, size_t size, size_t offset) = NULL;

hipError_t  (*hipModuleLaunchKernel_fptr)(hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
                                         unsigned int, hipStream_t, void**, void**) = NULL;
hipError_t  (*hipModuleLoad_fptr)(hipModule_t*, const char*) = NULL;
hipError_t  (*hipModuleGetFunction_fptr)(hipFunction_t*, hipModule_t, const char*) = NULL;
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

    hipError_t result = (*hipFree_fptr)(p);

    insert_event(EVENT_FREE, (char*)__func__, (int) result, hipStreamDefault, g_curr_event);
    insert_free(g_curr_event, hipStreamDefault, p);

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

    insert_event(EVENT_MALLOC, (char*)__func__, (int) result, hipStreamDefault, g_curr_event);
    insert_malloc(g_curr_event, hipStreamDefault, p, size);

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

    hipError_t result = (*hipMemcpy_fptr)(dst, src, size, kind);

    insert_event(EVENT_MEMCPY, (char*)__func__, result, hipStreamDefault, g_curr_event);
    insert_memcpy(g_curr_event, hipStreamDefault, dst, src, size, kind);

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

    hipError_t result = (*hipGetDeviceProperties_fptr)(p_prop, device);

    insert_event(EVENT_DEVICE, (char*)__func__, (int) result, hipStreamDefault, g_curr_event);

    return result;
}

hipError_t hipSetupArgument(const void * arg, size_t size, size_t offset)
{
     g_curr_event++;

    if (hipSetupArgument_fptr == NULL) {
        hipSetupArgument_fptr = ( hipError_t (*) (const void*, size_t size, size_t offset)) dlsym(rocmLibHandle, "hipSetupArgument");
    } 
    
    hipError_t result = (hipSetupArgument_fptr)(arg, size, offset);
    insert_event(EVENT_DEVICE, (char*)__func__, (int) result, hipStreamDefault, g_curr_event);

    return result;
}

hipError_t hipLaunchKernel(const void* function_address,
        dim3 numBlocks,
        dim3 dimBlocks,
        void** args,
        size_t sharedMemBytes ,
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
    
    std::vector<ArgInfo> arg_infos = getArgInfo(g_codeobj_filename.c_str());

    uint64_t total_size = 0;
    if (arg_infos.size() > 0) {
        total_size = arg_infos[arg_infos.size() - 1].offset + arg_infos[arg_infos.size() - 1].size;
    }

    std::vector<std::byte> arg_data(total_size);

    for(int i = 0; i < arg_infos.size(); i++) {
        std::memcpy(arg_data.data() + arg_infos[i].offset, args + arg_infos[i].offset, arg_infos[i].size);
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

    hipError_t result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);
    insert_event(EVENT_LAUNCH, (char*)__func__, (int) result, stream, g_curr_event);
    insert_launch(g_curr_event, stream, kernel_name.c_str(), numBlocks, dimBlocks, sharedMemBytes, arg_data.data(), arg_data.size());

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

    std::vector<ArgInfo> arg_infos = getArgInfo(g_codeobj_filename.c_str());

    uint64_t total_size = 0;
    if (arg_infos.size() > 0) {
        total_size = arg_infos[arg_infos.size() - 1].offset + arg_infos[arg_infos.size() - 1].size;
    }

    std::vector<std::byte> arg_data(total_size);

    for(int i = 0; i < arg_infos.size(); i++) {
        std::memcpy(arg_data.data() + arg_infos[i].offset, kernelParams + arg_infos[i].offset, arg_infos[i].size);
        // FIXME? (kernelParams?, __HIP__KERNEL_PARAM___?)
    }
    
    hipError_t result = (*hipModuleLaunchKernel_fptr)(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ,
                                                            sharedMemBytes, stream, kernelParams, extra);

    dim3 numBlocks = { gridDimX, gridDimY, gridDimZ };
    dim3 dimBlocks = { blockDimX, blockDimY, blockDimZ };
    insert_event(EVENT_LAUNCH, (char*)__func__, (int) result, stream, g_curr_event);
    insert_launch(g_curr_event, stream, kernel_name.c_str(), numBlocks, dimBlocks, sharedMemBytes, arg_data.data(), arg_data.size());

    return result;
}

void* __hipRegisterFatBinary(const void* data)
{
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

        std::string filename = std::string("./") + triple.c_str() + ".code";
        if (g_codeobj_filename.size() == 0) {
            g_codeobj_filename = filename;
        }

        std::FILE* code = std::fopen(filename.c_str(), "wb");
        std::fwrite(bin_bytes + desc.offset, sizeof(std::byte), desc.size, code);
        std::fclose(code);

		const char* sql = "INSERT INTO Code(Idx, Triple, Path) VALUES(?, ?, ?)";
		sqlite3_stmt* pStmt; 
		sqlite3_prepare(g_event_db, sql, -1, &pStmt, 0);

        sqlite3_bind_int(pStmt, 1, i);
		sqlite3_bind_text(pStmt, 2, triple.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 3, filename.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_step(pStmt);
        sqlite3_finalize(pStmt);

        next += chunk_size;
    }

    return (*hipRegisterFatBinary_fptr)(data);
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
