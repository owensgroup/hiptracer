#include <cstdio>
#include <cstddef>
#include <vector>
#include <iostream>
#include <cstring>
#include <string>

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

// event_t, header_t
#include "trace.h"

const char* g_prog_name = "./vectoradd"; //FIXME
int g_curr_event = 0;

typedef void* my_hipStream_t;

void write_header(std::FILE* fp, header_t* header)
{
    header_t hdr;
    hdr.magic = 0xDEADBEEF;
    if (header) {
        hdr.num_events = header->num_events;
    }
    std::fwrite(&hdr, sizeof(header_t), 1, fp);
}

void write_event_list(std::FILE* fp, std::vector<event_t> &events)
{
    std::fwrite(events.data(), sizeof(events[0]), events.size(), fp);
}

void write_event(std::FILE* fp, event_t* event)
{
    event_t e;
    if (event) {
        e.name = event->name;
    }

    std::fwrite(&e, sizeof(event_t), 1, fp);
}

extern "C" {
    const unsigned __hipFatMAGIC2 = 0x48495046; // "HIPF"

#define CLANG_OFFLOAD_BUNDLER_MAGIC "__CLANG_OFFLOAD_BUNDLE__"
#define AMDGCN_AMDHSA_TRIPLE "hip-amdgcn-amd-amdhsa"

    typedef struct dim3 {
        uint32_t x;  ///< x
        uint32_t y;  ///< y
        uint32_t z;  ///< z
    } dim3;

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

    typedef struct my_hipDeviceProp_t {
        char name[256];            ///< Device name.
        size_t totalGlobalMem;     ///< Size of global memory region (in bytes).
        size_t sharedMemPerBlock;  ///< Size of shared memory region (in bytes).
        int regsPerBlock;          ///< Registers per block.
        int warpSize;              ///< Warp size.
        int maxThreadsPerBlock;    ///< Max work items per work group or workgroup max size.
        int maxThreadsDim[3];      ///< Max number of threads in each dimension (XYZ) of a block.
        int maxGridSize[3];        ///< Max grid dimensions (XYZ).
        int clockRate;             ///< Max clock frequency of the multiProcessors in khz.
        int memoryClockRate;       ///< Max global memory clock frequency in khz.
        int memoryBusWidth;        ///< Global memory bus width in bits.
        size_t totalConstMem;      ///< Size of shared memory region (in bytes).
        int major;  ///< Major compute capability.  On HCC, this is an approximation and features may
        ///< differ from CUDA CC.  See the arch feature flags for portable ways to query
        ///< feature caps.
        int minor;  ///< Minor compute capability.  On HCC, this is an approximation and features may
        ///< differ from CUDA CC.  See the arch feature flags for portable ways to query
        ///< feature caps.
        int multiProcessorCount;          ///< Number of multi-processors (compute units).
        int l2CacheSize;                  ///< L2 cache size.
        int maxThreadsPerMultiProcessor;  ///< Maximum resident threads per multi-processor.
        int computeMode;                  ///< Compute mode.
        int clockInstructionRate;  ///< Frequency in khz of the timer used by the device-side "clock*"
        ///< instructions.  New for HIP.
        // hipDeviceArch_t arch;      ///< Architectural feature flags.  New for HIP.
        int concurrentKernels;     ///< Device can possibly execute multiple kernels concurrently.
        int pciDomainID;           ///< PCI Domain ID
        int pciBusID;              ///< PCI Bus ID.
        int pciDeviceID;           ///< PCI Device ID.
        size_t maxSharedMemoryPerMultiProcessor;  ///< Maximum Shared Memory Per Multiprocessor.
        int isMultiGpuBoard;                      ///< 1 if device is on a multi-GPU board, 0 if not.
        int canMapHostMemory;                     ///< Check whether HIP can map host memory
        int gcnArch;                              ///< DEPRECATED: use gcnArchName instead
        char gcnArchName[256];                    ///< AMD GCN Arch Name.
        int integrated;            ///< APU vs dGPU
        int cooperativeLaunch;            ///< HIP device supports cooperative launch
        int cooperativeMultiDeviceLaunch; ///< HIP device supports cooperative launch on multiple devices
        int maxTexture1DLinear;    ///< Maximum size for 1D textures bound to linear memory
        int maxTexture1D;          ///< Maximum number of elements in 1D images
        int maxTexture2D[2];       ///< Maximum dimensions (width, height) of 2D images, in image elements
        int maxTexture3D[3];       ///< Maximum dimensions (width, height, depth) of 3D images, in image elements
        unsigned int* hdpMemFlushCntl;      ///< Addres of HDP_MEM_COHERENCY_FLUSH_CNTL register
        unsigned int* hdpRegFlushCntl;      ///< Addres of HDP_REG_COHERENCY_FLUSH_CNTL register
        size_t memPitch;                 ///<Maximum pitch in bytes allowed by memory copies
        size_t textureAlignment;         ///<Alignment requirement for textures
        size_t texturePitchAlignment;    ///<Pitch alignment requirement for texture references bound to pitched memory
        int kernelExecTimeoutEnabled;    ///<Run time limit for kernels executed on the device
        int ECCEnabled;                  ///<Device has ECC support enabled
        int tccDriver;                   ///< 1:If device is Tesla device using TCC driver, else 0
        int cooperativeMultiDeviceUnmatchedFunc;        ///< HIP device supports cooperative launch on multiple
        ///devices with unmatched functions
        int cooperativeMultiDeviceUnmatchedGridDim;     ///< HIP device supports cooperative launch on multiple
        ///devices with unmatched grid dimensions
        int cooperativeMultiDeviceUnmatchedBlockDim;    ///< HIP device supports cooperative launch on multiple
        ///devices with unmatched block dimensions
        int cooperativeMultiDeviceUnmatchedSharedMem;   ///< HIP device supports cooperative launch on multiple
        ///devices with unmatched shared memories
        int isLargeBar;                  ///< 1: if it is a large PCI bar device, else 0
        int asicRevision;                ///< Revision of the GPU in this device
        int managedMemory;               ///< Device supports allocating managed memory on this system
        int directManagedMemAccessFromHost; ///< Host can directly access managed memory on the device without migration
        int concurrentManagedAccess;     ///< Device can coherently access managed memory concurrently with the CPU
        int pageableMemoryAccess;        ///< Device supports coherently accessing pageable memory
        ///< without calling hipHostRegister on it
        int pageableMemoryAccessUsesHostPageTables; ///< Device accesses pageable memory via the host's page tables
    } my_hipDeviceProp_t;


    typedef enum my_hipError_t {
        hipSuccess = 0,
        hipErrorInvalidValue = 1,
        hipErrorOutOfMemory = 2,
        hipErrorMemoryAllocation = 2,
        hipErrorNotInitialized = 3,
        hipErrorInitializationError = 3,
        hipErrorDeinitialized = 4,
        hipErrorProfilerDisabled = 5
    } my_hipError_t;

    typedef enum my_hipMemcpyKind {
        hipMemcpyHostToHost = 0,
        hipMemcpyHostToDevice = 1,
        hipMemcpyDeviceToHost = 2,
        hipMemcpyDeviceToDevice = 3,
        hipMemcpyDefault = 4
    } my_hipMemcpyKind;

    void* rocmLibHandle = NULL;

    my_hipError_t (*hipGetDeviceProperties_fptr)(my_hipDeviceProp_t*,int) = NULL;
    void* (*hipRegisterFatBinary_fptr)(const void*) = NULL;
    my_hipError_t (*hipLaunchKernel_fptr)(const void*, dim3, dim3, void**, size_t, my_hipStream_t) = NULL;
    my_hipError_t (*hipFree_fptr)(void*) = NULL;
    my_hipError_t (*hipMalloc_fptr)(void**, size_t) = NULL;
    my_hipError_t (*hipMemcpy_fptr)(void*, const void*, size_t, my_hipMemcpyKind) = NULL;

    my_hipError_t hipGetDeviceProperties(my_hipDeviceProp_t* p_prop, int device)
    {
        g_curr_event++;

        if (rocmLibHandle == NULL) { 
            rocmLibHandle = dlopen("/opt/rocm/hip/lib/libamdhip64.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (hipGetDeviceProperties_fptr == NULL) {
            hipGetDeviceProperties_fptr = (my_hipError_t (*) (my_hipDeviceProp_t*, int)) dlsym(rocmLibHandle, "hipGetDeviceProperties");
        }

        printf("[%d] hooked: hipGetDeviceProperties\n", g_curr_event);
        printf("[%d] \t p_prop: %p\n", g_curr_event, p_prop);
        printf("[%d]\t device: %d\n", g_curr_event, device);
        printf("[%d] calling: hipGetDeviceProperties\n", g_curr_event); 

        my_hipError_t result = (*hipGetDeviceProperties_fptr)(p_prop, device);

        printf("[%d] \t result: %d\n", g_curr_event, result);
        return result;
    }

    my_hipError_t hipLaunchKernel(const void* function_address,
            dim3 numBlocks,
            dim3 dimBlocks,
            void** args,
            size_t sharedMemBytes,
            my_hipStream_t stream)
    {
        g_curr_event++;

        if (rocmLibHandle == NULL) {
            rocmLibHandle = dlopen("/opt/rocm/hip/lib/libamdhip64.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (hipLaunchKernel_fptr == NULL) {
            hipLaunchKernel_fptr = ( my_hipError_t (*) (const void*, dim3, dim3, void**, size_t, my_hipStream_t)) dlsym(rocmLibHandle, "hipLaunchKernel");
        }

        printf("[%d] hooked: hipLaunchKernel\n", g_curr_event);
        printf("[%d] \t function_address: %p\n", g_curr_event,  function_address);
        printf("[%d] \t numBlocks: %d %d %d\n", g_curr_event, numBlocks.x, numBlocks.y, numBlocks.z);
        printf("[%d] \t dimBlocks: %d %d %d\n", g_curr_event, dimBlocks.x, dimBlocks.y, dimBlocks.z);
        printf("[%d] \t args: %p\n", g_curr_event, args);
        printf("[%d] \t sharedMemBytes: %lu\n", g_curr_event, sharedMemBytes);
        printf("[%d] \t stream: %p\n", g_curr_event, stream); 
        printf("[%d] calling: hipLaunchKernel\n", g_curr_event);

        my_hipError_t result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);

        printf("[%d] \t result: %d\n", g_curr_event, result);

        return result;
    }


    void* __hipRegisterFatBinary(const void* data)
    {
        if (rocmLibHandle == NULL) {
            rocmLibHandle = dlopen("/opt/rocm/hip/lib/libamdhip64.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (hipRegisterFatBinary_fptr == NULL) {
            hipRegisterFatBinary_fptr = ( void* (*) (const void*)) dlsym(rocmLibHandle, "__hipRegisterFatBinary");
        }
        printf("[%d] hooked: __hipRegisterFatBinary(%p)\n", g_curr_event, data);

        /*
        printf("Printing %p\n", data);
        printf("__CudaFatBinaryWrapper struct\n");
        */

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

            //std::fwrite(bytes.data(), sizeof(std::byte), bytes.size(), fp);

            std::string triple;
            triple.reserve(desc.tripleSize + 1);
            std::memcpy(triple.data(), bytes.data() + sizeof(desc), desc.tripleSize);
            triple[desc.tripleSize] = '\0';

            if (desc.size > 0) {
                printf("[%d] writing code object for %s:\n", g_curr_event, triple.c_str());
                std::FILE* code = std::fopen(triple.c_str(), "wb");
                std::fwrite(bin_bytes + desc.offset, sizeof(std::byte), desc.size, code);
            }
            next += chunk_size;
        }

        return (*hipRegisterFatBinary_fptr)(data);
    }
};

int main() {
    std::cout << "Running as executable" << std::endl;
    std::cout << "Create dummy trace" << std::endl;

    return 0;
}
