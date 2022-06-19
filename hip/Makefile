OBJS	= hiptracer.pic.o
SOURCE	= hip.cpp
OUT	= libhiptracer.so
CC	 = clang++
FLAGS	 = -c -Wall -fPIC -pedantic -std=c++17 -g
INC = -I../include/ -I/opt/rocm/llvm/include
LIBS = -lLLVMWindowsManifest -lLLVMXRay -lLLVMLibDriver -lLLVMDlltoolDriver -lLLVMCoverage -lLLVMLineEditor -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen -lLLVMX86Desc -lLLVMX86Info -lLLVMAMDGPUTargetMCA -lLLVMAMDGPUDisassembler -lLLVMAMDGPUAsmParser -lLLVMAMDGPUCodeGen -lLLVMAMDGPUDesc -lLLVMAMDGPUUtils -lLLVMAMDGPUInfo -lLLVMOrcJIT -lLLVMMCJIT -lLLVMJITLink -lLLVMInterpreter -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMOrcTargetProcess -lLLVMOrcShared -lLLVMDWP -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoGSYM -lLLVMDebuginfod -lLLVMOption -lLLVMOffloadArch -lLLVMObjectYAML -lLLVMMCA -lLLVMMCDisassembler -lLLVMLTO -lLLVMPasses -lLLVMCFGuard -lLLVMCoroutines -lLLVMObjCARCOpts -lLLVMipo -lLLVMVectorize -lLLVMLinker -lLLVMInstrumentation -lLLVMFrontendOpenMP -lLLVMFrontendOpenACC -lLLVMExtensions -lLLVMDWARFLinker -lLLVMGlobalISel -lLLVMMIRParser -lLLVMAsmPrinter -lLLVMDebugInfoMSF -lLLVMSelectionDAG -lLLVMCodeGen -lLLVMIRReader -lLLVMAsmParser -lLLVMInterfaceStub -lLLVMFileCheck -lLLVMFuzzMutate -lLLVMTarget -lLLVMScalarOpts -lLLVMInstCombine -lLLVMAggressiveInstCombine -lLLVMTransformUtils -lLLVMBitWriter -lLLVMAnalysis -lLLVMProfileData -lLLVMDebugInfoDWARF -lLLVMObject -lLLVMTextAPI -lLLVMMCParser -lLLVMMC -lLLVMDebugInfoCodeView -lLLVMBitReader -lLLVMCore -lLLVMRemarks -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMTableGen -lLLVMSupport -lLLVMDemangle -lrt -ldl -lpthread -lm -lz -ltinfo

all: hiptracer.pic.o
	$(CC) -shared $(OBJS) -o $(OUT) -L /opt/rocm/llvm/lib/ $(LIBS)	

hiptracer.pic.o: hip.cpp
	$(CC) -o $@ $(INC) $(FLAGS) hip.cpp 

clean:
	rm -f $(OBJS) $(OUT)
