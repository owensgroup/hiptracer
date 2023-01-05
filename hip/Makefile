OBJS	= hiptracer.pic.o msgpuck.o hints.o sqlite.o
OUT	= libhiptracer.so
CXX = clang++
CC = clang
FLAGS	 = -c -Wall -fPIC -pedantic -std=c++17
INC = -I../include/ -I/opt/rocm/llvm/include -isystem/opt/rocm/include
LIBS = -ldl -lzstd -lpthread

#Link step
all: hiptracer.pic.o msgpuck.o hints.o sqlite.o
	$(CXX) -shared -g $(OBJS) -o $(OUT) $(LIBS) --no-undefined

hiptracer.pic.o: hip.cpp sqlite.o
	$(CXX) -g -o $@ $(INC) $(FLAGS) hip.cpp sqlite.o

msgpuck.o: msgpuck.c
	$(CC) -o $@ $(INC) -fPIC -c msgpuck.c

hints.o: hints.c
	$(CC) -o $@ $(INC) -fPIC -c hints.c

sqlite.o: sqlite3.c
	$(CC) -o $@ $(INC) -fPIC -c sqlite3.c

clean:
	rm -f $(OBJS) $(OUT)
