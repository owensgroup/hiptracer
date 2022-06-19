HIP_PATH?= $(wildcard /opt/rocm/hip)
ifeq (,$(HIP_PATH))
	HIP_PATH=/opt/rocm
endif

HIPCC=$(HIP_PATH)/bin/hipcc

TARGET=hcc

SOURCES = replay.cpp
OBJECTS = replay.o

EXECUTABLE=replay

LLVM_INCLUDE=/home/carozhon/wario/prefix/include/

all: replay

CXXFLAGS = -g -I../include -std=c++17 -I/opt/rocm/llvm/include

LIBS = -lLLVMWindowsManifest -lLLVMXRay -lLLVMLibDriver -lLLVMDlltoolDriver -lLLVMCoverage -lLLVMLineEditor -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen -lLLVMX86Desc -lLLVMX86Info -lLLVMAMDGPUTargetMCA -lLLVMAMDGPUDisassembler -lLLVMAMDGPUAsmParser -lLLVMAMDGPUCodeGen -lLLVMAMDGPUDesc -lLLVMAMDGPUUtils -lLLVMAMDGPUInfo -lLLVMOrcJIT -lLLVMMCJIT -lLLVMJITLink -lLLVMInterpreter -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMOrcTargetProcess -lLLVMOrcShared -lLLVMDWP -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoGSYM -lLLVMDebuginfod -lLLVMOption -lLLVMOffloadArch -lLLVMObjectYAML -lLLVMMCA -lLLVMMCDisassembler -lLLVMLTO -lLLVMPasses -lLLVMCFGuard -lLLVMCoroutines -lLLVMObjCARCOpts -lLLVMipo -lLLVMVectorize -lLLVMLinker -lLLVMInstrumentation -lLLVMFrontendOpenMP -lLLVMFrontendOpenACC -lLLVMExtensions -lLLVMDWARFLinker -lLLVMGlobalISel -lLLVMMIRParser -lLLVMAsmPrinter -lLLVMDebugInfoMSF -lLLVMSelectionDAG -lLLVMCodeGen -lLLVMIRReader -lLLVMAsmParser -lLLVMInterfaceStub -lLLVMFileCheck -lLLVMFuzzMutate -lLLVMTarget -lLLVMScalarOpts -lLLVMInstCombine -lLLVMAggressiveInstCombine -lLLVMTransformUtils -lLLVMBitWriter -lLLVMAnalysis -lLLVMProfileData -lLLVMDebugInfoDWARF -lLLVMObject -lLLVMTextAPI -lLLVMMCParser -lLLVMMC -lLLVMDebugInfoCodeView -lLLVMBitReader -lLLVMCore -lLLVMRemarks -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMTableGen -lLLVMSupport -lLLVMDemangle -lrt -ldl -lpthread -lm -lz -ltinfo

replay: replay.o
	$(HIPCC) replay.o -L /opt/rocm/llvm/lib/ $(LIBS) -o replay 

replay.o: replay.cpp
	$(HIPCC) $(CXXFLAGS) -c replay.cpp
    
clean:
	rm -f $(EXECUTABLE)
	rm -f $(OBJECTS)
	rm -f $(HIP_PATH)/src/*.o

