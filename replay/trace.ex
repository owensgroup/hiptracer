LD_PRELOAD=../../hip/libhiptracer.so ./vectoradd_hip.exe
[0] hooked: __hipRegisterFatBinary(0x204568)
[0] writing code object for hipv4-amdgcn-amd-amdhsa--gfx908:
[1] hooked: hipGetDeviceProperties
[1]      p_prop: 0x7ffc84545d70
[1]      device: 0
[1] calling: hipGetDeviceProperties
[1]      result: 0
 System minor 0
 System major 9
 agent prop name
hip Device prop succeeded

    // ... //

[7] hooked: hipLaunchKernel
[7]      function_address: 0x200cc0
[7]      numBlocks: 64 64 1
[7]      dimBlocks: 16 16 1
[7]      args: 0x7ffc84545d40
[7]      sharedMemBytes: 0
[7]      stream: (nil)
[7] calling: hipLaunchKernel
[7]      result: 0
[8] hooked: hipMemcpy
[8]      dst: 0x7fec02eff010
[8]      src: 0x7fec02000000
[8]      size: 4194304
[8]      kind: 2
[8] calling: hipMempcy
[8]      result: 0
PASSED!
[9] hooked: hipFree
[9]      p: 0x7fec02000000
[9] calling: hipFree
[9]      result: 0
[10] hooked: hipFree
[10]     p: 0x7fec01800000
[10] calling: hipFree
[10]     result: 0
[11] hooked: hipFree
[11]     p: 0x7fec01000000
[11] calling: hipFree
[11]     result: 0
