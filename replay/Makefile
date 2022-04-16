HIP_PATH?= $(wildcard /opt/rocm/hip)
ifeq (,$(HIP_PATH))
	HIP_PATH=/opt/rocm
endif

HIPCC=$(HIP_PATH)/bin/hipcc

TARGET=hcc

SOURCES = replay.cpp elf.cpp
OBJECTS = replay.o elf.o

EXECUTABLE=replay

LLVM_INCLUDE=/home/carozhon/wario/prefix/include/

all: replay

CXXFLAGS = -I../include -I$(LLVM_INCLUDE) -I/home/carozhon/wario/msgpack/include/ -std=c++17

$(EXECUTABLE): $(OBJECTS) 
	$(HIPCC) $(CXXFLAGS) -O3 $(OBJECTS) -o $@ -L/home/carozhon/wario/prefix/lib -lLLVM

replay.o: replay.cpp
	$(HIPCC) $(CXXFLAGS) -O3 -c -o replay.o replay.cpp

#elf.o: elf.cpp
#	$(HIPCC) $(CXXFLAGS) -O3 -c -o elf.o elf.cpp

clean:
	rm -f $(EXECUTABLE)
	rm -f $(OBJECTS)
	rm -f $(HIP_PATH)/src/*.o

