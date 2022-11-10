struct Inst {
    enum SIZE {
        SIZE32 = 32, SIZE64 = 64, SIZE128 = 128
    };
    std::string inst_name;
    uint64_t offset;
    SIZE size;
};

enum JUMPWHEN {
    BEFORE = 1, AFTER = 2
};

struct Rewrite {
    uint64_t offset;
    JUMPWHEN when;
    void* device_func;
};
// Take a list of instructions, return a list of rewrites to perform
void list_instrumentation_points(
 /* input */ Inst* instructions, uint64_t instructions_len,
 /* output*/ Rewrite** rewrites, uint64_t* rewrites_len);
