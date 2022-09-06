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

LIBS = -ldl

replay: replay.o
	$(HIPCC) ../hip/msgpuck.o ../hip/hints.o replay.o -L /opt/rocm/llvm/lib/ $(LIBS) -lreadline -o replay 

replay.o: replay.cpp
	$(HIPCC) $(CXXFLAGS) -c replay.cpp
 
clean:
	rm -f $(EXECUTABLE)
	rm -f $(OBJECTS)
	rm -f $(HIP_PATH)/src/*.o

