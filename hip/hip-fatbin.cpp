#include <dlfcn.h>
#include <cstdio>
#include <cstddef>
#include <condition_variable>
#include <string>

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

// DynInst
#include "InstructionDecoder.h"
#include "InsnFactory.h"

// Capture
#include "elf.h"
#include "trace.h"
#include "sqlite3.h"
#include "atomic_queue/atomic_queue.h"
#include "flat_hash_map.hpp"

// Binary Instrumentation
#include "AMDHSAKernelDescriptor.h"
#include "binary_inst.h"

#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime.h>

std::vector<char> get_hip_code(const char* filename) {
    ELFIO::elfio reader;
    if (!reader.load(filename)) {
        std::printf("Loading file %s failed\n", filename);
    } 
    for ( int i = 0; i < reader.sections.size(); ++i ) {
        ELFIO::section* psec = reader.sections[i];
        std::printf("Section name: %s \n", psec->get_name().c_str());
    }
}

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

        instructions.push_back(instr);

        offset += instr.size;
        i = decoder.decode();
    }
    std::printf("INSTRUCTIONS SIZE %d\n", instructions.size());
    return instructions;
}

std::vector<char> get_injected_instructions(int* atomics, uint64_t* buffer, uint64_t buffer_size, uint32_t address_register, int regsUsed)
{
    std::vector<char> instrs;
    std::vector<char*> instr_pool;
    uint32_t next_free_vreg = regsUsed; // TODO: Compute next register from kernel descriptor
    uint32_t vreg_to_save = 6;
    uint32_t sreg_to_save = 6;

    const uint32_t waitRegs[] = {0xBF8C0000 };
    for (uint32_t i = 0; i < sizeof(waitRegs); i ++) {
        instrs.push_back(((char*)waitRegs)[i]);
    }

    for (uint32_t i = 0; i < vreg_to_save; i++) {
        auto save_vec = InsnFactory::create_v_mov_b32(next_free_vreg + i, i, instr_pool);
        for (uint32_t j = 0; j < save_vec.size; j++) {
            instrs.push_back(((char*)save_vec.ptr)[j]);
        }
        std::free(save_vec.ptr);
    }
    for (uint32_t i = 0; i < sreg_to_save; i++) {
        auto save_scalar = InsnFactory::create_v_writelane_b32(next_free_vreg + vreg_to_save + i, 0, i, instr_pool);
        for (uint32_t j = 0; j < save_scalar.size; j++) {
            instrs.push_back(((char*)save_scalar.ptr)[j]);
        }
        std::free(save_scalar.ptr);
    }

    for (uint32_t i = 0; i < sizeof(waitRegs); i ++) {
        instrs.push_back(((char*)waitRegs)[i]);
    }

    uint32_t v_registers_moved = vreg_to_save;

    if (address_register < v_registers_moved - 1) {
        address_register += next_free_vreg;
    }

    uint32_t atomic_addr_low = reinterpret_cast<uint64_t>(atomics) & (0x00000000FFFFFFFF);
    uint32_t atomic_addr_high = (reinterpret_cast<uint64_t>(atomics) & (0xFFFFFFFF00000000)) >> 32;
    uint32_t buffer_addr_low = reinterpret_cast<uint64_t>(buffer) & (0x00000000FFFFFFFF);
    uint32_t buffer_addr_high = (reinterpret_cast<uint64_t>(buffer) & (0xFFFFFFFF00000000)) >> 32; 
    uint32_t buffer_size_low = (reinterpret_cast<uint64_t>(buffer_size) & (0x00000000FFFFFFFF));
    uint32_t buffer_size_high = (reinterpret_cast<uint64_t>(buffer_size) & (0xFFFFFFFF00000000)) >> 32; 

/*
	v_mov_b32_e32 v0, 1                                        // 000000001608: 7E000281
	v_mov_b32_e32 v1, 0                                        // 00000000160C: 7E020280
	v_mov_b32_e32 v2, 0                                        // 000000001610: 7E040280
	s_waitcnt lgkmcnt(0)                                       // 000000001614: BF8CC07F
	global_atomic_add_x2 v[0:1], v2, v[0:1], s[2:3] glc        // 000000001618: DD898000 00020002
	s_waitcnt vmcnt(0)                                         // 000000001620: BF8C0F70

	v_cmp_ge_u64_e32 vcc, s[4:5], v[0:1]                       // 000000001624: 7DDC0004
	s_and_saveexec_b64 s[2:3], vcc                             // 000000001628: BE82206A
	s_cbranch_execz 9                                          // 00000000162C: BF880009 <_Z8memtracePmS_mm+0x54>
	v_lshlrev_b64 v[0:1], 3, v[0:1]                            // 000000001630: D28F0000 00020083
	v_mov_b32_e32 v4, s1                                       // 000000001638: 7E080201
	v_add_co_u32_e32 v0, vcc, s0, v0                           // 00000000163C: 32000000
	v_mov_b32_e32 v2, s6                                       // 000000001640: 7E040206
	v_mov_b32_e32 v3, s7                                       // 000000001644: 7E060207
	v_addc_co_u32_e32 v1, vcc, v4, v1, vcc                     // 000000001648: 38020304
	global_store_dwordx2 v[0:1], v[2:3], off                   // 00000000164C: DC748000 007F0200
	s_endpgm                                                   // 000000001654: BF810000
*/

    auto move_addr_low = InsnFactory::create_v_readlane_b32(6, 0, address_register, instr_pool);
    auto move_addr_high = InsnFactory::create_v_readlane_b32(7, 0, address_register + 1, instr_pool);

    //for (int i = 0; i < move_addr_low.size; i++) {
    //    instrs.push_back(((char*)move_addr_low.ptr)[i]);
    //}
    //std::free(move_addr_low.ptr);
    //for (int i = 0; i < move_addr_high.size; i++) {
    //    instrs.push_back(((char*)move_addr_high.ptr)[i]);
    //}
    std::free(move_addr_high.ptr);

    for (uint32_t i = 0; i < sizeof(waitRegs); i ++) {
        instrs.push_back(((char*)waitRegs)[i]);
    }

    if (HIPT_CURRENT_APP == HIPT_APP_INSTRCOUNT) {
        const uint32_t setup_arguments_code[] = { 0xBE8200FF, atomic_addr_low, 0xBE8300FF, atomic_addr_high  };
    /*{ 0xBE8000FF, buffer_addr_low, 0xBE8100FF, buffer_addr_high, 0xBE8200FF, atomic_addr_low, 0xBE8300FF, atomic_addr_high, };*/
                                              /*0xBE8400FF, buffer_size_low, 0xBE8500FF, buffer_size_high }; */
        const uint32_t count_code[] = { 0x7E000281, 0x7E020280, 0x7E040280, 0xBF8CC07F, 0xDD898000,
                                            0x00020002, 0xBF8C0F70 }; /*0x7DDC0004, 0xBE82206A, 
                                            0xBF880009, 0xD28F0000, 0x00020083, 0x7E080201,
                                            0x32000000, 0x7E040206, 0x7E060207, 0x38020304,
                                            0xDC748000, 0x007F0200, 0xBF8C0070};*/


        for (int i = 0; i < sizeof(setup_arguments_code); i++) {
            instrs.push_back(((char*)setup_arguments_code)[i]);
        }
 
        for (int i = 0; i < sizeof(count_code); i++) {
            instrs.push_back(((char*)count_code)[i]);
        }
    } else if (HIPT_CURRENT_APP == HIPT_APP_MEMTRACE) {
    //const uint32_t memtrace_code_sec1[] = { 0x7E0602A0, 0x7E0202FF, atomic_addr_low, 0x7E080280, 0x7E0402FF, atomic_addr_high, 0xDD890000, 0x03000301};
    //const uint32_t memtrace_code_sec2[] = { /*0x2600009F, 0x7E0A02FF, buffer_addr_low, 0xBF8C0070, 0x32060103, 0x38080880, */ 0xD28F0003, 0x00020683, 0x320606FF, buffer_addr_high, 0x38080905 };

    //const uint32_t memtrace_code_sec3[] = { 0xDC740000, 0x00000103 };

            const uint32_t memtrace_code_sec1[] = { 0x7E0002FF, atomic_addr_low, 0x7E0202FF, atomic_addr_high, 0x7E0402A0, 0xDD090000, 0x00000200};
    const uint32_t memtrace_code_sec2[] = { 0xBF8C0070, 0x2202009F, 0xBF8C0070, 0xD28F0000, 0x00020082, 0x320000FF, buffer_addr_low, 0x7E0602FF, buffer_addr_high, 0x38020303 };

    const uint32_t memtrace_code_sec3[] = { 0xDC740000, 0x00000200, 0xBF8C0070 };


        for (int i = 0; i < sizeof(memtrace_code_sec1); i++) {
            instrs.push_back(((char*)memtrace_code_sec1)[i]);
        }
        for (int i = 0; i < sizeof(memtrace_code_sec2); i++) {
            instrs.push_back(((char*)memtrace_code_sec2)[i]);
        }

        auto move_addr_low = InsnFactory::create_v_mov_b32(2, address_register, instr_pool);
        auto move_addr_high = InsnFactory::create_v_mov_b32(3, address_register + 1, instr_pool);

        for (int i = 0; i < move_addr_low.size; i++) {
            instrs.push_back(((char*)move_addr_low.ptr)[i]);
        }
        std::free(move_addr_low.ptr);
        for (int i = 0; i < move_addr_high.size; i++) {
            instrs.push_back(((char*)move_addr_high.ptr)[i]);
        }
        std::free(move_addr_high.ptr);

        for (uint32_t i = 0; i < sizeof(waitRegs); i ++) {
            instrs.push_back(((char*)waitRegs)[i]);
        }

        for (int i = 0; i < sizeof(memtrace_code_sec3); i++) {
            instrs.push_back(((char*)memtrace_code_sec3)[i]);
        } 
    }

    for (uint32_t i = 0; i < vreg_to_save; i++) {
        auto load_vec = InsnFactory::create_v_mov_b32(i, next_free_vreg + i, instr_pool);
        for (int j = 0; j < load_vec.size; j++) {
            instrs.push_back(((char*)load_vec.ptr)[j]);
        }
        std::free(load_vec.ptr);
    }
    for (uint32_t i = 0; i < sreg_to_save; i++) {
        auto load_scalar = InsnFactory::create_v_readlane_b32(i, 0, next_free_vreg + i + vreg_to_save, instr_pool);
        for (int j = 0; j < load_scalar.size; j++) {
            instrs.push_back(((char*)load_scalar.ptr)[j]);
        }
        std::free(load_scalar.ptr);
    }

    for (uint32_t i = 0; i < sizeof(waitRegs); i ++) {
        instrs.push_back(((char*)waitRegs)[i]);
    }
    
    return instrs;
}

