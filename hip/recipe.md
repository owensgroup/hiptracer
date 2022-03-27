clang -Wall -fPIC -O -g abc1.c -c -o abc1.pic.o
clang -shared abc1.pic.o -ldl -o libabc.so
