#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "hip/hip_runtime.h"

#include "hsakmt.h"
#include "kfd_ioctl.h"

// header_t, event_t
#include "trace.h"

// ArgInfo, getArgInfo
#include "elf.h"

#define HIPTRACER_BIGALLOC 0

std::unordered_map<uintptr_t, uintptr_t> allocations;

uint64_t gPageSize = 4096;

uint64_t getAllocatedSize(uint64_t p) 
{
    return allocations[p];
}

int main()
{
    Trace trace;
    trace.open("trace");

    std::FILE* data_fp = trace.data_fp;

    std::vector<event_t>& events = trace.events;
    std::vector<data_t> chunks;

    // Iterate over events 

    for (int i = 0; i < events.size(); i++)
    {
        event_t event = events[i];

        std::cout << "Playing back event[" << i << "]" << std::endl;
        // Print event name
        // Begin timing
        // Execute event type
        if (event.type == EVENT_DEVICE) {

            std::printf("\thipGetDeviceProperties\n");

            hipDeviceProp_t* devProp = NULL;
            hipDeviceProp_t* my_devProp = new hipDeviceProp_t;

            int device;

            std::fseek(data_fp, event.offset, SEEK_SET);

            std::fread(&devProp, sizeof(hipDeviceProp_t*), 1, data_fp);
            std::fread(&device, sizeof(int), 1, data_fp);

            std::printf("\t\tp_prop%p\n", devProp);
            std::printf("\t\tdevice%d\n", device);

            std::printf("\texecuting...\n");
            hipGetDeviceProperties(my_devProp, device);

        } else if (event.type == EVENT_MEMCPY) {
            std::printf("\thipMemcpy\n");

            std::fseek(data_fp, event.offset, SEEK_SET);
            std::printf("event size: %lu\n", event.size);

            void* dst;
            void* src;
            size_t size;
            hipMemcpyKind kind; 

            std::fread(&dst, sizeof(void*), 1, data_fp);
            std::fread(&src, sizeof(const void*), 1, data_fp);
            std::fread(&size, sizeof(size_t), 1, data_fp);
            std::fread(&kind, sizeof(hipMemcpyKind), 1, data_fp);

            if (kind == hipMemcpyHostToDevice) {
                std::printf("\t size: %lu\n", size);
                std::printf("\t reading host buffer of size %zu from trace...\n", size);
                std::vector<std::byte> buffer(size); 
                std::fread(buffer.data(), sizeof(std::byte), size, data_fp); 

                std::printf("\t replaying HostToDev MemCpy\n");
 
                hipMemcpy(dst, buffer.data(), size, kind);
            }
        } else if (event.type == EVENT_LAUNCH) {
            std::printf("\thipLaunchKernel\n");

            std::fseek(data_fp, event.offset, SEEK_SET);
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

            std::printf("\tGetting arg info from code object\n");
            uint64_t total_argsize = 0;
            
            std::vector<ArgInfo> arg_infos = getArgInfo();

            std::printf("\tChecking args for allocations\n");
            for (int i = 0; i < arg_infos.size(); i++) {
                total_argsize += arg_infos[i].size;
                std::printf("value kind %s\n", arg_infos[i].value_kind.c_str());
                std::printf("Access %s\n", arg_infos[i].access.c_str());

                if (arg_infos[i].value_kind == "global buffer") {
                    // Check for pointers in allocation table
                }
            }
            
            std::printf("\t arg_size from code object%d\n", total_argsize);
            std::printf("\t args_size from trace %d\n", args_size);

            std::printf("\t ... Performing launch ... \n");
            hipLaunch

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

            allocations[ptr_as_int] = ret_as_int;

        } else if (event.type == EVENT_FREE) {
            std::printf("\thipFree\n");
            std::fseek(data_fp, event.offset, SEEK_SET);

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
    }


    return 0;
}
