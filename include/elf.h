#include "elfio/elfio.hpp"

#include "llvm/BinaryFormat/MsgPackDocument.h"

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

                std::string metadata_blob(reinterpret_cast<char *>(desc), descSize);
                llvm::msgpack::Document doc;
                doc.readFromBlob(metadata_blob, false);

                auto args = doc.getRoot()
                    .getMap(true)[doc.getNode("amdhsa.kernels")]
                    .getArray(true)[0]
                    .getMap(true)[doc.getNode(".args")]
                    .getArray(true);

                std::cout << "Arg size " << args.size() << std::endl; 
                for (int i = 0; i < args.size(); i++) { 
                    auto a = args[i].getMap(true);
                    ArgInfo info;
                    if (a[doc.getNode(".address_space")].isString()) {
                        info.address_space = std::string(a[doc.getNode(".address_space")].getString().str());
                    } if (a[doc.getNode(".size")].isScalar()) {
                        info.size = a[doc.getNode(".size")].getUInt();
                    } if (a[doc.getNode(".offset")].isScalar()) {
                        info.offset = a[doc.getNode(".offset")].getUInt();
                    } if (a[doc.getNode(".value_kind")].isString()) {
                        info.value_kind = std::string(a[doc.getNode(".value_kind")].getString().str());
                    } if (a[doc.getNode(".access")].isString()) {
                        info.access = std::string(a[doc.getNode(".access")].getString().str());
                    }
                    arg_infos.push_back(info);
                }
            }
        }
    } 

    for (int i = 0; i < arg_infos.size(); i++) {
        auto info = arg_infos[i];
        if (info.address_space.size() > 0)
            std::cout << info.address_space << std::endl;
        if (info.access.size() > 0)
            std::cout << info.access.size() << std::endl;
        std::cout << info.size << std::endl;
        std::cout << info.offset << std::endl;
        std::cout << info.value_kind << std::endl;
    }

    return arg_infos;
}
