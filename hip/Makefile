OBJS	= hiptracer.pic.o msgpuck.o hints.o
OUT	= libhiptracer.so
CC	 = clang++
FLAGS	 = -c -Wall -fPIC -pedantic -std=c++17
INC = -I../include/ -I/opt/rocm/llvm/include
LIBS = -ldl -lzstd

#Link step
all: hiptracer.pic.o msgpuck.o hints.o
	$(CC) -shared $(OBJS) -o $(OUT) $(LIBS) --no-undefined

hiptracer.pic.o: hip.cpp
	$(CC) -o $@ $(INC) $(FLAGS) hip.cpp 

msgpuck.o: msgpuck.c
	$(CC) -o $@ $(INC) $(FLAGS) msgpuck.c

hints.o: hints.c
	$(CC) -o $@ $(INC) $(FLAGS) hints.c

clean:
	rm -f $(OBJS) $(OUT)