void readToKd(const uint8_t *rawBytes, size_t rawBytesLength,
                                size_t fromIndex, size_t numBytes,
                                uint8_t *data) {
  assert(rawBytes && "rawBytes must be non-null");
  assert(data && "data must be non-null");
  assert(fromIndex + numBytes <= rawBytesLength);

  for (size_t i = 0; i < numBytes; ++i) {
    size_t idx = fromIndex + i;
    data[i] = rawBytes[idx];
  }
}

void addInstrumentation(ELFIO::section* psec, std::string& text, uint32_t base, Instr instr,  std::vector<char>& injected_instrs) {
    assert(psec != nullptr);

    uint64_t offset = instr.getOffset();

    std::vector<char*> instr_pool;
    auto jumpto = InsnFactory::create_s_branch(offset, base + psec->get_size(), NULL, instr_pool);

    assert(jumpto.size <= instr.size);

    // JUMP TO
    std::memcpy(&text[offset], jumpto.ptr, jumpto.size);
    uint32_t dummy = 0xBF800000; // s_nop
    printf("INSTR SIZE %d\n", instr.size);
    if (instr.size == 8) {
        std::memcpy(&text[offset + jumpto.size], &dummy, 4);
    }
    // Execute INJECTED
    psec->append_data(injected_instrs.data(), injected_instrs.size()); 
    // Execute ORIGINAL INSTRUCTION
    psec->append_data(instr.data.data(), instr.data.size()); 
    // JUMP BACK
    auto jumpback = InsnFactory::create_s_branch(base + psec->get_size(), offset + 4, NULL, instr_pool);
    psec->append_data((char*) jumpback.ptr, jumpback.size);     
}

