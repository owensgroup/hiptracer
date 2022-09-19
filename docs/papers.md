# Thesis Statement for Papers / QE
- This is a thesis(es) for papers and the QE. 

- I am taking what I built and trying to apply it to several problems that I think have research-value. Each bullet is one potential paper idea, and at the end I have a "thesis statement" made from putting them together.

- I'm basing each item off specific papers from conferences that did similar things (ex MICRO, SIGMETRICS).

## Paper Ideas
We built a set of tools to obtain sequences of library calls and executed GPU binary code.
We propose using these tools in the following interesting ways:

* We can **instrument** program behavior. - By placing a sequence of instrumentation instructions at the start and end of a block of GPU code, we can instrument the behavior of that code. We use captures to provide a reproducible sequence of events for this instrumentation. NVBIT implemented several examples of **instrumentation** functions, such as "mem_trace", which prints the location of each load and store while activated, or "instr_count", which counts the number of executed instructions. They use these functions to **evaluate** existing hardware.
    - Whisper (MICRO 2022) - https://web.eecs.umich.edu/~takh/papers/khan-whisper-micro-2022.pdf - They use this form of binary instrumentation to propose a new form of branch prediction. Then they insert those predictions as hints. You could also place this under **optimization**.
    - CachePerf: A Unified Cache Miss Classifier via Hybrid Hardware Sampling -- https://arxiv.org/abs/2203.08943
    
* We can feed our traces into a **simulator** to propose new hardware. - Rather than feeding a source executable to a simulator like GPGPUSim or GEM5, we can provide a sequence of instructions to be executed, and the context for that execution. (I think this could be a lot more useful, but I don't have a concrete way of saying that yet. I think "here's our architecture simulating tensorflow for 5mins" is more valuable than "here's vectoradd simulated with our architecture". 
     - Vulkan-Sim: A GPU Architecture Simulator for Ray Tracing (MICRO 2022) https://zenodo.org/record/6941190
     
* We can perform runtime **optimizations**. - When we have a sequence of instructions that were executed (and the resulting branches and execution masks), we can translate that sequence to another sequence that is equivalent but performs faster. We can also try and detect certain patterns that are bad and replace them with better versions. Normally, I would argue that it wouldn't be possible to "optimize" this "optimized" code any more, but we are making optimization decisions based on runtime information - information that is not available to a compiler, so we should be able to make some improvements based on these traces.
    - Memory Space Recycling (SIGMETRICS 2022) - https://dl.acm.org/doi/abs/10.1145/3508034   
    - Speculative Code Compaction: Eliminating Dead Code via Speculative Microcode Transformations (Micro 2022) - (probably, can't find a PDF to read it, but "speculative microcode transformations" sounds identical)
    - Work w/ Zhongyi on ML apps? (kernel fusion, measuring divergence, ) 
    
* We can use the binary traces to **translate** programs. - Suppose we wish to translate a binary from one architecture to another. There will be certain constructs in the resulting translated binary that are always "bad" on the target architecture. This may be due to some structure of the original program, or some feature that the target architecture lacks. But we can use our ability to capture binary traces to capture the "bad" sequences that have been generated. We can then translate these to more efficient structures on the target architecture. This could be used in a bunch of really cool ways:
   - Emulate a Compute API on a Graphics API -  HIP has very poor support outside the MIX00 cards. This would allow us to translate a program that contains HIP calls and binary code and execute it within a Vulkan or OpenGL runtime.
   - Emulate a Compute API on another Compute API - We could emulate HIP on CUDA or run CUDA programs on HIP.
   - Emulate different architectures - We could run a gfx908 binary on a gfx90a machine, or we could run sm_30 CUDA code on a sm_70 machine. This allows us to keep software used on newer architectures without requiring it be recompiled (or needing to carry PTX). 

## As a QE Thesis Statement
To improve the performance of GPU applications and ensure they remain usable and performant without recompilation, we propose the follow thesis:
  A system that captures a sequence of library calls and executed GPU binary instructions can be used to instrument devices, propose new hardware, perform unique optimizations at runtime, and emulate architectures on different hardware.
