#include "elfio/elfio.hpp"

#include "msgpuck.h"

#include <iostream>
#include <cstdio>
#include <string>
#include <map>

using namespace ELFIO;

struct ArgInfo {
    std::string address_space;
    uint64_t size;
    uint64_t offset;
    std::string value_kind;
    std::string access;
};

std::vector<ArgInfo> getArgInfo(const char* fname)
{
    elfio reader;
	
    if (!reader.load(fname) ) {
        std::cout << "Unable to find elf file" << std::endl;
    } else {
    	std::cout << "Found ELF file " << fname << std::endl;
    }

    // Print ELF file sections info
    Elf_Half sec_num = reader.sections.size(); 
	std::cout <<" Sections " << sec_num << std::endl;

    std::vector<ArgInfo> arg_infos;
    for ( int i = 0; i < sec_num; ++i ) {
        section* psec = reader.sections[i];
		std::printf("TYPE %d SHT_NOTE == %d\n", psec->get_type(), SHT_NOTE);
        if (psec->get_type() == SHT_NOTE) {
            note_section_accessor notes(reader, psec);

            std::printf("Num notes: %d\n", notes.get_notes_num());
            for (int i = 0; i < notes.get_notes_num(); i++) {
                Elf_Word type;
                std::string name;
                void* desc;
                Elf_Word descSize;
                notes.get_note(i, type, name, desc, descSize);

                const char* r = (const char*)desc;
                uint32_t map_size = mp_decode_map(&r);
                std::printf("Map Size: %d \n", map_size);
                for (int i = 0; i < map_size; i++) {
                    uint32_t key_len;
                    const char* key = mp_decode_str(&r, &key_len);
                    
                    std::printf("Key Length: %d\n", key_len);
                    std::printf("Key:%.*s\n", key_len, key);
                    
                    if (std::string(key, key_len) == "amdhsa.kernels") {
                        uint32_t num_kernels = mp_decode_array(&r);
                        for (int i = 0; i < num_kernels; i++) {
                            uint32_t kern_map_size = mp_decode_map(&r);

                            for (int i = 0; i < kern_map_size; i++) {
                                uint32_t kkey_len;
                                const char* kern_key = mp_decode_str(&r, &kkey_len);

                                if (std::string(kern_key, kkey_len) == ".args") {
                                    uint32_t num_args = mp_decode_array(&r);
                                    for (int i = 0; i < num_args; i++) {
                                        uint32_t arg_map_size = mp_decode_map(&r);
                                        ArgInfo info;
                                        for (int i = 0; i < arg_map_size; i++) {
                                            uint32_t arg_len;
                                            const char* arg_key = mp_decode_str(&r, &arg_len);
                                            
                                            if (std::string(arg_key, arg_len) == ".address_space") {
                                                uint32_t addr_len;
                                                const char* address_space = mp_decode_str(&r, &addr_len);
                                                info.address_space = std::string(address_space, addr_len);
                                            } else if (std::string(arg_key, arg_len) == ".size") {
                                                info.size = mp_decode_uint(&r);
                                            } else if (std::string(arg_key, arg_len) == ".offset") {
                                                info.offset = mp_decode_uint(&r);
                                            } else if (std::string(arg_key, arg_len) == ".value_kind") {
                                                uint32_t valkind_len;
                                                const char* value_kind = mp_decode_str(&r, &valkind_len);
                                                if (valkind_len > 0)
                                                    info.value_kind = std::string(value_kind, valkind_len);
                                            } else if (std::string(arg_key, arg_len) == ".access") {
                                                uint32_t access_len;
                                                const char* access_str = mp_decode_str(&r, &access_len);
                                                if (access_len > 0)
                                                    info.access = std::string(access_str, access_len);
                                            } else {
                                                mp_next(&r);
                                            }
                                        }
                                        arg_infos.push_back(info);
                                    }
                                    
                                } else {
                                    mp_next(&r); // Skip value
                                }
                            }
                        }
                    }
                    else {
                        mp_next(&r);
                    }
                }
            }
        }
    } 

    for (int i = 0; i < arg_infos.size(); i++) {
        auto info = arg_infos[i];
        if (info.address_space.size() > 0)
            std::cout << "Address Space :: " << info.address_space << std::endl;
        if (info.access.size() > 0)
            std::cout << "ACCESS " << info.access.size() << std::endl;
        std::cout << "Size" << info.size << std::endl;
        std::cout << "Offset" << info.offset << std::endl;
        std::cout << "Value Kind: " << info.value_kind << std::endl;
    }

    return arg_infos;
}