int editKernelDescriptor(ELFIO::elfio& reader, ELFIO::Elf64_Addr value, ELFIO::Elf_Half section_index, int reg_needed) {
    ELFIO::section* kd_region = reader.sections[section_index];
    std::printf("kd_region name %s size %d\n", kd_region->get_name().c_str(), kd_region->get_size());
    const size_t kd_offset = value - kd_region->get_address();

    std::printf("kd offset %d\n", kd_offset);
    llvm::amdhsa::kernel_descriptor_t kdRepr;

    uint8_t *kdPtr = (uint8_t *)&kdRepr;
    uint8_t *kdBytes = (uint8_t*) (kd_region->get_data() + kd_offset);

    const size_t kdSize = 64;
    size_t found_idx = 0;
    size_t idx = 0;
    while (idx < kdSize) {
        switch (idx) {
            case llvm::amdhsa::GROUP_SEGMENT_FIXED_SIZE_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::PRIVATE_SEGMENT_FIXED_SIZE_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::KERNARG_SIZE_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::RESERVED0_OFFSET:
                readToKd(kdBytes, kdSize, idx, 4 * sizeof(int8_t), kdPtr + idx);
                idx += 4 * sizeof(uint8_t);
                break;

            case llvm::amdhsa::KERNEL_CODE_ENTRY_BYTE_OFFSET_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint64_t), kdPtr + idx);
                idx += sizeof(uint64_t);
                break;

            case llvm::amdhsa::RESERVED1_OFFSET:
                readToKd(kdBytes, kdSize, idx, 20 * sizeof(uint8_t), kdPtr + idx);
                idx += 20 * sizeof(uint8_t);
                break;

            case llvm::amdhsa::COMPUTE_PGM_RSRC3_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::COMPUTE_PGM_RSRC1_OFFSET:
                found_idx = idx;
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::COMPUTE_PGM_RSRC2_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint32_t), kdPtr + idx);
                idx += sizeof(uint32_t);
                break;

            case llvm::amdhsa::KERNEL_CODE_PROPERTIES_OFFSET:
                readToKd(kdBytes, kdSize, idx, sizeof(uint16_t), kdPtr + idx);
                idx += sizeof(uint16_t);
                break;

            case llvm::amdhsa::RESERVED2_OFFSET:
                readToKd(kdBytes, kdSize, idx, 6 * sizeof(uint8_t), kdPtr + idx);
                idx += 6 * sizeof(uint8_t);
                break;
        }
    }

