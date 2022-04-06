OBJS	= hiptracer.pic.o
SOURCE	= hip.cpp
OUT	= libhiptracer.so
CC	 = clang++
FLAGS	 = -c -Wall -fPIC -pedantic -std=c++11
LFLAGS	 = -ldl
INC = ../include/

all: hiptracer.pic.o hip.exe
	$(CC) -shared $(OBJS) -o $(OUT) $(LFLAGS)
	$(CC) $(OBJS) -o hip.exe $(LFLAGS)

hiptracer.pic.o: hip.cpp
	$(CC) -o $@ -I$(INC) $(FLAGS) hip.cpp 

clean:
	rm -f $(OBJS) $(OUT)
