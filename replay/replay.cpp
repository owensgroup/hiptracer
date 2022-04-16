#include <cstdio>
#include <iostream>
#include <unordered_map>

#include "hip/hip_runtime.h"

// header_t, event_t
#include "trace.h"

#include "elf.h"

#define HIPTRACER_BIGALLOC 0

std::unordered_map<uint64_t, uint64_t> allocations;

uint64_t getDevPtr(uint64_t p) 
{
    return allocations[p];
}

int main()
{
    // Open trace file
    std::FILE* events_fp = std::fopen("trace.events", "rb");
    std::FILE* data_fp = std::fopen("trace.data", "rb");
    
    if (events_fp == nullptr) {
        std::printf("Unable to open events file\n");
    }

    header_t header;
    std::fread(&header, sizeof(header), 1, events_fp);

    std::printf("magic: %lx\n", header.magic);
    std::printf("num events: %lu\n", header.num_events);

    std::vector<event_t> events(header.num_events);
    std::vector<data_t> chunks;

    std::fread(events.data(), sizeof(events[0]), header.num_events, events_fp);

    // Iterate over events 

    for (int i = 0; i < header.num_events; i++)
    {
        event_t event = events[i];

        std::cout << "Playing back event[" << i << "]" << std::endl;
        // Print event name
        // Begin timing
        // Execute event type
        if (event.type == EVENT_DEVICE) {

            std::printf("\thipGetDeviceProperties\n");

            // TODO: Map pointer value from trace to _my_ pointer
            hipDeviceProp_t* devProp = NULL;
            hipDeviceProp_t* my_devProp = new hipDeviceProp_t;
            // TODO: Remap trace devices to replay devices
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

                void* devPtr = (void*) getDevPtr((uint64_t) dst);
                hipMemcpy(devPtr, buffer.data(), size, kind);
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
            for (int i = 0; i < arg_infos.size(); i++) {
                total_argsize += arg_infos[i].size;
            }
            std::printf("\t arg_size from code object%d\n", total_argsize);
            std::printf("\t args_size from trace %d\n", args_size);

            std::printf("\tChecking args for allocations\n");

        } else if (event.type == EVENT_MALLOC) {
            std::cout << "\thipMalloc" << std::endl;

            void* ptr;
            size_t size;

            std::fread(&ptr, sizeof(void*), 1, data_fp);
            std::fread(&size, sizeof(size_t), 1, data_fp);

            std::printf("\tRead pointer %p\n", ptr);
            void* realPtr;
            if (hipMalloc(&realPtr, size) == hipSuccess) {
                allocations[(uint64_t) ptr] = (uint64_t) realPtr;
                std::printf("\tAdding allocation mapping\n");
            }
        } else if (event.type == EVENT_FREE) {
            std::printf("\thipFree\n");
            std::fseek(data_fp, event.offset, SEEK_SET);

            void* p = NULL;
            std::fread(&p, sizeof(void*), 1, data_fp);
            std::printf("\t\tp:%p\n", p);

        } else {
            // Unhandled event type
        }
    }


    return 0;
}