#define GET_VALUE(MASK) ((fourByteBuffer & MASK) >> (MASK##_SHIFT))
#define SET_VALUE(MASK) (fourByteBuffer | ((14) << (MASK##_SHIFT)))
#define CLEAR_BITS(MASK) (fourByteBuffer & (~(MASK)))
#define CHECK_WIDTH(MASK) ((vall) >> (MASK##_WIDTH) == 0)

    uint32_t fourByteBuffer = kdRepr.compute_pgm_rsrc1;
    int regsUsed = (GET_VALUE(llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT) + 1) * 4;
    std::printf("REG USED %d\n", regsUsed);
    std::printf("REG NEEDED %d\n", reg_needed);
    int reg = 8;
    if (regsUsed + reg_needed <= 16) {
        reg = 16;
    } else if (regsUsed + reg_needed <= 40) {
        reg = 40;
    } else if (regsUsed + reg_needed <= 64) {
        reg = 64;
    }
    fourByteBuffer = CLEAR_BITS(llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT);
    fourByteBuffer = (fourByteBuffer | ((reg / 4 - 1) << (llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT_SHIFT)));

    std::printf("AFTER GRANULATED %d\n", GET_VALUE(llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WORKITEM_VGPR_COUNT));
    //fourByteBuffer = CLEAR_BITS(llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WAVEFRONT_SGPR_COUNT);
    //fourByteBuffer = (fourByteBuffer | ((12) << (llvm::amdhsa::COMPUTE_PGM_RSRC1_GRANULATED_WAVEFRONT_SGPR_COUNT_SHIFT)));
    //std::printf("FOUR BYTE %d\n", fourByteBuffer);

    std::string data{kd_region->get_data(), kd_region->get_size()}; 
    std::memcpy(data.data() + kd_offset + found_idx, &fourByteBuffer, 4); // Copy kernel descriptor back

    kd_region->set_data(data.data(), data.size());

    return regsUsed;
}

