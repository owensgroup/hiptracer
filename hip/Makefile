OBJS	= hiptracer.pic.o
SOURCE	= hip.cpp
OUT	= libhiptracer.so
CC	 = clang++
FLAGS	 = -c -Wall -fPIC
LFLAGS	 = -ldl

all: $(OBJS)
	$(CC) -shared $(OBJS) -o $(OUT) $(LFLAGS)

hiptracer.pic.o: hip.cpp
	$(CC) $(FLAGS) hip.cpp 

clean:
	rm -f $(OBJS) $(OUT)
