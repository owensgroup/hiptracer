#include <cstdlib>

void hipt_at_init() {
    std::printf("Starting MEMTRACE\n");
    return ;
}

void hipt_at_launch(int is_exit, const void* function,
                    const char* kernel_name,
                    void** args) {
    if (is_exit) {
        std::printf("Exiting launch for kernel %s\n", kernel_name);
    } else {
        std::printf("Entering launch for kernel %s\n", kernel_name);
    }
}