void captureFatBin(std::string image, std::string filename)
{
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
}

void captureLaunch() {

}

void binintSetupForFatBin(std::string image, std::string filename) {
			std::istringstream is(image);
			ELFIO::elfio reader;
			reader.load(is);

            hipError_t  (*hipMalloc_fptr)(void**, size_t) = NULL;
            if (hipMalloc_fptr == NULL) {
                hipMalloc_fptr = (hipError_t (*) (void**, size_t)) dlsym(get_rocm_lib(), "hipMalloc");
                assert(hipMalloc_fptr != NULL);
            }

            int* atomics = nullptr;
            uint64_t* buffer = nullptr;
            const size_t BUFFER_SIZE = 55000000; //33554432
            hipMalloc_fptr((void**) &atomics, sizeof(int) * 1); // TODO: MORE ATOMICS, LESS CONTENTION
            hipMalloc_fptr((void**) &buffer, sizeof(uint64_t) * BUFFER_SIZE);

            hipError_t  (*hipMemcpy_fptr)(void*, const void*, size_t, hipMemcpyKind) = NULL;
            
            if (hipMemcpy_fptr == NULL) {
                hipMemcpy_fptr = (hipError_t (*) (void*, const void*, size_t, hipMemcpyKind)) dlsym(get_rocm_lib(), "hipMemcpy");
            }
            int zero_atomics = 0;
            hipError_t memcpy_result = hipMemcpy_fptr(atomics, &zero_atomics, sizeof(int), hipMemcpyHostToDevice);
            assert(memcpy_result == hipSuccess);

            std::vector<uint64_t> host_buffer(BUFFER_SIZE);
            for (int i = 0; i < BUFFER_SIZE; i++) {
                host_buffer[i] = 0;
            }

            memcpy_result = hipMemcpy_fptr(buffer, host_buffer.data(), sizeof(uint64_t) * BUFFER_SIZE, hipMemcpyHostToDevice);

            assert(atomics != NULL);
            assert(buffer != NULL);
            std::printf("ATOMICS %p BUFFER %p\n", atomics, buffer);

            get_atomic_addr() = (uint64_t) atomics;
            get_buffer_addr() = (uint64_t) buffer;

            int regsUsed;
			for(int i = 0; i < reader.sections.size(); i++) {
				ELFIO::section* psec = reader.sections[i];

				if (psec) {
					std::string sec_name = psec->get_name();

                    if ( psec->get_type() == ELFIO::SHT_SYMTAB) {
                        const ELFIO:: symbol_section_accessor symbols(reader, psec);
                        for (int j = 0; j < symbols.get_symbols_num(); ++j) {
                            std::string name;
                            ELFIO::Elf64_Addr value;
                            ELFIO::Elf_Xword size;
                            unsigned char bind;
                            unsigned char type;
                            ELFIO::Elf_Half section_index;
                            unsigned char other;

                            symbols.get_symbol(j, name, value, size, bind, type, section_index, other); 
                            if (name.find(".kd") != std::string::npos) {
                                int regsNeeded = 16;
                                regsUsed = editKernelDescriptor(reader, value, section_index, regsNeeded);
                            }
                        }
                    }
               }
           }
           for (int i = 0; i < reader.sections.size(); i++) {
               ELFIO::section* psec = reader.sections[i];
               if (psec) {
					std::string sec_name = psec->get_name();
					if (sec_name == std::string(".text")) {
                        std::printf("Instructions length originally %d bytes\n", psec->get_size());
						std::string text{static_cast<const char*>(psec->get_data()), psec->get_size()};
						std::vector<Instr> instructions = get_instructions(text); 
                        int needed_sreg = 0;
                        int needed_vreg = 0; 
						for (int i = 0; i < instructions.size(); i++) {
							Instr instr = instructions[i];
                            uint32_t base = 0;
                            if (i == 0) {
                                base = instr.offset;
                            }

                            //std::printf("Instr %d: %s\n", i, instr.getCdna());
                            if (HIPT_CURRENT_APP == HIPT_APP_INSTRCOUNT && i == HIPT_INSTRUMENTED_INSTR) {
							//if ((instr.isLoad() || instr.isStore()) && instr.isGlobal()) {  
                            //    uint32_t address_register = InsnFactory::get_addr_from_flat(instr.data); 
                            //    std::vector<char> injected_instructions = get_injected_instructions(atomics, buffer, BUFFER_SIZE, address_register);

                            //    addInstrumentation(psec, text, base, instr, injected_instructions);

							//}
                                std::vector<char> injected_instructions = get_injected_instructions(atomics, buffer, BUFFER_SIZE, 0, regsUsed);
                                addInstrumentation(psec, text, base, instr, injected_instructions);
                            } else if (HIPT_CURRENT_APP == HIPT_APP_MEMTRACE && i == HIPT_INSTRUMENTED_INSTR) { 
                                assert(instr.isLoad() || instr.isStore());
                                std::vector<char> injected_instructions = get_injected_instructions(atomics, buffer, BUFFER_SIZE, 0, regsUsed);
                                addInstrumentation(psec, text, base, instr, injected_instructions);
                            }

						}

						std::string newtext{static_cast<const char*>(psec->get_data()), psec->get_size()};

                        for (int i = 0; i < text.size(); i++) {
                            newtext[i] = text[i];
                        }

                        psec->set_data(&newtext[0], newtext.size());

                        std::printf("Instructions length now %d bytes \n", psec->get_size());
					}
				}
			}
           	reader.save(filename.c_str()); 
            get_filenames().push_back(filename);
}

