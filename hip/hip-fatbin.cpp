#include <dlfcn.h>
#include <cstdio>
#include <cstddef>
#include <condition_variable>
#include <string>

// DynInst
#include "InstructionDecoder.h"

#include "elf.h"
#include "trace.h"

#include "sqlite3.h"

#include "atomic_queue/atomic_queue.h"
#include "flat_hash_map.hpp"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

std::vector<Instr> get_instructions(std::string text) {
    std::vector<Instr> instructions;

    Dyninst::InstructionAPI::InstructionDecoder decoder(text.data(), text.size(), Dyninst::Architecture::Arch_amdgpu_gfx908);

    Dyninst::InstructionAPI::Instruction i = decoder.decode();
    size_t offset = 0;
    while(i.isValid()) {
        Instr instr;
        instr.offset = offset;
        instr.cdna = std::string(i.format(instr.offset));
        instr.size = i.size();
        //instr.num_operands = i.getNumOperands();

        instructions.push_back(instr);

        offset += instr.size;
        i = decoder.decode();
    }
    std::printf("INSTRUCTIONS SIZE %d\n", instructions.size());
    return instructions;
}

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


void* __hipRegisterFatBinary(const void* data)
{
    if (hipRegisterFatBinary_fptr == NULL) {
        hipRegisterFatBinary_fptr = ( void* (*) (const void*)) dlsym(get_rocm_lib(), "__hipRegisterFatBinary");
    }
    if (get_handled_fatbins().find((uint64_t) data) != get_handled_fatbins().end()) {
        if (get_handled_fatbins().at((uint64_t) data)) return (*hipRegisterFatBinary_fptr)(data);
    }

    int register_event_id = get_curr_event()++;
    if (register_event_id % 100 == 0) {
        std::printf("%d\n", register_event_id);
    }

    struct fb_wrapper {
        uint32_t magic;
        uint32_t version;
        void* binary;
        void* unused;
    };
    const fb_wrapper* fbwrapper = static_cast<const fb_wrapper*>(data);
    typedef struct {
        const char magic[sizeof(CLANG_OFFLOAD_BUNDLER_MAGIC) - 1] = 
            { '_', '_', 
              'C', 'L', 'A', 'N', 'G', '_', 
              'O', 'F', 'F', 'L', 'O', 'A', 'D', '_',
              'B', 'U', 'N', 'D', 'L', 'E',
              '_', '_' };
        uint64_t numBundles; 
    } fb_header;

    const fb_header* fbheader = static_cast<const fb_header*>(fbwrapper->binary);

    const void* next = static_cast<const char*>(fbwrapper->binary) + sizeof(fb_header);
    std::printf("num bundles %d\n", fbheader->numBundles);
    for(int i = 0; i < fbheader->numBundles; i++) {
        struct code_desc {
            uint64_t offset;
            uint64_t size;
            uint64_t tripleSize;
        };

        const code_desc* descriptor = static_cast<const code_desc*>(next);

        // Determine chunk size 
        size_t chunk_size = sizeof(code_desc) + descriptor->tripleSize;

        const char* unterm_triple = static_cast<const char*>(next) + sizeof(code_desc);
        std::string_view triple(unterm_triple, descriptor->tripleSize);

        if (triple.find("host") != std::string_view::npos) {
            next = static_cast<const char*>(next) + chunk_size;
            continue;
        }
        if (triple.find("gfx908") == std::string_view::npos) {
            next = static_cast<const char*>(next) + chunk_size;
            continue;
        }
        std::printf("TRIPLE %s\n", triple.data());

        std::string filename = std::string("./code/") + std::string(triple) + "-" + std::to_string(get_curr_event()) + "-" + std::to_string(i) + ".code";
        std::string image{static_cast<const char*>(fbwrapper->binary) + descriptor->offset, descriptor->size};

        if (get_tool() == TOOL_CAPTURE) {
            gputrace_event event;
            gputrace_event_code code_event;
            // Make a copy of the code object here. 
            std::istringstream is(image); // FIXME: Makes a second copy?
            getArgInfo(is, get_kernel_arg_sizes(), register_event_id);

            code_event.code = image;
            code_event.filename = filename;

            event.id = get_curr_event();
            event.rc = hipSuccess;
            event.stream = hipStreamDefault;
            event.type = EVENT_CODE;

            event.data = std::move(code_event);

            pushback_event(event);
        } else if (get_tool() == TOOL_MEMTRACE) {
            //std::FILE* code = std::fopen(filename.c_str(), "wb");
            //std::fwrite(image.data(), sizeof(char), image.size(), code);
            //std::fclose(code);

			std::istringstream is(image);
			ELFIO::elfio reader;
			reader.load(is);

			for(int i = 0; i < reader.sections.size(); i++) {
				ELFIO::section* psec = reader.sections[i];

				if (psec) {
					std::string sec_name = psec->get_name();
					if (sec_name == std::string(".text")) {
						std::string text{static_cast<const char*>(psec->get_data()), psec->get_size()};
						std::vector<Instr> instructions = get_instructions(text);
						for (int i = 0; i < instructions.size(); i++) {
							Instr instr = instructions[i];
                            //std::printf("Instr %d: %s\n", i, instr.getCdna());
                            /*
							if (instr.isLoad() || instr.isStore()) {
								uint32_t offset = instr.getOffset();
								uint32_t jump = image.size() - offset;
								uint32_t new_inst = 0xBF820000;
								uint32_t ret_inst = 0xBF820000;

								new_inst |= (jump / 4) - 1;

								std::printf("NEW INST 0x%dx", new_inst);

            					uint32_t old_inst = 0x0; // FIXME ~ Target instr could be 64 bits, ask llvm-mc / DynInst
            					std::memcpy(&old_inst, &text[offset], sizeof(old_inst));
            					std::memcpy(&text[offset], &new_inst, sizeof(old_inst));	
            					
            					psec->set_data(&text[0], text.size());
            					
            					uint32_t new_code = 0x8004FF80;
            					uint32_t new_code2 = 0x4048F5C3;
            					uint32_t new_code3 = 0x7E0C0204;
            					psec->append_data((char*) &new_code, sizeof(new_code));
            					psec->append_data((char*) &new_code2, sizeof(new_code2));
            					psec->append_data((char*) &new_code3, sizeof(new_code3));
            					psec->append_data((char*) &old_inst, sizeof(old_inst));
            					psec->append_data((char*) &ret_inst, sizeof(ret_inst));
            					
            					reader.save(filename.c_str());
							}
                            */
						}	
					}
				}
			}
        }

        next = static_cast<const char*>(next) + chunk_size;
    }

    get_handled_fatbins().insert({(uint64_t)data, true });
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
        hipLaunchKernel_fptr = ( hipError_t (*) (const void*, dim3, dim3, void**, size_t, hipStream_t)) dlsym(get_rocm_lib(), "hipLaunchKernel");
    } 
    if (hipModuleLoad_fptr == NULL) {
        hipModuleLoad_fptr = ( hipError_t (*) (hipModule_t* , const char* )) dlsym(get_rocm_lib(), "hipModuleLoad");
    }

    if (hipKernelNameRefByPtr_fptr == NULL) {
        hipKernelNameRefByPtr_fptr = ( const char* (*) (const void*, hipStream_t)) dlsym(get_rocm_lib(), "hipKernelNameRefByPtr");
    }
    if (hipModuleLaunchKernel_fptr == NULL) {
        hipModuleLaunchKernel_fptr = ( hipError_t (*) (hipFunction_t, unsigned int, unsigned int, unsigned int,
                                                          unsigned int, unsigned int, unsigned int, unsigned int,
                                                          hipStream_t, void**, void**)) dlsym(get_rocm_lib(), "hipModuleLaunchKernel");
    }
    if (hipModuleGetFunction_fptr == NULL) {
        hipModuleGetFunction_fptr = ( hipError_t (*) (hipFunction_t*, hipModule_t, const char*)) dlsym(get_rocm_lib(), "hipModuleGetFunction");
    }

    hipError_t result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);

    if (get_tool() == TOOL_CAPTURE) {
        std::string kernel_name = std::string((*hipKernelNameRefByPtr_fptr)(function_address, stream));

        XXH64_hash_t hash = XXH64(kernel_name.data(), kernel_name.size(), 0);
        uint64_t num_args = 0;
        std::printf("Looking up %d: \n", hash);
        std::printf("Table has size %d: \n", get_kernel_arg_sizes().size());
        if (get_kernel_arg_sizes().find(hash) != get_kernel_arg_sizes().end()) {
                num_args = get_kernel_arg_sizes().at(hash).size;
        }

        uint64_t total_size = 0;
        for(int i = 0; i < num_args; i++) {
            std::string key = kernel_name + std::to_string(i);
            hash = XXH64(key.data(), key.size(), 0);
            SizeOffset size_offset = get_kernel_arg_sizes().at(hash);
            total_size = size_offset.offset + size_offset.size;
        }
        std::vector<std::byte> arg_data(total_size);
        for (int i = 0; i < num_args; i++) {
            std::string key = kernel_name + std::to_string(i);
            hash = XXH64(key.data(), key.size(), 0);
            SizeOffset size_offset = get_kernel_arg_sizes().at(hash);
            //std::printf("ARG %d SIZE %d\n", i, size_offset.size);
            if (size_offset.size != 0 && args[i] != NULL) { 
                std::memcpy(arg_data.data() + size_offset.offset, args[i], size_offset.size);
            } else {
                std::memcpy(arg_data.data() + size_offset.offset, &(args[i]), sizeof(void**));
            }
        }

        gputrace_event event;
        gputrace_event_launch launch_event;

        event.id = get_curr_event()++;
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
    } else if (get_tool() == TOOL_MEMTRACE) {
        
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
    if (hipModuleLaunchKernel_fptr == NULL) {
        hipModuleLaunchKernel_fptr = ( hipError_t (*) (hipFunction_t, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, hipStream_t, void**, void**)) dlsym(get_rocm_lib(), "hipModuleLaunchKernel");
    }
    
    if (hipKernelNameRef_fptr == NULL) {
        hipKernelNameRef_fptr = ( const char* (*) (const hipFunction_t)) dlsym(get_rocm_lib(), "hipKernelNameRef");
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

    /*
    //gputrace_event event;
    gputrace_event* event = static_cast<gputrace_event*>(std::malloc(sizeof(gputrace_event)));
    gputrace_event_launch launch_event;

    event->id = get_curr_event()++;
    event->name = "hipModuleLaunchKernel";
    event->rc = result;
    event->stream = stream;
    event->type = EVENT_LAUNCH;

    launch_event.kernel_name = kernel_name;
    launch_event.num_blocks = numBlocks;
    launch_event.dim_blocks = dimBlocks;
    launch_event.shared_mem_bytes = sharedMemBytes;
    launch_event.argdata = arg_data;

    event->data = std::move(launch_event);
    pushback_event(event);
    */

    return result;
}

};
