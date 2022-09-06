HIP_PATH?= $(wildcard /opt/rocm/hip)
ifeq (,$(HIP_PATH))
	HIP_PATH=/opt/rocm
endif

HIPCC=$(HIP_PATH)/bin/hipcc

TARGET=hcc

SOURCES = vectoradd_hip.cpp 
OBJECTS = $(SOURCES:.cpp=.o)

EXECUTABLE=./vectoradd_hip.exe

.PHONY: test

CXXFLAGS=

CXX=$(HIPCC)

$(EXECUTABLE): $(OBJECTS) 
	$(HIPCC) -g -O0 $(OBJECTS) -o $@

$(TRACER): ../../hip/hip.cpp
	make -C ../../hip/

clean:
	rm -f $(EXECUTABLE)
	rm -f $(OBJECTS)
	rm -f $(HIP_PATH)/src/*.o

