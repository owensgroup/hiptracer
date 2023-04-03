#include <dlfcn.h>
#include <cstdio>
#include <cstddef>
#include <condition_variable>
#include <string>

// DynInst
#include "InstructionDecoder.h"
#include "InsnFactory.h"

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

        std::vector<char> data(i.size());
        std::memcpy(data.data(), i.ptr(), i.size());
        instr.data = data;

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
        //std::printf("%d\n", register_event_id);
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
    //std::printf("num bundles %d\n", fbheader->numBundles);
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
        //std::printf("TRIPLE %s\n", triple.data());

        std::string filename = std::string("./code/") + std::string(triple) + "-" + std::to_string(get_curr_event()) + "-" + std::to_string(i) + ".code";
        std::string image{static_cast<const char*>(fbwrapper->binary) + descriptor->offset, descriptor->size};

        std::istringstream is(image); // FIXME: Makes a second copy?
        getArgInfo(is, get_kernel_arg_sizes(), register_event_id);

        if (get_tool() == TOOL_CAPTURE) {
            gputrace_event event;
            gputrace_event_code code_event;

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
                        std::printf("Instructions length originally %d bytes\n", psec->get_size());
						std::string text{static_cast<const char*>(psec->get_data()), psec->get_size()};
						std::vector<Instr> instructions = get_instructions(text); 
						for (int i = 0; i < instructions.size(); i++) {
							Instr instr = instructions[i];
                            uint32_t base = 0;
                            if (i == 0) {
                                base = instr.offset;
                            }

                            //std::printf("Instr %d: %s\n", i, instr.getCdna());
							if ((instr.isLoad() || instr.isStore()) && instr.isFlat()) { 

                                std::printf("IS LOAD OR STORE\n");
								uint32_t offset = instr.getOffset();	

                                std::vector<char*> instr_pool;
                                auto jumpto = InsnFactory::create_s_branch(offset, base + psec->get_size(), NULL, instr_pool);
                                auto jumpback = InsnFactory::create_s_branch(base + psec->get_size(), offset, NULL, instr_pool);

                                assert(jumpto.size <= instr.size);
 
                                std::memcpy(&text[offset], jumpto.ptr, jumpto.size);

                                uint32_t next_free_vreg = 65; // TODO: Compute next register from kernel descriptor

                                auto savev0 = InsnFactory::create_v_mov_b32(next_free_vreg, 0, instr_pool);
                                auto savev1 = InsnFactory::create_v_mov_b32(next_free_vreg + 1, 1, instr_pool);
                                auto savev2 = InsnFactory::create_v_mov_b32(next_free_vreg + 2, 2, instr_pool);
                                auto saves0 = InsnFactory::create_v_writelane_b32(next_free_vreg + 3, instr_pool);
                                auto saves1 = InsnFactory::create_v_writelane_b32(next_free_vreg + 4, instr_pool);

                                uint32_t v_registers_moved = 3;

                                // MEMTRACE
                                // v_mov_b32_e32 vXX, v0
                                // v_mov_b32_e32 vYY, v1
                                // v_mov_b32_e32 vZZ, v2
                                // s_mov_b64 vWW, s[0:1] ???
                                //         v_cmp_eq_u32_e32 vcc, 0, v0                                // 000000001000: 7D940080
        						// s_and_saveexec_b64 s[0:1], vcc                             // 000000001004: BE80206A
        						// s_cbranch_execz 21                                         // 000000001008: BF880015 <_Z15vectoradd_floatPfPKfS1_ii+0x60>
        						// v_mov_b32_e32 v0, 0xbeefbeef                               // 00000000100C: 7E0002FF BEEFBEEF
        						// v_mov_b32_e32 v1, 0                                        // 000000001014: 7E020280
        						// v_mov_b32_e32 v2, 1                                        // 000000001018: 7E040281
        						// flat_atomic_add v0, v[0:1], v2 glc                         // 00000000101C: DD090000 00000200
        						// s_movk_i32 s0, 0x400                                       // 000000001024: B0000400
        						// s_waitcnt vmcnt(0) lgkmcnt(0)                              // 000000001028: BF8C0070
        						// v_cmp_gt_i32_e32 vcc, s0, v0                               // 00000000102C: 7D880000
        						// s_and_b64 exec, exec, vcc                                  // 000000001030: 86FE6A7E
        						// s_cbranch_execz 10                                         // 000000001034: BF88000A <_Z15vectoradd_floatPfPKfS1_ii+0x60>
        						// v_ashrrev_i32_e32 v1, 31, v0                               // 000000001038: 2202009F
        						// v_lshlrev_b64 v[0:1], 2, v[0:1]                            // 00000000103C: D28F0000 00020082
        						// v_mov_b32_e32 v2, 0xcccccccc                               // 000000001044: 7E0402FF CCCCCCCC
        						// v_add_co_u32_e32 v0, vcc, 0xdeadb000, v0                   // 00000000104C: 320000FF DEADB000
        						// v_addc_co_u32_e32 v1, vcc, 0, v1, vcc                      // 000000001054: 38020280
        						// flat_store_dword v[0:1], v2 offset:3823                    // 000000001058: DC700EEF 00000200
        						// s_endpgm                                                   // 000000001060: BF810000
                                // v_mov_b32_e32 v0, vXX
                                // v_mov_b32_e32 v1, vYY
                                // v_mov_b32_e32 v2, vZZ
                                // s_mov_b64 s[0:1], vWW

								//uint32_t new_inst = 0xBF820000;
								//uint32_t ret_inst = 0xBF820000;

								//new_inst |= (jump / 4) - 1;

								////std::printf("NEW INST 0x%dx", new_inst);

            					//uint32_t old_inst = 0x0; // FIXME ~ Target instr could be 64 bits, ask llvm-mc / DynInst
            					//std::memcpy(&old_inst, &text[offset], sizeof(old_inst));
            					//std::memcpy(&text[offset], &new_inst, sizeof(old_inst));	
            					//
            					//psec->set_data(&curr_text[0], text.size());
            					//
            					//uint32_t new_code = 0x8004FF80;
            					//uint32_t new_code2 = 0x4048F5C3;
            					//uint32_t new_code3 = 0x7E0C0204;
            					//psec->append_data((char*) &new_code, sizeof(new_code));
            					//psec->append_data((char*) &new_code2, sizeof(new_code2));
            					//psec->append_data((char*) &new_code3, sizeof(new_code3));
            					//psec->append_data((char*) &old_inst, sizeof(old_inst));
            					//psec->append_data((char*) &ret_inst, sizeof(ret_inst));            				

                                uint32_t address_register = get_addr_from_flat(instr.data);
                                if (address_register < v_registers_moved - 1) {
                                    address_register += next_free_vreg;
                                }
                                uint32_t mov_addr = 0x7E040200 | address_register;
                                std::printf("FOUND ADDRESS REGISTER %d\n", address_register);

                                uint32_t atomic_addr_low = 0xbeefbeef;
                                uint32_t atomic_addr_high = 0xdeaddead;
                                uint32_t buffer_addr_low = 
								uint32_t memtrace_code[ ] = { 0x7D940080, 0xBE80206A, 0xBF880015, 0x7E0002FF, atomic_addr_low, 0x7E0202FF, , atomic_addr_high, 0x7E040281,
                                                              0xDD090000, 0x00000200, 0xB0000400, 0xBF8C0070, 0x7D880000,  0x86FE6A7E, 0xBF88000A, 0x2202009F,
                                                              0xD28F0000, 0x00020082, mov_addr };

                                psec->append_data((char*) savev0.ptr, savev0.size);
                                psec->append_data((char*) savev1.ptr, savev1.size);
                                psec->append_data((char*) savev2.ptr, savev2.size);
                                psec->append_data((char*) saves0.ptr, saves0.size);
                                psec->append_data((char*) saves1.ptr, saves1.size);

                                psec->append_data((char*) memtrace_code, sizeof(memtrace_code));

                                psec->append_data(instr.data.data(), instr.data.size());
                                psec->append_data((char*) jumpback.ptr, jumpback.size); 

                                std::free(jumpto.ptr);
                                std::free(jumpback.ptr);
                                std::free(savev0.ptr);
                                std::free(savev1.ptr);
                                std::free(savev2.ptr);
                                std::free(saves0.ptr);
                                std::free(saves1.ptr);
							}
						}

						std::string newtext{static_cast<const char*>(psec->get_data()), psec->get_size()};

                        for (int i = 0; i < text.size(); i++) {
                            newtext[i] = text[i];
                        }

                        psec->set_data(&newtext[0], newtext.size());

                        std::printf("Instructions length now %d bytes \n", psec->get_size());
            		    reader.save(filename.c_str()); 
                        get_filenames().push_back(filename);
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

    std::string kernel_name = std::string((*hipKernelNameRefByPtr_fptr)(function_address, stream));


    XXH64_hash_t hash = XXH64(kernel_name.data(), kernel_name.size(), 0);
    uint64_t num_args = 0;
    //std::printf("Looking up %d: \n", hash);
    //std::printf("Table has size %d: \n", get_kernel_arg_sizes().size());
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
            // "HIDDEN" case
            std::memcpy(arg_data.data() + size_offset.offset, &(args[i]), sizeof(void**));
        }
    }

    hipError_t result;
    if (get_tool() == TOOL_CAPTURE) {
        result = (*hipLaunchKernel_fptr)(function_address, numBlocks, dimBlocks, args, sharedMemBytes, stream);

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
        // Get modified form of kernel

        std::vector<hipModule_t> modules;

        for (int i = 0; i < get_filenames().size(); i++) {
            hipModule_t module;
            hipModuleLoad_fptr(&module, get_filenames()[i].c_str());
            modules.push_back(module);
        } 

        hipFunction_t function;
        for (int i = 0; i < modules.size(); i++) {
            hipError_t ret = hipModuleGetFunction_fptr(&function, modules[i], kernel_name.c_str());
            if (ret == hipSuccess) {
                break; 
            }
        }

        hipModuleLaunchKernel(function, numBlocks.x, numBlocks.y, numBlocks.z, dimBlocks.x, dimBlocks.y, dimBlocks.z, sharedMemBytes,
                              stream, args, NULL);
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
