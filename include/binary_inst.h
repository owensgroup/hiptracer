#ifndef BINARY_INST_H
#define BINARY_INST_H

#include <cstdint>

void hipt_at_init();
void hipt_at_exit();
void hipt_at_launch(int is_exit, const void* function,
                    const char* kernel_name,
                    void** args); 
/*
void hipt_get_lineinfo(void* function, int offset,
                       std::string& filename, std::string& directory,
                       int& line);
 */
struct hipt_inst {
    std::string print();
    uint64_t offset;
    int get_num_operands();
    bool is_load();
    bool is_store();
};

std::vector<hipt_inst> hipt_list_insts(void* func);

void hipt_insert_call(hipt_inst& inst, const char* filename, 
                      const char* dev_func);
void hipt_add_call_arg_vreg(hipt_inst& inst, int reg);
void hipt_add_call_arg_sreg(hipt_inst& inst, int reg);
void hipt_add_call_arg_const_u32(hipt_inst& inst, uint32_t value);
void hipt_add_call_arg_const_u64(hipt_inst& inst, uint64_t value);

int hipt_get_num_registers(void* function);
int hipt_get_regs_required(const char* dev_func);

#endif // BINARY_INST_H
