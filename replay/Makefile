HIP_PATH?= $(wildcard /opt/rocm/hip)
ifeq (,$(HIP_PATH))
	HIP_PATH=/opt/rocm
endif

HIPCC=$(HIP_PATH)/bin/hipcc

TARGET=hcc

SOURCES = replay.hip
OBJECTS = replay.o

EXECUTABLE=replay

LLVM_INCLUDE=/home/carozhon/wario/prefix/include/

all: replay

CXXFLAGS = -g -I../include -std=c++17 -I/opt/rocm/llvm/include

LIBS = -ldl

replay: replay.o
	$(HIPCC) -g ../hip/msgpuck.o ../hip/sqlite.o ../hip/hints.o replay.o -L /opt/rocm/llvm/lib/ $(LIBS) -lreadline -o replay 

replay.o: replay.hip
	$(HIPCC) $(CXXFLAGS) -g -c replay.hip
 
clean:
	rm -f $(EXECUTABLE)
	rm -f $(OBJECTS)
	rm -f $(HIP_PATH)/src/*.o