void binintLaunch() {

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
//        auto at_init = get_hiptracer_state().at_init_fptr;
//        if (at_init == nullptr) {
//            at_init = hipt_at_init;
//        }
        //hipt_at_init();
    }
    if (get_handled_fatbins().find((uint64_t) data) != get_handled_fatbins().end()) {
        if (get_handled_fatbins().at((uint64_t) data)) return (*hipRegisterFatBinary_fptr)(data);
    }

    int register_event_id = get_curr_event()++;

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

        std::string filename = std::string("./code/") + std::string(triple) + "-" + std::to_string(get_curr_event()) + "-" + std::to_string(i) + ".code";
        std::string image{static_cast<const char*>(fbwrapper->binary) + descriptor->offset, descriptor->size};

        std::istringstream is(image); // FIXME: Makes a second copy?
        getArgInfo(is, get_kernel_arg_sizes(), register_event_id);

        if (get_tool() == TOOL_CAPTURE) {
            captureFatBin(image, filename);
        } else if (get_tool() == TOOL_BININT) {
            binintSetupForFatBin(image, filename);
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
    } else if (get_tool() == TOOL_BININT) {
        // Get modified form of kernel
        std::vector<hipModule_t> modules;

        for (int i = 0; i < get_filenames().size(); i++) {
            hipModule_t module;
            hipModuleLoad_fptr(&module, get_filenames()[i].c_str());
            //std::printf("FILE %s\n", get_filenames()[i].c_str());
            modules.push_back(module);
        } 

        hipFunction_t function;
        for (int i = 0; i < modules.size(); i++) {
            hipError_t ret = hipModuleGetFunction_fptr(&function, modules[i], kernel_name.c_str());
            if (ret == hipSuccess) {
                break; 
            }
        }

        //auto at_launch = get_hiptracer_state().at_launch_fptr;
        //if (at_launch == nullptr) {
        //    at_launch = hipt_at_launch;
        //}

        //hipt_at_launch(0, function_address, kernel_name.c_str(), args);
        hipModuleLaunchKernel(function, numBlocks.x, numBlocks.y, numBlocks.z, dimBlocks.x, dimBlocks.y, dimBlocks.z, sharedMemBytes,
                              stream, args, NULL);
        //hipt_at_launch(1, function_address, kernel_name.c_str(), args);
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
